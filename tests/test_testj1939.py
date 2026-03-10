# SPDX-License-Identifier: GPL-2.0-only

import subprocess
import time
import os
import pytest
import signal
import pty
import select # Import select here or at top level

# Note: bin_path and interface fixtures are provided implicitly by conftest.py

def test_testj1939_usage(bin_path):
    """
    Test usage/help output for testj1939.

    Manual Reproduction:
    1. Run: ./testj1939 -h
    2. Expect: Output containing "Usage: testj1939".
    """
    testj1939 = os.path.join(bin_path, "testj1939")
    if not os.path.exists(testj1939):
        pytest.skip("testj1939 binary not found")

    result = subprocess.run(
        [testj1939, "-h"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    assert "Usage: testj1939" in result.stderr or "Usage: testj1939" in result.stdout

def test_testj1939_send(bin_path, can_interface):
    """
    Test sending functionality of testj1939.

    Description:
    1. Start j1939spy (as receiver) in promiscuous mode on the interface using PTY.
    2. Run testj1939 to send 10 bytes of dummy data (-s 10) from Source 0x90 to Dest 0x80.
    3. Verify that j1939spy captures the payload.

    Manual Reproduction:
    1. Receiver: ./j1939spy -P vcan0
    2. Sender: ./testj1939 vcan0:90 vcan0:80 -s 10
    3. Check j1939spy output for payload "01234567 89abcdef" (standard dummy pattern of testj1939).
    """
    testj1939 = os.path.join(bin_path, "testj1939")
    j1939spy = os.path.join(bin_path, "j1939spy")

    for tool in [testj1939, j1939spy]:
        if not os.path.exists(tool):
            pytest.skip(f"{tool} not found")

    # 1. Start Receiver (j1939spy)
    # Command: ./j1939spy -P vcan0
    cmd_spy = [j1939spy, "-P", can_interface]
    print(f"DEBUG: Starting spy: {' '.join(cmd_spy)}")

    # Use PTY to ensure line buffering for the spy tool
    master_fd, slave_fd = pty.openpty()

    spy_proc = subprocess.Popen(
        cmd_spy,
        stdout=slave_fd,
        stderr=subprocess.PIPE,
        text=True
    )
    os.close(slave_fd) # Close slave in parent

    try:
        # Allow spy to start
        time.sleep(0.5)
        if spy_proc.poll() is not None:
            _, err = spy_proc.communicate()
            pytest.fail(f"j1939spy failed to start. Stderr: {err}")

        # 2. Run Sender (testj1939)
        # Command: ./testj1939 vcan0:90 vcan0:80 -s 10
        # Source SA: 0x90, Dest SA: 0x80, Size: 10 bytes
        # testj1939 generates a dummy pattern. Based on logs: "01234567 89abcdef"
        src_arg = f"{can_interface}:90"
        dst_arg = f"{can_interface}:80"

        cmd_send = [testj1939, src_arg, dst_arg, "-s", "10"]
        print(f"DEBUG: Sending data: {' '.join(cmd_send)}")

        send_proc = subprocess.run(
            cmd_send,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )

        if send_proc.returncode != 0:
            print(f"DEBUG: testj1939 stderr: {send_proc.stderr}")
            # Note: Depending on kernel and socket state, send might fail or succeed.
            # Usually sendto() succeeds on CAN even without receiver.

        # 3. Verify capture
        # Read from PTY
        time.sleep(1.0)

        output = ""
        found = False

        # Expected Payload: Based on error log "01234567 89abcdef"
        # We search for "01234567" which is the start of the payload
        expected_pattern_hex = "01234567"

        try:
            start_read = time.time()
            # Try reading for up to 3 seconds
            while time.time() - start_read < 3.0:
                # Use select to check if data is available to read
                r, _, _ = select.select([master_fd], [], [], 0.5)
                if master_fd in r:
                    chunk = os.read(master_fd, 4096).decode('utf-8', errors='replace')
                    if chunk:
                        output += chunk
                        if expected_pattern_hex in output:
                            found = True
                            break
                # Check if process died
                if spy_proc.poll() is not None:
                    break
        except OSError:
            pass

        print(f"DEBUG: j1939spy output:\n{output}")

        assert found, f"j1939spy did not capture the expected payload pattern '{expected_pattern_hex}'. Received output:\n{output}"

    finally:
        if spy_proc.poll() is None:
            spy_proc.terminate()
            try:
                spy_proc.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                spy_proc.kill()

        if master_fd:
            os.close(master_fd)
