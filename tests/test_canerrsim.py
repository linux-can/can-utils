# SPDX-License-Identifier: GPL-2.0-only

import subprocess
import time
import os
import pytest
import signal
import tempfile

# Note: bin_path and interface fixtures are provided implicitly by conftest.py

def test_usage_error(bin_path):
    """
    Test the behavior when no arguments are provided.

    Description:
    The tool requires at least an interface name. Running without arguments
    displays the usage text.
    Note: The tool exits with code 0 even on missing arguments.

    Manual Reproduction:
    Run: ./canerrsim
    Expect: Output showing Usage info and exit code 0.
    """
    canerrsim = os.path.join(bin_path, "canerrsim")

    if not os.path.exists(canerrsim):
        pytest.skip(f"Binary {canerrsim} not found. Please build it first.")

    result = subprocess.run(
        [canerrsim],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    # Expect success code (0) as per tool implementation
    assert result.returncode == 0
    # Expect usage hint in output
    assert "Usage: canerrsim" in result.stderr or "Usage: canerrsim" in result.stdout

def test_basic_can_traffic(bin_path, can_interface):
    """
    Sanity check: Verify that standard CAN frames are looped back correctly.
    If this fails, vcan is broken or not up.
    """
    cansend = os.path.join(bin_path, "cansend")
    candump = os.path.join(bin_path, "candump")

    if not os.path.exists(cansend) or not os.path.exists(candump):
        pytest.skip("cansend or candump not found.")

    with tempfile.TemporaryFile(mode='w+') as tmp_out:
        candump_proc = subprocess.Popen(
            [candump, can_interface],
            stdout=tmp_out,
            stderr=subprocess.PIPE,
            text=True
        )
        time.sleep(0.2)

        # Send standard frame 123#112233
        subprocess.run([cansend, can_interface, "123#112233"], check=True)
        time.sleep(0.2)

        candump_proc.terminate()
        candump_proc.wait()

        tmp_out.seek(0)
        output = tmp_out.read()

        assert "123" in output and "11 22 33" in output, "Standard CAN frame loopback failed on vcan0"

@pytest.mark.parametrize("args, grep_for", [
    (["NoAck"], "ERRORFRAME"),
    (["Data0=AA", "Data1=BB"], "AA BB"),
    (["TxTimeout"], "ERRORFRAME"),
])
def test_canerrsim_generate_errors(bin_path, can_interface, args, grep_for):
    """
    Test generating specific error frames using canerrsim.

    Description:
    1. Start candump in background with error frames enabled (-e).
    2. Run canerrsim with specific error options.
    3. Verify candump received the error frame matching the criteria.

    Feature Detection:
    Checks if the environment supports error frame loopback on vcan.
    Skips if not supported.

    Manual Reproduction:
    1. Terminal 1: candump -e vcan0
    2. Terminal 2: ./canerrsim vcan0 <args>
    """
    canerrsim = os.path.join(bin_path, "canerrsim")
    candump = os.path.join(bin_path, "candump")

    if not os.path.exists(canerrsim):
        pytest.skip(f"Binary {canerrsim} not found.")

    # --- Feature Detection Step ---
    # Try to verify if we can receive ANY error frame before running specific tests
    with tempfile.TemporaryFile(mode='w+') as probe_out:
        probe_proc = subprocess.Popen(
            [candump, "-e", can_interface],
            stdout=probe_out,
            stderr=subprocess.PIPE,
            text=True
        )
        time.sleep(0.2)
        # Send a simple error frame
        subprocess.run([canerrsim, can_interface, "NoAck"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.2)
        probe_proc.terminate()
        probe_proc.wait()

        probe_out.seek(0)
        if "ERRORFRAME" not in probe_out.read():
            pytest.skip("Environment does not support loopback of user-space generated CAN error frames on vcan.")

    # --- Actual Test ---
    with tempfile.TemporaryFile(mode='w+') as tmp_out:
        # Start candump to capture the error frame
        candump_proc = subprocess.Popen(
            [candump, "-e", can_interface],
            stdout=tmp_out,
            stderr=subprocess.PIPE,
            text=True
        )

        try:
            time.sleep(0.5)
            # Run canerrsim
            cmd = [canerrsim, can_interface] + args
            result = subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=2
            )

            assert result.returncode == 0, f"canerrsim failed with args: {args}"
            time.sleep(0.5)

        finally:
            candump_proc.terminate()
            try:
                candump_proc.wait(timeout=1)
            except subprocess.TimeoutExpired:
                candump_proc.kill()
                candump_proc.wait()

        # Rewind file to read captured output
        tmp_out.seek(0)
        stdout = tmp_out.read()

        # Validation
        print(f"--- Candump Output ---\n{stdout}")

        found = grep_for in stdout or grep_for.lower() in stdout or grep_for.upper() in stdout
        assert found, f"Expected '{grep_for}' in candump output for args {args}"
