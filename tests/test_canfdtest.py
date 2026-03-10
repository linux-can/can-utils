# SPDX-License-Identifier: GPL-2.0-only

import subprocess
import time
import signal
import os
import pytest

# Note: bin_path and interface fixtures are provided implicitly by conftest.py

def test_usage_error(bin_path):
    """
    Test the behavior when an invalid option is provided.

    Description:
    The tool should reject invalid flags (like '-h' which is not implemented
    as a success flag in this tool) and return a non-zero exit code.

    Manual Reproduction:
    Run: ./canfdtest -h
    Expect: Output showing Usage info and exit code != 0.
    """
    canfdtest = os.path.join(bin_path, "canfdtest")

    if not os.path.exists(canfdtest):
        pytest.skip(f"Binary {canfdtest} not found. Please build it first.")

    result = subprocess.run(
        [canfdtest, "-h"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    # Expect failure code
    assert result.returncode != 0
    # Expect usage hint in output
    assert "Usage: canfdtest" in result.stderr or "Usage: canfdtest" in result.stdout

@pytest.mark.parametrize("extra_args", [
    [],                     # Standard CAN
    ["-d"],                 # CAN FD
    ["-e"],                 # Extended Frames (29-bit)
    ["-b", "-d"],           # CAN FD with Bit Rate Switch
])
def test_full_duplex_communication(bin_path, can_interface, extra_args):
    """
    Test full-duplex communication (ping-pong) between two instances.

    Description:
    Simulates a DUT (Device Under Test) and a Host on the same interface.
    1. DUT: Runs in background (`canfdtest -v <iface>`), echoing frames.
    2. Host: Runs in foreground (`canfdtest -g -v <iface>`), generating frames.
    3. Checks if the Host process exits successfully (0).

    Manual Reproduction:
    1. Terminal 1: ./canfdtest -v vcan0 (MUST use same flags -d/-e as Host)
    2. Terminal 2: ./canfdtest -g -v -l 10 vcan0 (add -d or -e as needed)
    """
    canfdtest = os.path.join(bin_path, "canfdtest")

    if not os.path.exists(canfdtest):
        pytest.skip(f"Binary {canfdtest} not found. Please build it first.")

    # 1. Start DUT (echo server)
    # DUT must also receive flags like -d (FD) or -e (Extended) to open the socket correctly
    # We use setsid to ensure we can kill the process group later
    dut_cmd = [canfdtest, "-v"] + extra_args + [can_interface]
    dut_proc = subprocess.Popen(
        dut_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        preexec_fn=os.setsid
    )

    try:
        # Give DUT time to initialize socket
        time.sleep(0.2)

        # 2. Start Host (generator)
        # -g: generate, -l 10: 10 loops
        host_cmd = [canfdtest, "-g", "-v", "-l", "10"] + extra_args + [can_interface]

        host_result = subprocess.run(
            host_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=5  # Test should be very fast
        )

        # 3. Validation
        if host_result.returncode != 0:
            print(f"--- Host Stdout ---\n{host_result.stdout}")
            print(f"--- Host Stderr ---\n{host_result.stderr}")

        assert host_result.returncode == 0, f"Host failed with args: {extra_args}"
        # The tool outputs "Test messages sent and received: N" on success, not "Test successful"
        assert "Test messages sent and received" in host_result.stdout

    finally:
        # 4. Teardown: Kill DUT
        if dut_proc.poll() is None:
            os.killpg(os.getpgid(dut_proc.pid), signal.SIGTERM)
            dut_proc.wait()
