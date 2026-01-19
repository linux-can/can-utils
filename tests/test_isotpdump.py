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

def test_isotpdump_usage(bin_path):
    """
    Test usage/help output for isotpdump.

    Manual Reproduction:
    Run: ./isotpdump -h
    Expect: Output containing "Usage: isotpdump".
    """
    isotpdump = os.path.join(bin_path, "isotpdump")

    if not os.path.exists(isotpdump):
        pytest.skip("isotpdump binary not found")

    # Running with -h usually triggers error output about invalid option 'h'
    # or just prints usage.
    result = subprocess.run(
        [isotpdump, "-h"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    assert "Usage: isotpdump" in result.stderr or "Usage: isotpdump" in result.stdout

@pytest.mark.parametrize("payload_hex, desc", [
    ("11 22 33", "Single Frame"),
    ("00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE", "Multi Frame (Segmentation)")
])
def test_isotpdump_traffic(bin_path, can_interface, payload_hex, desc):
    """
    Test that isotpdump correctly captures and decodes ISO-TP traffic.

    Description:
    1. Start isotpdump in background using a PTY.
       It uses a raw CAN socket to monitor traffic and interprets ISO-TP headers.
    2. Start isotprecv (Background) to handle Flow Control (FC).
    3. Run isotpsend to generate traffic.
    4. Read dump output continuously.
    5. Stop dump with SIGINT.
    6. Verify dump output contains the payload bytes.

    Manual Reproduction:
    1. Terminal 1: ./isotpdump -s 123 -d 321 vcan0
    2. Terminal 2: ./isotprecv -s 321 -d 123 vcan0
    3. Terminal 3: echo "11 22 33" | ./isotpsend -s 123 -d 321 vcan0
    """
    isotpdump = os.path.join(bin_path, "isotpdump")
    isotpsend = os.path.join(bin_path, "isotpsend")
    isotprecv = os.path.join(bin_path, "isotprecv")

    for tool in [isotpdump, isotpsend, isotprecv]:
        if not os.path.exists(tool):
            pytest.skip(f"{tool} not found")

    if not check_isotp_support(bin_path, can_interface):
        pytest.skip("ISO-TP kernel support missing")

    SRC_ID = "123"
    DST_ID = "321"

    print(f"\nDEBUG: Starting test '{desc}'")

    # 1. Start Dump with PTY
    master_fd, slave_fd = pty.openpty()

    # -s SourceID, -d DestID
    dump_cmd = [isotpdump, "-s", SRC_ID, "-d", DST_ID, can_interface]
    print(f"DEBUG: Starting dump: {' '.join(dump_cmd)}")

    dump_proc = subprocess.Popen(
        dump_cmd,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        close_fds=True
    )
    os.close(slave_fd)

    output_bytes = b""

    try:
        time.sleep(1.0) # Wait for startup

        if dump_proc.poll() is not None:
            pytest.fail(f"isotpdump process died during startup. RC={dump_proc.returncode}")

        # 2. Start Receiver (isotprecv)
        # Required for Multi-Frame Flow Control
        recv_cmd = [isotprecv, "-s", DST_ID, "-d", SRC_ID, can_interface]
        recv_proc = subprocess.Popen(
            recv_cmd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True
        )

        try:
            time.sleep(1.0)

            if recv_proc.poll() is not None:
                _, err = recv_proc.communicate()
                pytest.fail(f"isotprecv failed to start. RC={recv_proc.returncode}. Stderr: {err}")

            # 3. Send Data (isotpsend)
            send_cmd = [isotpsend, "-s", SRC_ID, "-d", DST_ID, can_interface]
            print(f"DEBUG: Sending data: {' '.join(send_cmd)} with payload '{payload_hex}'")

            subprocess.run(
                send_cmd,
                input=payload_hex,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=True,
                timeout=2
            )

            # 4. Capture dump output
            start_wait = time.time()
            # isotpdump output format is raw-ish but includes data bytes.
            # Example: "vcan0  123  [3]  11 22 33"
            expected_parts = payload_hex.split()
            print("DEBUG: Waiting for data in dump output...")

            while time.time() - start_wait < 4:
                r, _, _ = select.select([master_fd], [], [], 0.1)
                if master_fd in r:
                    try:
                        chunk = os.read(master_fd, 4096)
                        if chunk:
                            output_bytes += chunk
                    except OSError:
                        pass

                current_out = output_bytes.decode('utf-8', errors='replace')
                # Check if we have the data
                if all(part in current_out for part in expected_parts):
                    print("DEBUG: Found all expected parts in output.")
                    break

                if dump_proc.poll() is not None:
                    break

        finally:
            if recv_proc.poll() is None:
                recv_proc.terminate()
                recv_proc.wait()

    finally:
        if dump_proc.poll() is None:
            print("DEBUG: Sending SIGINT to dump.")
            dump_proc.send_signal(signal.SIGINT)
            try:
                end_time = time.time() + 1
                while time.time() < end_time:
                    r, _, _ = select.select([master_fd], [], [], 0.1)
                    if master_fd in r:
                        try:
                            chunk = os.read(master_fd, 4096)
                        except OSError:
                            break
                    if dump_proc.poll() is not None:
                        break
            finally:
                if dump_proc.poll() is None:
                    dump_proc.kill()
                dump_proc.wait()

        os.close(master_fd)

    output = output_bytes.decode('utf-8', errors='replace')
    print(f"--- Dump Output ({desc}) ---\n{output}")

    expected_parts = payload_hex.split()
    missing_bytes = [b for b in expected_parts if b not in output]

    assert not missing_bytes, f"Missing bytes in dump output: {missing_bytes}. Output:\n{output}"
