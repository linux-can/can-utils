# SPDX-License-Identifier: GPL-2.0-only

import subprocess
import time
import os
import pytest
import signal
import pty
import select

# Note: bin_path and interface fixtures are provided implicitly by conftest.py

def check_isotp_support(bin_path, interface):
    """
    Helper to check if ISO-TP kernel module is loaded/available.
    Reused logic to prevent failing tests on systems without can-isotp.
    """
    isotpsend = os.path.join(bin_path, "isotpsend")
    if not os.path.exists(isotpsend):
        return False

    try:
        proc = subprocess.Popen(
            [isotpsend, "-s", "123", "-d", "321", interface],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
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

def test_isotpperf_usage(bin_path):
    """
    Test usage/help output for isotpperf.

    Manual Reproduction:
    1. Run: ./isotpperf -h
    2. Expect: Output containing "Usage: isotpperf".
    """
    isotpperf = os.path.join(bin_path, "isotpperf")

    if not os.path.exists(isotpperf):
        pytest.skip("isotpperf binary not found")

    result = subprocess.run(
        [isotpperf, "-h"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    # Check for usage info in stdout or stderr (some tools print help to stderr)
    assert "Usage: isotpperf" in result.stderr or "Usage: isotpperf" in result.stdout

def test_isotpperf_measurement(bin_path, can_interface):
    """
    Test isotpperf measurement functionality with a separate receiver.

    Description:
    1. Start isotpperf to monitor/measure the transfer.
    2. Start isotprecv (in background) to act as the active receiver (providing Flow Control).
    3. Send data (25 bytes) using isotpsend.
    4. Verify that isotpperf correctly reports "25 byte in".

    Manual Reproduction:
    1. ./isotpperf -s 123 -d 321 vcan0
    2. ./isotprecv -s 321 -d 123 vcan0 (Required for Flow Control on multi-frame)
    3. echo "00 ... 11" | ./isotpsend -s 123 -d 321 vcan0
    4. Check isotpperf output for "25 byte in".
    """
    isotpperf = os.path.join(bin_path, "isotpperf")
    isotprecv = os.path.join(bin_path, "isotprecv")
    isotpsend = os.path.join(bin_path, "isotpsend")

    for tool in [isotpperf, isotprecv, isotpsend]:
        if not os.path.exists(tool):
            pytest.skip(f"{tool} not found")

    if not check_isotp_support(bin_path, can_interface):
        pytest.skip("ISO-TP kernel support missing")

    perf_src = "123"
    perf_dst = "321"

    # 1. Start isotpperf
    # Arguments: -s <source_id> -d <dest_id> <interface>
    cmd_perf = [isotpperf, "-s", perf_src, "-d", perf_dst, can_interface]
    print(f"DEBUG: Starting isotpperf: {' '.join(cmd_perf)}")

    # Use PTY for isotpperf to force unbuffered output behavior
    master_fd, slave_fd = pty.openpty()

    perf_proc = subprocess.Popen(
        cmd_perf,
        stdout=slave_fd,
        stderr=subprocess.PIPE,
        text=True
    )
    os.close(slave_fd)  # Close slave in parent

    recv_proc = None

    try:
        time.sleep(0.5)
        if perf_proc.poll() is not None:
            _, err = perf_proc.communicate()
            pytest.fail(f"isotpperf failed to start. Stderr: {err}")

        # 2. Start isotprecv (The Receiver / Flow Control provider)
        # It needs reversed IDs relative to the sender to acknowledge frames.
        # Sender (isotpsend): -s 123 -d 321
        # Receiver (isotprecv): -s 321 -d 123
        cmd_recv = [isotprecv, "-s", perf_dst, "-d", perf_src, can_interface]
        print(f"DEBUG: Starting isotprecv: {' '.join(cmd_recv)}")

        # We don't strictly need isotprecv's output, but we need it running.
        # Pipe output to DEVNULL to avoid buffer blocking.
        recv_proc = subprocess.Popen(
            cmd_recv,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )

        time.sleep(0.5)

        # 3. Send data (25 bytes)
        # "00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE 11 11 11 11 11 11 11 11 11 11"
        payload_bytes = "00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE 11 11 11 11 11 11 11 11 11 11"
        cmd_send = [isotpsend, "-s", perf_src, "-d", perf_dst, can_interface]

        print(f"DEBUG: Sending data via isotpsend: {' '.join(cmd_send)}")
        subprocess.run(
            cmd_send,
            input=payload_bytes,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True
        )

        # Wait a bit for isotpperf to receive and print
        # Reading from PTY
        output = ""
        start_time = time.time()
        found = False
        expected_msg = "25 byte in"

        while time.time() - start_time < 3.0:
            r, _, _ = select.select([master_fd], [], [], 0.1)
            if master_fd in r:
                try:
                    chunk = os.read(master_fd, 1024).decode('utf-8', errors='replace')
                    if chunk:
                        output += chunk
                        # Check continuously
                        if expected_msg in output:
                            found = True
                            break
                except OSError:
                    break

            if perf_proc.poll() is not None:
                break

        print(f"DEBUG: isotpperf output (PTY):\n{output}")

        assert found, f"isotpperf did not report '{expected_msg}'. Captured: {output}"

    finally:
        # Cleanup
        if recv_proc:
            recv_proc.terminate()
            recv_proc.wait()

        if perf_proc.poll() is None:
            perf_proc.terminate()
            try:
                perf_proc.wait(timeout=1)
            except subprocess.TimeoutExpired:
                perf_proc.kill()

        if master_fd:
            os.close(master_fd)
