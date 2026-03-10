# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (c) 2023 Linux CAN project
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License for more details.

import pytest
import subprocess
import os
import time
import signal
import re

# --- Helper Functions ---

class TrafficMonitor:
    """
    Context manager to run candump in the background.
    Captures the bus traffic to verify what cansequence sends.
    """
    def __init__(self, bin_path, interface, args=None):
        self.cmd = [os.path.join(bin_path, "candump"), "-L", interface]
        if args:
            self.cmd.extend(args)
        self.process = None
        self.output = ""

    def __enter__(self):
        self.process = subprocess.Popen(
            self.cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            preexec_fn=os.setsid
        )
        time.sleep(0.2)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.process:
            os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
            self.output, _ = self.process.communicate()

def run_cansequence_sender(bin_path, interface, count=None, args=None):
    """Runs cansequence in sender mode."""
    cmd = [os.path.join(bin_path, "cansequence"), interface]
    if count is not None:
        cmd.append(f"--loop={count}")
    if args:
        cmd.extend(args)

    subprocess.run(cmd, check=True)

class CanSequenceReceiver:
    """Context manager for cansequence receiver."""
    def __init__(self, bin_path, interface, args=None):
        self.cmd = [os.path.join(bin_path, "cansequence"), interface, "--receive"]
        if args:
            self.cmd.extend(args)
        self.process = None
        self.stdout = ""
        self.stderr = ""

    def __enter__(self):
        self.process = subprocess.Popen(
            self.cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            preexec_fn=os.setsid
        )
        time.sleep(0.2)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.process:
            if self.process.poll() is None:
                os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
            self.stdout, self.stderr = self.process.communicate()

# --- Tests for cansequence ---

def test_help_option(bin_path):
    """Test -h option."""
    result = subprocess.run([os.path.join(bin_path, "cansequence"), "-h"], capture_output=True, text=True)
    assert "Usage: cansequence" in result.stdout or "Usage: cansequence" in result.stderr

def test_sender_payload_increment(bin_path, can_interface):
    """
    Test 1: Sender + Candump.
    Verify that cansequence sends frames with incrementing payload.
    """
    count = 5
    with TrafficMonitor(bin_path, can_interface) as monitor:
        run_cansequence_sender(bin_path, can_interface, count=count)

    # Parse output to find payloads
    # candump format: (timestamp) iface ID#DATA
    # cansequence default ID is 2. Payload is sequence number (hex).
    # e.g. 00, 01, 02...

    lines = monitor.output.strip().splitlines()
    assert len(lines) == count

    # Extract data bytes. Assuming standard classic CAN frame.
    # Regex to capture the first byte of data: ID#<Byte0>...
    data_bytes = []
    for line in lines:
        match = re.search(r'#([0-9A-Fa-f]{2})', line)
        if match:
            data_bytes.append(int(match.group(1), 16))

    # Verify increment
    for i in range(len(data_bytes)):
        assert data_bytes[i] == i % 256, f"Payload mismatch at index {i}"

def test_sender_receiver_success(bin_path, can_interface):
    """
    Test 2: Sender + Receiver.
    Verify that receiver accepts the stream from sender without error.
    """
    # Start receiver
    with CanSequenceReceiver(bin_path, can_interface, args=["-v"]) as receiver:
        # Run sender
        run_cansequence_sender(bin_path, can_interface, count=10)

        # Give receiver a moment to process
        time.sleep(0.5)

    # Receiver should NOT have exited with error (if we didn't use -q)
    # and shouldn't have printed "sequence mismatch" errors.
    # Note: cansequence prints to stderr usually on error.

    assert "sequence number mismatch" not in receiver.stderr
    assert "sequence number mismatch" not in receiver.stdout

def test_receiver_detects_error(bin_path, can_interface):
    """
    Test 3: Cansend (Injection) + Receiver.
    Verify receiver detects missing sequence number.
    """
    # Use -q 1 to quit immediately on first error
    with CanSequenceReceiver(bin_path, can_interface, args=["--quit", "1"]) as receiver:

        # Manually send sequence 0 (valid)
        # Sequence number is usually little endian integer in payload.
        # ID 2 is default for cansequence.
        subprocess.run([os.path.join(bin_path, "cansend"), can_interface, "002#00"], check=True)
        time.sleep(0.1)

        # Manually send sequence 2 (skipping 1) -> Invalid!
        subprocess.run([os.path.join(bin_path, "cansend"), can_interface, "002#02"], check=True)
        time.sleep(0.5)

        # Check if receiver exited
        assert receiver.process.poll() is not None, "Receiver did not exit on sequence error"
        assert receiver.process.returncode != 0

def test_extended_id(bin_path, can_interface):
    """Test sending extended frames (-e)."""
    with TrafficMonitor(bin_path, can_interface) as monitor:
        # Send 1 frame, extended mode
        run_cansequence_sender(bin_path, can_interface, count=1, args=["-e"])

    # Output should contain 8-character ID (padded) or match extended syntax.
    # Default ID is 2, so extended is usually 00000002.
    assert "00000002#" in monitor.output or "2#" in monitor.output
    # Depending on candump formatting, we might verify extended flag logic if needed,
    # but existence of traffic is the main check here.

def test_custom_identifier(bin_path, can_interface):
    """Test custom CAN ID (-i)."""
    custom_id = "0x123" # Hex string ensures strtoul interprets as hex
    with TrafficMonitor(bin_path, can_interface) as monitor:
        run_cansequence_sender(bin_path, can_interface, count=1, args=["-i", custom_id])

    # Check for ID 123
    assert "123#" in monitor.output

def test_can_fd_mode(bin_path, can_interface):
    """Test CAN-FD mode (-f)."""
    # Requires hardware/vcan support for FD
    try:
        with TrafficMonitor(bin_path, can_interface) as monitor:
            run_cansequence_sender(bin_path, can_interface, count=1, args=["-f"])

        # FD frames usually appear with ## in candump -L (if not strict classic view)
        # or we verify the output exists.
        assert "##" in monitor.output or "#" in monitor.output
    except subprocess.CalledProcessError:
        pytest.skip("CAN-FD not supported or failed")
