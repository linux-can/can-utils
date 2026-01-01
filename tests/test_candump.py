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
import shutil

# --- Helper Functions ---

def run_cangen(bin_path, interface, args):
    """
    Runs cangen to generate traffic.
    Waits for it to finish (assumes -n is used in args).
    """
    cmd = [os.path.join(bin_path, "cangen"), interface] + args
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

class CandumpMonitor:
    """
    Context manager to run candump.
    Captures stdout/stderr.
    """
    def __init__(self, bin_path, interface, args=None):
        self.cmd = [os.path.join(bin_path, "candump"), interface]
        if args:
            # Insert args before interface if they are options
            # candump usage: candump [options] <interface>
            self.cmd = [os.path.join(bin_path, "candump")] + args + [interface]
        else:
            self.cmd = [os.path.join(bin_path, "candump"), interface]

        self.process = None
        self.output = ""
        self.error = ""

    def __enter__(self):
        self.process = subprocess.Popen(
            self.cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            preexec_fn=os.setsid
        )
        time.sleep(0.1) # Wait for start
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.process:
            if self.process.poll() is None:
                os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
            self.output, self.error = self.process.communicate()

# --- Tests for candump ---

def test_help_option(bin_path):
    """Test -h option: Should print usage."""
    # candump often returns 1 or 0 on help, we check output mainly
    result = subprocess.run([os.path.join(bin_path, "candump"), "-h"], capture_output=True, text=True)
    assert "Usage: candump" in result.stdout or "Usage: candump" in result.stderr

def test_count_n(bin_path, can_interface):
    """Test -n <count>: Terminate after reception of <count> CAN frames."""
    count = 3
    # Start candump expecting 3 frames
    with CandumpMonitor(bin_path, can_interface, ["-n", str(count)]) as monitor:
        # Generate 3 frames
        run_cangen(bin_path, can_interface, ["-n", str(count)])

        # Wait a bit to ensure candump finishes and exits on its own
        time.sleep(0.5)

        # Check if process exited
        assert monitor.process.poll() is not None, "candump did not exit after receiving count frames"

    # Count lines (ignoring empty lines)
    lines = [l for l in monitor.output.splitlines() if l.strip()]
    assert len(lines) == count

def test_timeout_T(bin_path, can_interface):
    """Test -T <msecs>: Terminate after timeout if no frames received."""
    start = time.time()
    # Run with 500ms timeout
    subprocess.run(
        [os.path.join(bin_path, "candump"), can_interface, "-T", "500"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )
    duration = time.time() - start
    # Should be around 0.5s, definitely less than 2s (safety margin) and more than 0.4s
    assert 0.4 <= duration <= 1.0

def test_timestamp_formats(bin_path, can_interface):
    """Test -t <type>: timestamp formats."""

    # (a)bsolute
    with CandumpMonitor(bin_path, can_interface, ["-t", "a"]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "1"])
    assert re.search(r'\(\d+\.\d+\)', monitor.output), "Absolute timestamp missing"

    # (d)elta
    with CandumpMonitor(bin_path, can_interface, ["-t", "d"]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "2", "-g", "10"])
    # Second line should have a small delta
    assert re.search(r'\(\d+\.\d+\)', monitor.output), "Delta timestamp missing"

    # (z)ero
    with CandumpMonitor(bin_path, can_interface, ["-t", "z"]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "1"])
    # Starts with (0.000000) roughly. Note: candump often uses 3 digits for seconds (e.g. 000.000000).
    # Regex adjusted to accept one or more leading zeros.
    assert re.search(r'\(0+\.\d+\)', monitor.output), f"Zero timestamp format mismatch: {monitor.output}"

    # (A)bsolute w date
    with CandumpMonitor(bin_path, can_interface, ["-t", "A"]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "1"])
    # Check for YYYY-MM-DD format
    assert re.search(r'\(\d{4}-\d{2}-\d{2}', monitor.output), "Date timestamp missing"

def test_ascii_output_a(bin_path, can_interface):
    """Test -a: Additional ASCII output."""
    # Send known data that is printable ASCII
    # 0x41 = 'A', 0x42 = 'B'
    with CandumpMonitor(bin_path, can_interface, ["-a"]) as monitor:
        # Force DLC to 4 to ensure we get all 4 bytes ("ABCD")
        run_cangen(bin_path, can_interface, ["-n", "1", "-D", "41424344", "-L", "4"])

    assert "ABCD" in monitor.output or " .ABCD" in monitor.output

def test_silent_mode_s(bin_path, can_interface):
    """Test -s <level>: Silent mode."""

    # Level 0 (Default, not silent)
    with CandumpMonitor(bin_path, can_interface, ["-s", "0"]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "1"])
    assert len(monitor.output) > 0

    # Level 2 (Silent)
    with CandumpMonitor(bin_path, can_interface, ["-s", "2"]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "1"])
    assert len(monitor.output.strip()) == 0

def test_log_file_l(bin_path, can_interface, tmp_path):
    """Test -l: Log to file (default name)."""
    # Resolve bin_path to absolute because we change CWD below
    bin_path = os.path.abspath(bin_path)

    # Change cwd to tmp_path to avoid littering
    cwd = os.getcwd()
    os.chdir(tmp_path)
    try:
        # -l implies -s 2 (silent stdout)
        with CandumpMonitor(bin_path, can_interface, ["-l"]) as monitor:
            run_cangen(bin_path, can_interface, ["-n", "1"])

        # Check if file starting with candump- exists
        files = [f for f in os.listdir('.') if f.startswith('candump-')]
        assert len(files) > 0, "Log file not created"

        # Check content
        with open(files[0], 'r') as f:
            content = f.read()
            assert can_interface in content
    finally:
        os.chdir(cwd)

def test_specific_log_file_f(bin_path, can_interface, tmp_path):
    """Test -f <fname>: Log to specific file."""
    logfile = os.path.join(tmp_path, "test.log")

    with CandumpMonitor(bin_path, can_interface, ["-f", logfile]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "1"])

    assert os.path.exists(logfile)
    with open(logfile, 'r') as f:
        content = f.read()
        assert can_interface in content

def test_log_format_stdout_L(bin_path, can_interface):
    """Test -L: Log file format on stdout."""
    with CandumpMonitor(bin_path, can_interface, ["-L"]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "1"])

    # Format: (timestamp) interface ID#DATA
    assert re.search(r'\(\d+\.\d+\)\s+' + can_interface + r'\s+[0-9A-F]+#', monitor.output)

def test_raw_dlc_8(bin_path, can_interface):
    """Test -8: Display raw DLC in {} for Classical CAN."""
    # Send a classic CAN frame
    with CandumpMonitor(bin_path, can_interface, ["-8"]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "1", "-L", "4"])

    # Look for [4] or {4} depending on output format with -8
    # Standard candump: "vcan0  123   [4]  11 22 33 44"
    # With -8: "vcan0  123  {4} [4]  11 22 33 44" (Wait, man page says "display raw DLC in {}")
    # Let's check for the curly braces
    assert "{" in monitor.output and "}" in monitor.output

def test_extra_info_x(bin_path, can_interface):
    """Test -x: Print extra message info (RX/TX, etc.)."""
    with CandumpMonitor(bin_path, can_interface, ["-x"]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "1"])

    # Usually adds "RX - - " or similar at the end
    assert "RX" in monitor.output or "TX" in monitor.output

def test_filters(bin_path, can_interface):
    """Test CAN filters on command line."""

    # Filter for ID 123
    # Command: candump vcan0,123:7FF

    # 1. Send ID 123 (Should receive)
    cmd_match = [can_interface + ",123:7FF"] # Argument is "interface,filter"
    with CandumpMonitor(bin_path, cmd_match[0], args=[]) as monitor:
        # Note: CandumpMonitor logic needs slightly adjustment if interface arg contains comma
        # But here we pass it as the "interface" argument to the class which works for valid invocation
        run_cangen(bin_path, can_interface, ["-n", "1", "-I", "123"])
    assert "123" in monitor.output

    # 2. Send ID 456 (Should NOT receive)
    with CandumpMonitor(bin_path, cmd_match[0], args=[]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "1", "-I", "456"])
    assert "456" not in monitor.output

def test_inverse_filter(bin_path, can_interface):
    """Test inverse filter (~)."""
    # Filter: Everything EXCEPT ID 123
    # Command: candump vcan0,123~7FF

    arg = can_interface + ",123~7FF"

    # Send 123 (Should NOT receive)
    with CandumpMonitor(bin_path, arg) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "1", "-I", "123"])
    assert "123" not in monitor.output

    # Send 456 (Should receive)
    with CandumpMonitor(bin_path, arg) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "1", "-I", "456"])
    assert "456" in monitor.output

def test_swap_byte_order_S(bin_path, can_interface):
    """Test -S: Swap byte order."""
    # Send 11223344
    # -S should display it swapped/marked.
    # Warning: -S only affects printed output, usually prints big-endian vs little-endian representation

    with CandumpMonitor(bin_path, can_interface, ["-S"]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "1", "-D", "11223344", "-L", "4"])

    # The help says "marked with '`'".
    assert "`" in monitor.output
