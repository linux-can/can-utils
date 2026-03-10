# SPDX-License-Identifier: GPL-2.0-only

import subprocess
import time
import os
import pytest
import signal
import pty

# Note: bin_path and interface fixtures are provided implicitly by conftest.py

def test_j1939spy_usage(bin_path):
    """
    Test usage/help output for j1939spy.

    Manual Reproduction:
    1. Run: ./j1939spy -h
    2. Expect: Output containing "Usage: j1939spy".
    """
    j1939spy = os.path.join(bin_path, "j1939spy")
    if not os.path.exists(j1939spy):
        pytest.skip("j1939spy binary not found")

    result = subprocess.run(
        [j1939spy, "-h"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    assert "Usage: j1939spy" in result.stderr or "Usage: j1939spy" in result.stdout

def test_j1939spy_sniffing(bin_path, can_interface):
    """
    Test j1939spy sniffing functionality.

    Description:
    1. Start j1939spy in promiscuous mode (-P) on the interface using PTY to avoid buffering.
    2. Generate J1939 traffic using j1939sr (Source 0x90 -> Dest 0x80).
    3. Verify that j1939spy captures and displays the traffic.

    Manual Reproduction:
    1. Spy: ./j1939spy -P vcan0
    2. Transfer: echo "test" | ./j1939sr vcan0:90 vcan0:80
    3. Check j1939spy output for "test" (hex: 74657374).
    """
    j1939spy = os.path.join(bin_path, "j1939spy")
    j1939sr = os.path.join(bin_path, "j1939sr")

    for tool in [j1939spy, j1939sr]:
        if not os.path.exists(tool):
            pytest.skip(f"{tool} not found")

    # 1. Start j1939spy
    # Command: ./j1939spy -P vcan0
    cmd_spy = [j1939spy, "-P", can_interface]
    print(f"DEBUG: Starting spy: {' '.join(cmd_spy)}")

    # Use PTY to force line buffering (simulates terminal behavior)
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

        # 2. Generate Traffic
        # Using j1939sr from SA 0x90 to SA 0x80
        src_addr = f"{can_interface}:90"
        dst_addr = f"{can_interface}:80"
        payload_str = "test"
        # Hex for "test" is 74 65 73 74. j1939spy usually prints hex.

        cmd_sr = [j1939sr, src_addr, dst_addr]
        print(f"DEBUG: Generating traffic: echo '{payload_str}' | {' '.join(cmd_sr)}")

        sr_proc = subprocess.run(
            cmd_sr,
            input=payload_str + "\n",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )

        if sr_proc.returncode != 0:
            print(f"DEBUG: j1939sr stderr: {sr_proc.stderr}")

        # 3. Verify capture
        # Read from PTY
        time.sleep(1.0)

        output = ""
        try:
            # Read non-blocking or simple read since we know data should be there
            # Using os.read on master_fd
            while True:
                # Basic non-blocking read approach or just one big read if buffer has data
                # For test stability, just read a chunk.
                chunk = os.read(master_fd, 4096).decode('utf-8', errors='replace')
                if not chunk:
                    break
                output += chunk
                if "74657374" in output:
                    break
        except OSError:
            pass

        print(f"DEBUG: j1939spy output:\n{output}")

        # Expected output format from user description:
        # vcan0:90,00000 80 !6 [5] 74657374 0a
        # We look for the payload hex "74657374" (test)
        expected_hex = "74657374"

        assert expected_hex in output, f"j1939spy did not capture the payload hex '{expected_hex}'"

    finally:
        if spy_proc.poll() is None:
            spy_proc.terminate()
            try:
                spy_proc.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                spy_proc.kill()

        if master_fd:
            os.close(master_fd)
