# SPDX-License-Identifier: GPL-2.0-only

import subprocess
import time
import os
import pytest
import signal

# Note: bin_path and interface fixtures are provided implicitly by conftest.py

def test_isotp_usage(bin_path):
    """
    Test usage/help output for isotpsend and isotprecv.
    isotpsend returns error on -h.
    isotprecv returns usage on missing args.
    """
    isotpsend = os.path.join(bin_path, "isotpsend")
    isotprecv = os.path.join(bin_path, "isotprecv")

    if os.path.exists(isotpsend):
        # isotpsend: invalid option -h
        # Note: The tool might exit with 0 even on invalid options in some builds.
        # We rely on checking the output text.
        res_send = subprocess.run(
            [isotpsend, "-h"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        # We do not assert returncode != 0 here because it was observed to be 0
        assert "Usage: isotpsend" in res_send.stderr or "Usage: isotpsend" in res_send.stdout

    if os.path.exists(isotprecv):
        # isotprecv: no args -> usage -> exit code != 0 (typically) or 0?
        # Based on your output, it prints Usage. We check valid stderr/stdout output.
        res_recv = subprocess.run(
            [isotprecv],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        # We assume non-zero because usually tools requiring args exit with error
        # checking the output text is the most important part.
        assert "Usage: isotprecv" in res_recv.stderr or "Usage: isotprecv" in res_recv.stdout

def check_isotp_support(bin_path, interface):
    """
    Helper to check if ISO-TP kernel module is loaded/available.
    Tries to run isotpsend for a split second.
    """
    isotpsend = os.path.join(bin_path, "isotpsend")
    # Try to send a dummy frame. If socket creation fails, it exits with error.
    # We use a timeout to ensure it doesn't hang.
    try:
        # -s 123 -d 321 are dummy IDs
        proc = subprocess.Popen(
            [isotpsend, "-s", "123", "-d", "321", interface],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        # Send empty or minimal data to close quickly if successful or fail if socket error
        try:
            outs, errs = proc.communicate(input="00", timeout=0.5)
        except subprocess.TimeoutExpired:
            proc.kill()
            outs, errs = proc.communicate()

        if "socket: Protocol not supported" in errs or "socket: Protocol not supported" in outs:
            return False
        return True
    except Exception:
        return False

@pytest.mark.parametrize("payload_hex", [
    "AA BB CC",                                      # Single Frame (< 8 bytes)
    "00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE"   # Multi Frame (> 8 bytes, triggers segmentation)
])
def test_isotp_transmission(bin_path, can_interface, payload_hex):
    """
    Test ISO-TP transmission between isotpsend and isotprecv.

    Description:
    1. Start isotprecv (Receiver) in background.
       It listens on RX_ID (e.g., 321) and sends Flow Control on TX_ID (e.g., 123).
    2. Run isotpsend (Sender).
       It sends on TX_ID (e.g., 123) and listens for Flow Control on RX_ID (e.g., 321).
    3. Verify that isotprecv outputs exactly the data sent by isotpsend.

    This verifies:
    - Kernel ISO-TP stack (socket creation).
    - Addressing (Source/Dest IDs).
    - Segmentation/Reassembly (for long payloads).
    """
    isotpsend = os.path.join(bin_path, "isotpsend")
    isotprecv = os.path.join(bin_path, "isotprecv")

    if not os.path.exists(isotpsend) or not os.path.exists(isotprecv):
        pytest.skip("isotpsend or isotprecv binary not found.")

    if not check_isotp_support(bin_path, can_interface):
        pytest.skip("ISO-TP kernel support missing (socket failed). Load can-isotp module.")

    # IDs for the connection
    # Sender transmits with ID_A, Receiver listens to ID_A
    ID_A = "123"
    ID_B = "321"

    # 1. Start Receiver (isotprecv)
    # -s <src> -d <dst>
    # In isotprecv context: -s is usually the ID it sends FC on, -d is ID it listens to?
    # Actually, can-utils convention is often:
    # isotprecv -s <tx_id> -d <rx_id>
    # To listen to what isotpsend (-s 123 -d 321) sends:
    # We need a socket that listens on 123 and writes on 321.
    # So isotprecv should be: -s 321 -d 123
    recv_cmd = [isotprecv, "-s", ID_B, "-d", ID_A, can_interface]

    recv_proc = subprocess.Popen(
        recv_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    try:
        time.sleep(0.5) # Wait for receiver to bind socket

        # 2. Run Sender (isotpsend)
        # -s 123 -d 321
        send_cmd = [isotpsend, "-s", ID_A, "-d", ID_B, can_interface]

        # Pass payload via stdin
        send_proc = subprocess.run(
            send_cmd,
            input=payload_hex,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=2
        )

        assert send_proc.returncode == 0, f"isotpsend failed: {send_proc.stderr}"

        # 3. Verify Receiver Output
        # isotprecv prints the received PDU. It might not exit automatically unless we kill it
        # OR if we used -l (loop). Without -l, it typically exits after one PDU.
        # We wait briefly for it to finish.
        try:
            recv_stdout, recv_stderr = recv_proc.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            recv_proc.terminate()
            recv_stdout, recv_stderr = recv_proc.communicate()

        # Check if the payload is in the output
        # Output format is usually "AA BB CC ..."
        # We normalize spaces for comparison
        expected = " ".join(payload_hex.split())
        actual = " ".join(recv_stdout.split())

        print(f"--- Sender Stderr: ---\n{send_proc.stderr}")
        print(f"--- Receiver Stdout: ---\n{recv_stdout}")

        assert expected in actual, f"Receiver did not receive correct data. Expected '{expected}', got '{actual}'"

    finally:
        if recv_proc.poll() is None:
            recv_proc.terminate()
            recv_proc.wait()
