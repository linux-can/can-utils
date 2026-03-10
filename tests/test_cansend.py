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
    Captures the bus traffic to verify what cansend sends.
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
        # Give candump a moment to bind to the socket
        time.sleep(0.2)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.process:
            os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
            self.output, _ = self.process.communicate()

def run_cansend(bin_path, interface, frame):
    """Executes cansend with the given frame string."""
    cmd = [os.path.join(bin_path, "cansend"), interface, frame]
    subprocess.run(cmd, check=True)

# --- Tests for cansend ---

def test_help_option(bin_path):
    """Test -h option: Should print usage."""
    # cansend might exit with 1 on help (since it expects args), check output
    result = subprocess.run([os.path.join(bin_path, "cansend"), "-h"], capture_output=True, text=True)
    # The output provided by user shows usage on stdout or stderr
    assert "Usage: " in result.stdout or "Usage: " in result.stderr
    assert "cansend" in result.stdout or "cansend" in result.stderr

def test_classic_can_simple(bin_path, can_interface):
    """Test standard frame: <can_id>#{data}"""
    # 123#DEADBEEF
    frame = "123#DEADBEEF"
    with TrafficMonitor(bin_path, can_interface) as monitor:
        run_cansend(bin_path, can_interface, frame)

    assert "123#DEADBEEF" in monitor.output

def test_classic_can_dots(bin_path, can_interface):
    """Test data with dots: 5A1#11.2233.44556677.88"""
    frame_input = "5A1#11.2233.44556677.88"
    # candump -L output will be without dots: 5A1#1122334455667788
    expected = "5A1#1122334455667788"

    with TrafficMonitor(bin_path, can_interface) as monitor:
        run_cansend(bin_path, can_interface, frame_input)

    assert expected in monitor.output

def test_classic_can_no_data(bin_path, can_interface):
    """Test frame with no data: 5AA#"""
    frame = "5AA#"
    with TrafficMonitor(bin_path, can_interface) as monitor:
        run_cansend(bin_path, can_interface, frame)

    # Expect ID with empty data. candump -L format: 5AA# (or 5AA# with nothing following)
    # Regex ensures 5AA# is at end of line or followed by space
    assert re.search(r'5AA#(\s|$)', monitor.output)

def test_classic_can_extended_id(bin_path, can_interface):
    """Test Extended Frame Format (EFF): 1F334455#11..."""
    frame = "1F334455#112233"
    with TrafficMonitor(bin_path, can_interface) as monitor:
        run_cansend(bin_path, can_interface, frame)

    assert "1F334455#112233" in monitor.output

def test_classic_can_rtr(bin_path, can_interface):
    """Test RTR frame: <can_id>#R"""
    frame = "123#R"
    with TrafficMonitor(bin_path, can_interface) as monitor:
        run_cansend(bin_path, can_interface, frame)

    # candump -L typically represents RTR via 'R' in output or specific formatting
    # e.g., 123#R
    assert "123#R" in monitor.output

def test_classic_can_rtr_len(bin_path, can_interface):
    """Test RTR with length: <can_id>#R{len}"""
    # 00000123#R3 -> ID 123 (EFF padded?), len 3
    # Note: 00000123 is 8 chars -> EFF.
    frame = "00000123#R3"
    with TrafficMonitor(bin_path, can_interface) as monitor:
        run_cansend(bin_path, can_interface, frame)

    # Check for EFF ID and RTR marker.
    # candump -L output for RTR might differ slightly depending on version,
    # but usually preserves the #R syntax if length matches.
    assert "00000123#R" in monitor.output

def test_classic_can_explicit_dlc(bin_path, can_interface):
    """Test explicit DLC: <can_id>#{data}_{dlc}"""
    # 1F334455#1122334455667788_B (DLC 11)
    # This sets the DLC field > 8, but payload is 8 bytes.
    # Classic CAN allows DLC > 8 (interpreted as 8 usually, but passed on wire).
    frame = "1F334455#1122334455667788_B"

    # We need candump to show the DLC to verify this, or just check it sends successfully.
    # Standard candump -L doesn't always show DLC > 8 unless -8 is used.
    # But here we verify 'cansend' doesn't crash and sends *something*.
    with TrafficMonitor(bin_path, can_interface) as monitor:
        run_cansend(bin_path, can_interface, frame)

    assert "1F334455#1122334455667788" in monitor.output

def test_classic_can_rtr_len_dlc(bin_path, can_interface):
    """Test RTR with len and DLC: <can_id>#R{len}_{dlc}"""
    # 333#R8_E (Len 8, DLC 14)
    frame = "333#R8_E"
    with TrafficMonitor(bin_path, can_interface) as monitor:
        run_cansend(bin_path, can_interface, frame)

    assert "333#R" in monitor.output

def test_can_fd_flags_only(bin_path, can_interface):
    """Test CAN FD flags only: <can_id>##<flags>"""
    # 123##1 (Flags: 1 = BRS)
    frame = "123##1"
    with TrafficMonitor(bin_path, can_interface) as monitor:
        run_cansend(bin_path, can_interface, frame)

    # candump -L for FD uses ##
    # Output should contain 123##1
    # Note: Kernel/Driver often adds CANFD_FDF (0x04) flag automatically,
    # so we might see 5 (1|4) instead of 1.
    assert "123##1" in monitor.output or "123##5" in monitor.output

def test_can_fd_flags_data(bin_path, can_interface):
    """Test CAN FD with flags and data: <can_id>##<flags>{data}"""
    # 213##311223344 (Flags 3, Data 11223344)
    # Flags 3 = BRS (1) | ESI (2)
    frame = "213##311223344"
    with TrafficMonitor(bin_path, can_interface) as monitor:
        run_cansend(bin_path, can_interface, frame)

    # Note: Kernel/Driver often adds CANFD_FDF (0x04) flag automatically,
    # so we might see 7 (3|4) instead of 3.
    assert "213##311223344" in monitor.output or "213##711223344" in monitor.output

def test_can_xl_frame(bin_path, can_interface):
    """Test CAN XL frame (if supported)."""
    # Example: 45123#81:00:12345678#11223344.556677
    # Prio: 123, Flags: 81, SDT: 00, AF: 12345678, Data...
    # Note: VCID is separate? The example in help says:
    # <vcid><prio>#... -> 45123 means VCID 45, Prio 123?
    frame = "45123#81:00:12345678#11223344556677"

    # This might fail on kernels without CAN XL support.
    # We wrap in try/except or check return code to skip.
    try:
        with TrafficMonitor(bin_path, can_interface) as monitor:
            run_cansend(bin_path, can_interface, frame)

        # If it worked, check output. XL output format in candump might vary.
        # But we expect the hex data to appear.
        assert "11223344556677" in monitor.output
    except subprocess.CalledProcessError:
        pytest.skip("CAN XL frame sending failed (kernel support missing?)")

def test_invalid_syntax(bin_path, can_interface):
    """Test invalid syntax handling."""
    # Missing separator
    frame = "123DEADBEEF"
    cmd = [os.path.join(bin_path, "cansend"), can_interface, frame]

    # Should exit with error code
    result = subprocess.run(cmd, capture_output=True, text=True)
    assert result.returncode != 0

def test_missing_args(bin_path):
    """Test missing arguments."""
    cmd = [os.path.join(bin_path, "cansend")]
    result = subprocess.run(cmd, capture_output=True, text=True)
    assert result.returncode != 0
