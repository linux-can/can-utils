# SPDX-License-Identifier: GPL-2.0-only

import subprocess
import time
import os
import pytest
import signal
import tempfile

# Note: bin_path and interface fixtures are provided implicitly by conftest.py

def test_j1939cat_usage(bin_path):
    """
    Test usage/help output for j1939cat.

    Manual Reproduction:
    1. Run: ./j1939cat -h
    2. Expect: Output containing "Usage: j1939cat".
    """
    j1939cat = os.path.join(bin_path, "j1939cat")
    if not os.path.exists(j1939cat):
        pytest.skip("j1939cat binary not found")

    # Some tools return 0 on -h, others non-zero. We just check output.
    result = subprocess.run(
        [j1939cat, "-h"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    assert "Usage: j1939cat" in result.stderr or "Usage: j1939cat" in result.stdout

def test_j1939cat_transfer_p2p(bin_path, can_interface):
    """
    Test Point-to-Point data transfer using j1939cat.

    Description:
    1. Start a receiver instance of j1939cat listening on SA 0x90.
    2. Start a sender instance sending data from SA 0x80 to SA 0x90 (PGN 0x12300).
    3. Verify that the receiver prints the sent data.

    Manual Reproduction:
    1. Receiver: ./j1939cat vcan0:0x90 -r
    2. Sender: echo "Hello J1939" | ./j1939cat vcan0:0x80 :0x90,0x12300
    3. Check receiver output for "Hello J1939".
    """
    j1939cat = os.path.join(bin_path, "j1939cat")
    if not os.path.exists(j1939cat):
        pytest.skip("j1939cat binary not found")

    # Define Addresses (Hex strings as per example usage)
    # Receiver Address (SA=0x90)
    recv_addr = f"{can_interface}:0x90"

    # Sender Address (SA=0x80)
    send_addr_from = f"{can_interface}:0x80"
    # Destination (SA=0x90, PGN=0x12300)
    # Note: The example usage ":0x90,0x12300" implies destination address 0x90 and PGN 0x12300.
    # The leading colon implies reusing the interface or defaults.
    send_addr_to = ":0x90,0x12300"

    payload = "Hello J1939 World"

    # Create a temp file for input
    with tempfile.NamedTemporaryFile(mode='w+', delete=False) as tmp_in:
        tmp_in.write(payload)
        tmp_in_path = tmp_in.name

    # 1. Start Receiver
    # Command: j1939cat <IFACE>:0x90 -r
    cmd_recv = [j1939cat, recv_addr, "-r"]
    print(f"DEBUG: Receiver cmd: {' '.join(cmd_recv)}")

    recv_proc = subprocess.Popen(
        cmd_recv,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    try:
        # Allow receiver to bind
        time.sleep(1.0)

        if recv_proc.poll() is not None:
            _, err = recv_proc.communicate()
            pytest.fail(f"Receiver failed to start. Stderr: {err}")

        # 2. Start Sender
        # Command: j1939cat -i <file> <IFACE>:0x80 :0x90,0x12300
        cmd_send = [j1939cat, "-i", tmp_in_path, send_addr_from, send_addr_to]
        print(f"DEBUG: Sender cmd: {' '.join(cmd_send)}")

        send_proc = subprocess.run(
            cmd_send,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )

        if send_proc.returncode != 0:
            pytest.fail(f"Sender failed with exit code {send_proc.returncode}. Stderr: {send_proc.stderr}")

        # 3. Verify Reception
        # Wait a brief moment for processing
        time.sleep(1.0)

        # Terminate receiver to read remaining output
        recv_proc.terminate()
        try:
            recv_proc.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            recv_proc.kill()

        stdout, stderr = recv_proc.communicate()

        print(f"DEBUG: Receiver Output:\n{stdout}")
        print(f"DEBUG: Receiver Stderr:\n{stderr}")

        assert payload in stdout, "Receiver did not output the sent payload"

    finally:
        # Cleanup
        if recv_proc.poll() is None:
            recv_proc.kill()
        if os.path.exists(tmp_in_path):
            os.remove(tmp_in_path)
