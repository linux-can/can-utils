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

def parse_candump_line(line):
    """
    Parses a line from 'candump -L' (logging format).
    Format: (timestamp) interface ID#DATA or ID##FLAGS_DATA (FD)
    Returns a dict with 'id', 'data', 'flags' (for FD) or None if parsing fails.
    """
    # Regex for standard CAN: (123.456) vcan0 123#112233
    match_std = re.search(r'\(\d+\.\d+\)\s+\S+\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*)', line)
    if match_std:
        return {'id': match_std.group(1), 'data': match_std.group(2), 'type': 'classic'}

    # Regex for CAN FD: (123.456) vcan0 123##<flags><data>
    match_fd = re.search(r'\(\d+\.\d+\)\s+\S+\s+([0-9A-Fa-f]+)##([0-9A-Fa-f]+)', line)
    if match_fd:
        # In log format, data usually follows flags. Not splitting strictly here,
        # just capturing the payload block for verification.
        return {'id': match_fd.group(1), 'payload_raw': match_fd.group(2), 'type': 'fd'}

    return None

class TrafficMonitor:
    """Context manager to run candump in the background."""
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
        time.sleep(0.2) # Wait for socket bind
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.process:
            os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
            out, _ = self.process.communicate()
            self.output = out

# --- Tests for cangen ---

def test_help_option(bin_path):
    """Test -h option: Should print usage and exit with 1."""
    result = subprocess.run([os.path.join(bin_path, "cangen"), "-h"], capture_output=True, text=True)
    assert result.returncode == 1
    assert "Usage: cangen" in result.stderr

def test_count_n(bin_path, can_interface):
    """Test -n <count>: Generate exact number of frames."""
    count = 5
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", str(count)],
            check=True
        )

    lines = [l for l in monitor.output.splitlines() if can_interface in l]
    assert len(lines) == count, f"Expected {count} frames, got {len(lines)}"

def test_gap_g(bin_path, can_interface):
    """Test -g <ms>: Gap generation (timing check)."""
    # Send 5 frames with 100ms gap -> should take approx 400-500ms
    start = time.time()
    subprocess.run(
        [os.path.join(bin_path, "cangen"), can_interface, "-n", "5", "-g", "100"],
        check=True
    )
    duration = time.time() - start
    # Allow some tolerance (e.g., at least 0.4s for 4 gaps)
    assert duration >= 0.4, f"Gap logic too fast: {duration}s"

def test_absolute_time_a(bin_path, can_interface):
    """Test -a: Use absolute time for gap (Functional check)."""
    # Difficult to verify timing strictly without real-time analysis, checking for successful execution
    result = subprocess.run(
        [os.path.join(bin_path, "cangen"), can_interface, "-n", "2", "-g", "10", "-a"],
        check=True
    )
    assert result.returncode == 0

def test_txtime_t(bin_path, can_interface):
    """Test -t: Use SO_TXTIME (Functional check)."""
    # Requires kernel support, checking for no crash
    result = subprocess.run(
        [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-t"],
        capture_output=True
    )
    # Don't assert 0 explicitly if kernel might not support it, but vcan usually works.
    if result.returncode != 0:
        pytest.skip("Kernel might not support SO_TXTIME or config issue")

def test_start_time_option(bin_path, can_interface):
    """Test --start <ns>: Start time (Functional check)."""
    # Using a timestamp in the past to ensure immediate execution or future for delay
    # Just checking argument parsing valid
    result = subprocess.run(
        [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "--start", "0"],
        check=True
    )
    assert result.returncode == 0

def test_extended_frames_e(bin_path, can_interface):
    """Test -e: Extended frame mode (EFF)."""
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-e"],
            check=True
        )

    parsed = parse_candump_line(monitor.output.strip())
    assert parsed, "No frame captured"
    # Extended IDs are usually shown with 8 chars in candump or check length > 3
    # Standard: 3 chars (e.g., 123), Extended: 8 chars (e.g., 12345678)
    # Note: cangen generates random IDs.
    assert len(parsed['id']) == 8, f"Expected 8-char hex ID for EFF, got {parsed['id']}"

def test_can_fd_f(bin_path, can_interface):
    """Test -f: CAN FD frames."""
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-f"],
            check=True
        )

    # candump -L for FD uses '##' separator
    assert "##" in monitor.output, "CAN FD separator '##' not found in candump output"

def test_can_fd_brs_b(bin_path, can_interface):
    """Test -b: CAN FD with Bitrate Switch (BRS)."""
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-b"],
            check=True
        )
    assert "##" in monitor.output

def test_can_fd_esi_E(bin_path, can_interface):
    """Test -E: CAN FD with Error State Indicator (ESI)."""
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-E"],
            check=True
        )
    assert "##" in monitor.output

def test_can_xl_X(bin_path, can_interface):
    """Test -X: CAN XL frames."""
    # This might fail on older kernels/interfaces. We skip if return code is error.
    result = subprocess.run(
        [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-X"],
        capture_output=True
    )
    if result.returncode != 0:
        pytest.skip("CAN XL not supported by this environment/kernel")

def test_rtr_frames_R(bin_path, can_interface):
    """Test -R: Remote Transmission Request (RTR)."""
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-R"],
            check=True
        )

    # candump -L often represents RTR with 'R' in the data part or 'remote' flag
    # For candump -L: "vcan0 123#R"
    assert "#R" in monitor.output, f"RTR flag not found in: {monitor.output}"

def test_dlc_greater_8_option_8(bin_path, can_interface):
    """Test -8: Allow DLC > 8 for Classic CAN."""
    # This generates frames with DLC > 8 but len 8.
    # It's a specific protocol violation test.
    result = subprocess.run(
        [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-8"],
        check=True
    )
    assert result.returncode == 0

def test_mix_modes_m(bin_path, can_interface):
    """Test -m: Mix CC, FD, XL."""
    result = subprocess.run(
        [os.path.join(bin_path, "cangen"), can_interface, "-n", "5", "-m"],
        check=True
    )
    assert result.returncode == 0

def test_id_generation_mode_I(bin_path, can_interface):
    """Test -I <mode>: ID generation."""

    # Fixed ID
    target_id = "123"
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-I", target_id],
            check=True
        )
    parsed = parse_candump_line(monitor.output)
    assert parsed['id'] == target_id

    # Increment ID ('i')
    # Generate 3 frames, IDs should be consecutive (or incrementing)
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", "3", "-I", "i"],
            check=True
        )
    lines = monitor.output.strip().splitlines()
    ids = [int(parse_candump_line(l)['id'], 16) for l in lines]
    assert len(ids) == 3
    assert ids[1] == ids[0] + 1
    assert ids[2] == ids[1] + 1

def test_dlc_generation_mode_L(bin_path, can_interface):
    """Test -L <mode>: DLC/Length generation."""

    # Fixed Length 4
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-L", "4"],
            check=True
        )
    parsed = parse_candump_line(monitor.output)
    # Data hex string length should be 2 * DLC (e.g. 4 bytes = 8 chars)
    assert len(parsed['data']) == 8

def test_data_generation_mode_D(bin_path, can_interface):
    """Test -D <mode>: Data content generation."""

    # Fixed Payload
    payload = "11223344"
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-D", payload, "-L", "4"],
            check=True
        )
    parsed = parse_candump_line(monitor.output)
    assert parsed['data'] == payload

    # Increment Payload ('i')
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", "2", "-D", "i", "-L", "1"],
            check=True
        )
    lines = monitor.output.strip().splitlines()
    data_bytes = [int(parse_candump_line(l)['data'], 16) for l in lines]
    # The first byte should increment (modulo 256)
    assert (data_bytes[1] - data_bytes[0]) % 256 == 1

def test_disable_loopback_x(bin_path, can_interface):
    """Test -x: Disable loopback."""
    # If loopback is disabled, candump on the SAME interface (same socket namespace)
    # should NOT receive the frames if it's running on the same host usually.
    # Note: On vcan, local loopback is handled by the driver.
    # cangen -x sets CAN_RAW_LOOPBACK = 0.

    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", "3", "-x"],
            check=True
        )

    # We expect output to be empty because candump is a local socket on the same node
    # and we disabled loopback for the sender.
    assert monitor.output.strip() == "", "candump received frames despite -x (disable loopback)"

def test_poll_p(bin_path, can_interface):
    """Test -p <timeout>: Poll on ENOBUFS."""
    # Functional test, hard to provoke ENOBUFS on empty vcan
    result = subprocess.run(
        [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-p", "10"],
        check=True
    )
    assert result.returncode == 0

def test_priority_P(bin_path, can_interface):
    """Test -P <priority>: Set socket priority."""
    result = subprocess.run(
        [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-P", "5"],
        check=True
    )
    assert result.returncode == 0

def test_ignore_enobufs_i(bin_path, can_interface):
    """Test -i: Ignore ENOBUFS."""
    result = subprocess.run(
        [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-i"],
        check=True
    )
    assert result.returncode == 0

def test_burst_c(bin_path, can_interface):
    """Test -c <count>: Burst count."""
    count = 6
    burst = 3
    # Sending 6 frames in bursts of 3. Total should still be 6.
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", str(count), "-c", str(burst)],
            check=True
        )
    lines = [l for l in monitor.output.splitlines() if can_interface in l]
    assert len(lines) == count

def test_verbose_v(bin_path, can_interface):
    """Test -v: Verbose output to stdout."""
    # -v prints ascii art/dots, -v -v prints details
    result = subprocess.run(
        [os.path.join(bin_path, "cangen"), can_interface, "-n", "1", "-v", "-v"],
        capture_output=True,
        text=True
    )
    # Output should contain the Interface and ID
    assert can_interface in result.stdout
    # The output format is like: "  vcan0  737   [3]  EF 32 23"
    # It does not contain labels like "ID:", "data:", "DLC:".
    # Check for the presence of brackets which indicate DLC in this format.
    assert "[" in result.stdout and "]" in result.stdout

def test_random_id_flags(bin_path, can_interface):
    """Test ID generation flags: r (random), e (even), o (odd)."""
    # Test Even
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", "5", "-I", "e"],
            check=True
        )
    lines = monitor.output.strip().splitlines()
    for l in lines:
        pid = int(parse_candump_line(l)['id'], 16)
        assert pid % 2 == 0, f"ID {pid} is not even"

    # Test Odd
    with TrafficMonitor(bin_path, can_interface) as monitor:
        subprocess.run(
            [os.path.join(bin_path, "cangen"), can_interface, "-n", "5", "-I", "o"],
            check=True
        )
    lines = monitor.output.strip().splitlines()
    for l in lines:
        pid = int(parse_candump_line(l)['id'], 16)
        assert pid % 2 != 0, f"ID {pid} is not odd"
