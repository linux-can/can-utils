# SPDX-License-Identifier: GPL-2.0-only

import subprocess
import time
import os
import pytest
import tempfile
import signal
import pty
import sys
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

def test_isotpsniffer_usage(bin_path):
    """
    Test usage/help output for isotpsniffer.

    Manual Reproduction:
    Run: ./isotpsniffer -h
    Expect: Output containing "Usage: isotpsniffer".
    """
    isotpsniffer = os.path.join(bin_path, "isotpsniffer")

    if not os.path.exists(isotpsniffer):
        pytest.skip("isotpsniffer binary not found")

    # Running with -h usually triggers error output about missing argument for option 'h'
    # or just prints usage.
    result = subprocess.run(
        [isotpsniffer, "-h"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    assert "Usage: isotpsniffer" in result.stderr or "Usage: isotpsniffer" in result.stdout

@pytest.mark.parametrize("payload_hex, desc", [
    ("11 22 33", "Single Frame"),
    ("00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE", "Multi Frame (Segmentation)")
])
def test_isotpsniffer_traffic(bin_path, can_interface, payload_hex, desc):
    """
    Test that isotpsniffer correctly captures ISO-TP traffic.

    Description:
    1. Start isotpsniffer in background using a PTY (pseudo-terminal).
       It sniffs raw CAN traffic and decodes ISO-TP.
    2. Start isotprecv (Background).
       It acts as the ISO-TP destination node, handling Flow Control (FC).
       This matches the manual reproduction where isotprecv is running.
    3. Run isotpsend to generate traffic.
    4. Read sniffer output continuously.
    5. Stop sniffer with SIGINT.
    6. Verify sniffer output contains the payload bytes.

    Manual Reproduction (Single Frame):
    1. Terminal 1: ./isotpsniffer -s 123 -d 321 vcan0
    2. Terminal 2: ./isotprecv -s 321 -d 123 vcan0
    3. Terminal 3: echo "11 22 33" | ./isotpsend -s 123 -d 321 vcan0
    4. Observe Terminal 1 output.

    Manual Reproduction (Multi Frame):
    1. Terminal 1: ./isotpsniffer -s 123 -d 321 vcan0
    2. Terminal 2: ./isotprecv -s 321 -d 123 vcan0
    3. Terminal 3: echo "00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE" | ./isotpsend -s 123 -d 321 vcan0
    4. Observe Terminal 1 output.
    """
    isotpsniffer = os.path.join(bin_path, "isotpsniffer")
    isotpsend = os.path.join(bin_path, "isotpsend")
    isotprecv = os.path.join(bin_path, "isotprecv")

    for tool in [isotpsniffer, isotpsend, isotprecv]:
        if not os.path.exists(tool):
            pytest.skip(f"{tool} not found")

    if not check_isotp_support(bin_path, can_interface):
        pytest.skip("ISO-TP kernel support missing")

    SRC_ID = "123"
    DST_ID = "321"

    print(f"\nDEBUG: Starting test '{desc}'")

    # 1. Start Sniffer with PTY
    # We use PTY to force line buffering and ensure captured output
    master_fd, slave_fd = pty.openpty()

    sniff_cmd = [isotpsniffer, "-s", SRC_ID, "-d", DST_ID, can_interface]
    print(f"DEBUG: Starting sniffer: {' '.join(sniff_cmd)}")
    sniff_proc = subprocess.Popen(
        sniff_cmd,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        close_fds=True
    )
    os.close(slave_fd)

    output_bytes = b""

    try:
        time.sleep(1.0)

        if sniff_proc.poll() is not None:
            pytest.fail(f"isotpsniffer process died during startup. RC={sniff_proc.returncode}")

        # 2. Start Receiver (isotprecv)
        recv_cmd = [isotprecv, "-s", DST_ID, "-d", SRC_ID, can_interface]
        print(f"DEBUG: Starting receiver: {' '.join(recv_cmd)}")
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

            # 4. Capture sniffer output
            start_wait = time.time()
            expected_parts = payload_hex.split()
            print("DEBUG: Waiting for data in sniffer output...")

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
                if all(part in current_out for part in expected_parts):
                    print("DEBUG: Found all expected parts in output.")
                    break

                if sniff_proc.poll() is not None:
                    break

        finally:
            if recv_proc.poll() is None:
                recv_proc.terminate()
                recv_proc.wait()

    finally:
        if sniff_proc.poll() is None:
            print("DEBUG: Sending SIGINT to sniffer.")
            sniff_proc.send_signal(signal.SIGINT)
            try:
                end_time = time.time() + 1
                while time.time() < end_time:
                    r, _, _ = select.select([master_fd], [], [], 0.1)
                    if master_fd in r:
                        try:
                            chunk = os.read(master_fd, 4096)
                        except OSError:
                            break
                    if sniff_proc.poll() is not None:
                        break
            finally:
                if sniff_proc.poll() is None:
                    sniff_proc.kill()
                sniff_proc.wait()

        os.close(master_fd)

    output = output_bytes.decode('utf-8', errors='replace')
    print(f"--- Sniffer Output ({desc}) ---\n{output}")

    expected_parts = payload_hex.split()
    missing_bytes = [b for b in expected_parts if b not in output]

    assert not missing_bytes, f"Missing bytes in sniffer output: {missing_bytes}. Output:\n{output}"

def test_isotp_protocol_compliance(bin_path, can_interface):
    """
    Verify raw ISO-TP protocol behavior on the bus using candump.
    This ensures that segmentation and flow control are actually happening.

    Scenario: Multi-Frame Transfer (>7 bytes)

    Manual Reproduction:
    1. Terminal 1: candump vcan0
    2. Terminal 2: ./isotprecv -s 321 -d 123 vcan0
    3. Terminal 3: echo "00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE" | ./isotpsend -s 123 -d 321 vcan0

    Expect in Terminal 1 (Raw Frames):
    - First Frame (FF) from Sender: ID=123, PCI=0x1...
    - Flow Control (FC) from Receiver: ID=321, PCI=0x3...
    - Consecutive Frames (CF) from Sender: ID=123, PCI=0x2...
    """
    isotpsend = os.path.join(bin_path, "isotpsend")
    isotprecv = os.path.join(bin_path, "isotprecv")
    candump = os.path.join(bin_path, "candump")

    for tool in [isotpsend, isotprecv, candump]:
        if not os.path.exists(tool):
            pytest.skip(f"{tool} not found")

    if not check_isotp_support(bin_path, can_interface):
        pytest.skip("ISO-TP kernel support missing")

    SRC_ID = "123" # 0x07B
    DST_ID = "321" # 0x141
    # 14 bytes payload -> requires segmentation (FF + CF)
    payload_hex = "00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE"

    # 1. Start candump (Raw Monitor)
    # We use a temp file to avoid pipe buffering issues
    with tempfile.TemporaryFile(mode='w+') as dump_out:
        dump_proc = subprocess.Popen(
            [candump, can_interface],
            stdout=dump_out,
            stderr=subprocess.PIPE,
            text=True
        )

        # 2. Start Receiver
        recv_proc = subprocess.Popen(
            [isotprecv, "-s", DST_ID, "-d", SRC_ID, can_interface],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE
        )

        try:
            time.sleep(1.0) # Wait for startup

            # 3. Start Sender
            subprocess.run(
                [isotpsend, "-s", SRC_ID, "-d", DST_ID, can_interface],
                input=payload_hex,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=True,
                timeout=2
            )

            time.sleep(1.0) # Allow transmission to complete

        finally:
            recv_proc.terminate()
            recv_proc.wait()
            dump_proc.terminate()
            dump_proc.wait()

        # 4. Analyze Raw Dump
        dump_out.seek(0)
        raw_traffic = dump_out.read()
        print(f"--- Raw CAN Traffic ---\n{raw_traffic}")

        # Check for ISO-TP Protocol Elements
        # Note: candump format is usually "interface  ID   [len]  data..."

        # 1. Check for First Frame (FF) sent by SRC (123)
        # FF PCI starts with nibble 1 (e.g., 10 0E ...)
        # We look for ID 123 and data starting with 1
        ff_found = False

        # 2. Check for Flow Control (FC) sent by DST (321)
        # FC PCI starts with nibble 3 (e.g., 30 00 00)
        fc_found = False

        # 3. Check for Consecutive Frame (CF) sent by SRC (123)
        # CF PCI starts with nibble 2 (e.g., 21 ...)
        cf_found = False

        for line in raw_traffic.splitlines():
            if SRC_ID in line:
                # Check data bytes for Sender
                # Typical line: "vcan0  123   [8]  10 0E 00 11 22 33 44 55"
                parts = line.split()
                if "[" in parts[2]: # Format with [len]
                    data_idx = 3
                else: # Format without [len]
                    data_idx = 2

                if len(parts) > data_idx:
                    first_byte = int(parts[data_idx], 16)
                    if (first_byte & 0xF0) == 0x10:
                        ff_found = True
                    elif (first_byte & 0xF0) == 0x20:
                        cf_found = True

            elif DST_ID in line:
                # Check data bytes for Receiver (FC)
                parts = line.split()
                if "[" in parts[2]:
                    data_idx = 3
                else:
                    data_idx = 2

                if len(parts) > data_idx:
                    first_byte = int(parts[data_idx], 16)
                    if (first_byte & 0xF0) == 0x30:
                        fc_found = True

        assert ff_found, "Missing ISO-TP First Frame (FF - 0x1...) in raw traffic"
        assert fc_found, "Missing ISO-TP Flow Control (FC - 0x3...) in raw traffic. Kernel stack might not be responding."
        assert cf_found, "Missing ISO-TP Consecutive Frame (CF - 0x2...) in raw traffic"
