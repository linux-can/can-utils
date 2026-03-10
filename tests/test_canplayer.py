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
    Captures the bus traffic to verify what canplayer sends.
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

def create_logfile(path, content):
    """Creates a temporary CAN log file."""
    with open(path, 'w') as f:
        f.write(content)
    return path

# --- Tests for canplayer ---

def test_help_option(bin_path):
    """Test -h option: Should print usage."""
    # canplayer might exit with 0 or 1, check output primarily
    result = subprocess.run([os.path.join(bin_path, "canplayer"), "-h"], capture_output=True, text=True)
    assert "Usage: canplayer" in result.stdout or "Usage: canplayer" in result.stderr

def test_replay_file(bin_path, can_interface, tmp_path):
    """Test -I: Replay a simple log file."""
    log_content = f"(1600000000.000000) {can_interface} 123#112233\n"
    logfile = create_logfile(os.path.join(tmp_path, "test.log"), log_content)

    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "canplayer"), "-I", logfile],
            check=True
        )

    assert "123#112233" in monitor.output

def test_stdin_input(bin_path, can_interface):
    """Test input via stdin (default behavior without -I)."""
    log_content = f"(1600000000.000000) {can_interface} 123#AABBCC\n"

    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "canplayer")],
            input=log_content,
            text=True,
            check=True
        )

    assert "123#AABBCC" in monitor.output

def test_interface_mapping(bin_path, can_interface, tmp_path):
    """Test interface assignment (e.g., vcan0=can99)."""
    # Log file contains 'can99', but we map it to our real 'can_interface'
    fake_iface = "can99"
    log_content = f"(1600000000.000000) {fake_iface} 123#DEADBEEF\n"
    logfile = create_logfile(os.path.join(tmp_path, "test.log"), log_content)

    # Assignment syntax: dest=src (send frames received from src on dest)
    # We want to send ON can_interface frames that came FROM fake_iface
    mapping = f"{can_interface}={fake_iface}"

    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "canplayer"), "-I", logfile, mapping],
            check=True
        )

    # Monitor should see it on can_interface
    assert "123#DEADBEEF" in monitor.output

def test_loop_l(bin_path, can_interface, tmp_path):
    """Test -l <num>: Loop playback."""
    log_content = f"(1600000000.000000) {can_interface} 123#01\n"
    logfile = create_logfile(os.path.join(tmp_path, "test.log"), log_content)

    count = 3
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "canplayer"), "-I", logfile, "-l", str(count)],
            check=True
        )

    # Should appear 3 times
    occurrences = monitor.output.count("123#01")
    assert occurrences == count

def test_ignore_timestamps_t(bin_path, can_interface, tmp_path):
    """Test -t: Ignore timestamps (send immediately)."""
    # Create log with 2 seconds delay between frames
    # If -t works, execution should be instant, not taking >2 seconds
    log_content = (
        f"(100.000000) {can_interface} 123#01\n"
        f"(102.000000) {can_interface} 123#02\n"
    )
    logfile = create_logfile(os.path.join(tmp_path, "test.log"), log_content)

    start = time.time()
    subprocess.run(
        [os.path.join(bin_path, "canplayer"), "-I", logfile, "-t"],
        check=True
    )
    duration = time.time() - start

    # Should be very fast, definitely under 1 second
    assert duration < 1.0

def test_skip_gaps_s(bin_path, can_interface, tmp_path):
    """Test -s <s>: Skip gaps in timestamps > 's' seconds."""
    # Gap of 2 seconds in log file
    log_content = (
        f"(100.000000) {can_interface} 123#01\n"
        f"(102.000000) {can_interface} 123#02\n"
    )
    logfile = create_logfile(os.path.join(tmp_path, "test.log"), log_content)

    # Tell canplayer to skip gaps > 1s (-s 1)
    start = time.time()
    subprocess.run(
        [os.path.join(bin_path, "canplayer"), "-I", logfile, "-s", "1"],
        check=True
    )
    duration = time.time() - start

    # Should skip the 2s wait
    assert duration < 1.5

def test_terminate_n(bin_path, can_interface, tmp_path):
    """Test -n <count>: Terminate after sending count frames."""
    # Timestamps fixed to 6 decimal places (microseconds)
    log_content = (
        f"(100.000000) {can_interface} 123#01\n"
        f"(100.001000) {can_interface} 123#02\n"
        f"(100.002000) {can_interface} 123#03\n"
    )
    logfile = create_logfile(os.path.join(tmp_path, "test.log"), log_content)

    # Process only 2 frames
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "canplayer"), "-I", logfile, "-n", "2"],
            check=True
        )

    assert "123#01" in monitor.output
    assert "123#02" in monitor.output
    assert "123#03" not in monitor.output

def test_verbose_v(bin_path, can_interface, tmp_path):
    """Test -v: Verbose output."""
    log_content = f"(1600000000.000000) {can_interface} 123#11\n"
    logfile = create_logfile(os.path.join(tmp_path, "test.log"), log_content)

    result = subprocess.run(
        [os.path.join(bin_path, "canplayer"), "-I", logfile, "-v"],
        capture_output=True,
        text=True,
        check=True
    )

    # Verbose mode usually prints the frame being sent to stdout
    assert "123" in result.stdout and "11" in result.stdout

def test_disable_loopback_x(bin_path, can_interface, tmp_path):
    """Test -x: Disable local loopback."""
    log_content = f"(1600000000.000000) {can_interface} 123#FF\n"
    logfile = create_logfile(os.path.join(tmp_path, "test.log"), log_content)

    # If loopback is disabled, a local candump should NOT see the frame
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "canplayer"), "-I", logfile, "-x"],
            check=True
        )

    # Expect empty output (or at least the frame shouldn't be there)
    assert "123#FF" not in monitor.output

def test_gap_g(bin_path, can_interface, tmp_path):
    """Test -g <ms>: Gap generation (functional check)."""
    # -g adds a fixed gap. Difficult to measure precisely without affecting test stability,
    # but we can check it runs successfully.
    # Timestamps fixed to 6 decimal places
    log_content = f"(100.000000) {can_interface} 123#11\n(100.000000) {can_interface} 123#22\n"
    logfile = create_logfile(os.path.join(tmp_path, "test.log"), log_content)

    subprocess.run(
        [os.path.join(bin_path, "canplayer"), "-I", logfile, "-g", "10"],
        check=True
    )

def test_parsing_bad_lines(bin_path, can_interface, tmp_path):
    """Test that lines not starting with '(' are ignored."""
    # Timestamps fixed to 6 decimal places
    log_content = (
        "This is a comment line\n"
        f"(100.000000) {can_interface} 123#AA\n"
        "Another invalid line\n"
    )
    logfile = create_logfile(os.path.join(tmp_path, "test.log"), log_content)

    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "canplayer"), "-I", logfile],
            check=True
        )

    # Should only process the valid frame
    assert "123#AA" in monitor.output
    # Ensure it didn't crash or error on invalid lines (return code check is implicit in check=True)
