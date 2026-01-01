# SPDX-License-Identifier: GPL-2.0-only

import subprocess
import time
import os
import pytest
import signal

# Note: bin_path and interface fixtures are provided implicitly by conftest.py

def test_j1939sr_usage(bin_path):
    """
    Test usage/help output for j1939sr.

    Manual Reproduction:
    1. Run: ./j1939sr -h
    2. Expect: Output containing "Usage: j1939sr".
    """
    j1939sr = os.path.join(bin_path, "j1939sr")
    if not os.path.exists(j1939sr):
        pytest.skip("j1939sr binary not found")

    # j1939sr -h typically returns error exit code because -h is invalid option
    result = subprocess.run(
        [j1939sr, "-h"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    assert "Usage: j1939sr" in result.stderr or "Usage: j1939sr" in result.stdout

def test_j1939sr_transfer(bin_path, can_interface):
    """
    Test basic data transfer using j1939sr.

    Description:
    1. Start a receiver (j1939sr) listening on a specific SA (Source Address).
    2. Start a sender (j1939sr) sending from another SA to the receiver's SA.
    3. Verify data integrity.

    Manual Reproduction:
    1. Receiver: ./j1939sr vcan0:80
    2. Sender: echo "TestPayload" | ./j1939sr vcan0:90 vcan0:80
    3. Check output of Receiver.
    """
    j1939sr = os.path.join(bin_path, "j1939sr")
    if not os.path.exists(j1939sr):
        pytest.skip("j1939sr binary not found")

    # Addresses (Hex without 0x prefix based on user feedback)
    recv_sa_hex = "80"
    send_sa_hex = "90"

    # Receiver Command: ./j1939sr vcan0:80
    # Listens on vcan0, address 0x80
    recv_arg = f"{can_interface}:{recv_sa_hex}"
    cmd_recv = [j1939sr, recv_arg]
    print(f"DEBUG: Receiver cmd: {' '.join(cmd_recv)}")

    # We set stdin=subprocess.PIPE to prevent the tool from detecting EOF immediately
    # and exiting. This simulates a running terminal session.
    recv_proc = subprocess.Popen(
        cmd_recv,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    try:
        # Allow receiver to bind socket
        time.sleep(1.0)

        # Check if it is still running. A listener SHOULD still be running.
        if recv_proc.poll() is not None:
            stdout, stderr = recv_proc.communicate()
            print(f"DEBUG: Receiver Stdout:\n{stdout}")
            # If it exited with 0, it might mean it finished 'nothing' or errored gracefully.
            # For a server, early exit is a failure.
            pytest.fail(f"Receiver exited prematurely (rc={recv_proc.returncode}). Stderr: {stderr}")

        # Sender Command: ./j1939sr vcan0:90 vcan0:80
        # SOURCE: vcan0:90 (Sender Address)
        # DEST:   vcan0:80 (Receiver Address)
        send_arg_src = f"{can_interface}:{send_sa_hex}"
        send_arg_dst = f"{can_interface}:{recv_sa_hex}"

        cmd_send = [j1939sr, send_arg_src, send_arg_dst]
        print(f"DEBUG: Sender cmd: {' '.join(cmd_send)}")

        payload = "J1939TestMessage\n" # newline is important for line buffering tools

        # Send data via stdin to sender process
        send_proc = subprocess.run(
            cmd_send,
            input=payload,
            stdout=subprocess.PIPE, # Capture to avoid clutter
            stderr=subprocess.PIPE,
            text=True
        )

        if send_proc.returncode != 0:
            pytest.fail(f"Sender failed with exit code {send_proc.returncode}. Stderr: {send_proc.stderr}")

        # Verify reception
        # We give it a moment to process
        time.sleep(0.5)

        # Terminate receiver to finish reading
        if recv_proc.poll() is None:
            recv_proc.terminate()
            try:
                recv_proc.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                recv_proc.kill()

        stdout, stderr = recv_proc.communicate()

        print(f"DEBUG: Receiver Output:\n{stdout}")

        if stderr:
             print(f"DEBUG: Receiver Stderr:\n{stderr}")

        # Check if payload exists in output
        assert payload.strip() in stdout, "Receiver did not output the sent payload"

    finally:
        if recv_proc.poll() is None:
            recv_proc.kill()
