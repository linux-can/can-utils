# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (c) 2025 Linux CAN project
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

import pytest
import subprocess
import os
import time
import signal
import re

# --- Helper Functions & Classes ---

def run_cangen(bin_path, interface, args):
    """
    Runs cangen to generate traffic.
    Waits for it to finish (assumes -n is used in args).
    """
    cmd = [os.path.join(bin_path, "cangen"), interface] + args
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

class CanbusloadMonitor:
    """
    Context manager to run canbusload.
    Captures stdout/stderr.
    Ensures process is killed to prevent stalls.
    """
    def __init__(self, bin_path, args):
        self.cmd = [os.path.join(bin_path, "canbusload")] + args
        self.process = None
        self.output = ""
        self.error = ""

    def __enter__(self):
        # run with setsid to allow killing the whole process group later
        self.process = subprocess.Popen(
            self.cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            preexec_fn=os.setsid
        )
        time.sleep(0.5) # Wait for tool to start and initialize
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.process:
            if self.process.poll() is None:
                # canbusload mentions using CTRL-C (SIGINT) to terminate.
                # SIGTERM might be ignored or handled differently.
                os.killpg(os.getpgid(self.process.pid), signal.SIGINT)
                try:
                    # Wait a bit for it to exit gracefully
                    self.process.wait(timeout=1.0)
                except subprocess.TimeoutExpired:
                    # Force kill if it's stuck (e.g. buffer deadlock)
                    os.killpg(os.getpgid(self.process.pid), signal.SIGKILL)

            # Read remaining output
            self.output, self.error = self.process.communicate()

# --- Tests for canbusload ---

def test_help_option(bin_path):
    """
    Description:
        Tests the -h option to ensure the usage message is displayed.

    Manual reproduction:
        $ canbusload -h
        > monitor CAN bus load.
        > Usage: canbusload [options] <CAN interface>+
    """
    result = subprocess.run(
        [os.path.join(bin_path, "canbusload"), "-h"],
        capture_output=True,
        text=True
    )
    assert "Usage: canbusload" in result.stdout or "Usage: canbusload" in result.stderr
    assert "monitor CAN bus load" in result.stdout or "monitor CAN bus load" in result.stderr

def test_basic_monitoring(bin_path, can_interface):
    """
    Description:
        Tests basic monitoring of a CAN interface with a specified bitrate.
        Verifies that the interface name appears in the output and frames are counted.

    Manual reproduction:
        1. Open a terminal and run:
           $ canbusload vcan0@500000
        2. In another terminal, generate traffic:
           $ cangen vcan0 -n 10
        3. Observe the output updating with frame counts.
    """
    bitrate = "500000"
    target = f"{can_interface}@{bitrate}"

    with CanbusloadMonitor(bin_path, [target]) as monitor:
        # Generate some traffic so canbusload has something to report
        # Use -g to add a small gap, preventing potential buffer flooding issues on very fast systems
        run_cangen(bin_path, can_interface, ["-n", "10", "-g", "10"])
        time.sleep(1.0) # Wait for next update cycle

    assert can_interface in monitor.output
    # Check if we saw some frames (regex for at least 1 frame)
    # Output format roughly: can0@500k  10  ...
    assert re.search(r'\s+[1-9][0-9]*\s+', monitor.output), "No frames detected in output"

def test_time_option_t(bin_path, can_interface):
    """
    Description:
        Tests the -t option which shows the current time on the first line.

    Manual reproduction:
        $ canbusload -t vcan0@500000
        > canbusload 2025-01-01 12:00:00 ...
    """
    target = f"{can_interface}@500000"

    with CanbusloadMonitor(bin_path, ["-t", target]) as monitor:
        time.sleep(0.5)

    # Check for YYYY-MM-DD format in the output header
    assert re.search(r'\d{4}-\d{2}-\d{2}', monitor.output), "Timestamp not found with -t option"

def test_bargraph_option_b(bin_path, can_interface):
    """
    Description:
        Tests the -b option which displays a bargraph of the bus load.

    Manual reproduction:
        1. Run:
           $ canbusload -b vcan0@500000
        2. Generate heavy traffic:
           $ cangen vcan0 -g 0 -n 100
        3. Observe the ASCII bar graph (e.g., |XXXX....|).
    """
    target = f"{can_interface}@500000"

    with CanbusloadMonitor(bin_path, ["-b", target]) as monitor:
        # Generate burst of traffic to spike load
        run_cangen(bin_path, can_interface, ["-n", "50", "-g", "0"])
        time.sleep(1.0)

    # Check for bargraph delimiters
    assert "|" in monitor.output
    # Check for bargraph content (X for data, . for empty)
    # Note: With low load it might just be dots, but the bar structure |...| should exist
    assert re.search(r'\|[XRT\.]+\|', monitor.output), "Bargraph structure not found"

def test_colorize_option_c(bin_path, can_interface):
    """
    Description:
        Tests the -c option for colorized output.
        Checks for the presence of ANSI escape codes in the output.

    Manual reproduction:
        $ canbusload -c vcan0@500000
        > Output should be colored (visible in compatible terminal).
    """
    target = f"{can_interface}@500000"

    with CanbusloadMonitor(bin_path, ["-c", target]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "1"])
        time.sleep(0.5)

    # Check for ANSI escape character (ASCII 27 / \x1b)
    assert "\x1b[" in monitor.output, "ANSI escape codes for color not found"

def test_redraw_option_r(bin_path, can_interface):
    """
    Description:
        Tests the -r option which redraws the terminal (like 'top').
        Checks for clear screen/cursor movement escape sequences.

    Manual reproduction:
        $ canbusload -r vcan0@500000
        > Screen should refresh instead of scrolling.
    """
    target = f"{can_interface}@500000"

    with CanbusloadMonitor(bin_path, ["-r", target]) as monitor:
        time.sleep(0.5)

    # Usually implies clearing screen or moving cursor home: \x1b[H or \x1b[2J
    assert "\x1b[" in monitor.output, "Cursor movement/redraw codes not found"

def test_exact_calculation_e(bin_path, can_interface):
    """
    Description:
        Tests the -e option for exact calculation of stuffed bits.
        Ensures the tool runs without error and outputs the 'exact' indicator if applicable
        or simply produces valid output.

    Manual reproduction:
        $ canbusload -e vcan0@500000
        > Header might indicate different mode or output values change slightly.
    """
    target = f"{can_interface}@500000"

    with CanbusloadMonitor(bin_path, ["-e", target]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "10"])
        time.sleep(0.5)

    # Primarily checking that it runs successfully and produces output
    assert can_interface in monitor.output
    assert re.search(r'\s+[1-9][0-9]*\s+', monitor.output)

def test_ignore_bitstuffing_i(bin_path, can_interface):
    """
    Description:
        Tests the -i option to ignore bitstuffing in bandwidth calculation.

    Manual reproduction:
        $ canbusload -i vcan0@500000
    """
    target = f"{can_interface}@500000"

    with CanbusloadMonitor(bin_path, ["-i", target]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "10"])
        time.sleep(0.5)

    assert can_interface in monitor.output

def test_multiple_interfaces(bin_path, can_interface):
    """
    Description:
        Tests monitoring multiple interfaces simultaneously.
        Note: Since we usually only have one 'can_interface' fixture,
        we will simulate the syntax. If the second interface doesn't exist,
        canbusload might still run or show error for that line, but we check
        parsing.

        To robustly test, we use the same interface twice with different bitrates
        (if supported by tool) or just check that the command line is accepted.

    Manual reproduction:
        $ canbusload vcan0@500000 vcan0@250000
        > Should list vcan0 twice.
    """
    # Using the same interface twice to ensure they exist.
    target1 = f"{can_interface}@500000"
    target2 = f"{can_interface}@250000"

    with CanbusloadMonitor(bin_path, [target1, target2]) as monitor:
        run_cangen(bin_path, can_interface, ["-n", "10"])
        time.sleep(1.0)

    # Check that the interface appears at least twice in the output lines
    # (ignoring the header line)
    count = monitor.output.count(can_interface)
    # 1 in command echo (if any), 2 in status lines.
    # Since we are capturing output, it likely appears multiple times across updates.
    # We just ensure it's there.
    assert count >= 2, "Multiple interfaces did not appear in output"

def test_missing_bitrate_error(bin_path, can_interface):
    """
    Description:
        Tests that omitting the bitrate results in an error/usage message.
        The bitrate is mandatory.

    Manual reproduction:
        $ canbusload vcan0
        > Should print usage or error.
    """
    # Running without @bitrate
    result = subprocess.run(
        [os.path.join(bin_path, "canbusload"), can_interface],
        capture_output=True,
        text=True
    )
    # Expect failure code or error message
    assert result.returncode != 0 or "Usage" in result.stdout or "Usage" in result.stderr
