# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (c) 2023 Linux CAN project
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

import subprocess
import time
import os
import pytest
import signal

def run_tool(bin_path, tool_name, args):
    """Helper function to run a tool"""
    full_path = os.path.join(bin_path, tool_name)
    cmd = [full_path] + args
    return subprocess.run(cmd, capture_output=True, text=True)

def test_tools_executable(bin_path):
    """Simply checks if the tools show help (existence & executability)"""
    for tool in ["cansend", "candump", "cangen"]:
        result = run_tool(bin_path, tool, ["-?"]) # -? is often help in can-utils, otherwise invalid args
        # We do not expect a crash (Segfault), Exit code may vary depending on arg parsing
        # It is important that stderr or stdout contains something meaningful or the process exits cleanly
        assert result.returncode != -11, f"{tool} crashed (Segfault)"

def test_send_and_receive(bin_path, can_interface):
    """
    Integration test:
    1. Starts candump in the background on the interface.
    2. Sends a CAN frame with cansend.
    3. Checks if candump saw the frame.
    """
    candump_path = os.path.join(bin_path, "candump")
    cansend_path = os.path.join(bin_path, "cansend")

    # Unique ID and data for this test
    can_id = "123"
    can_data = "11223344"
    can_frame = f"{can_id}#{can_data}"

    # 1. Start candump in the background
    # -L for Raw output, -x for extra details (optional), interface
    with subprocess.Popen(
        [candump_path, "-L", can_interface],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        preexec_fn=os.setsid # Process group for clean killing
    ) as dumpproc:

        time.sleep(0.5) # Wait briefly until candump is ready

        # 2. Send frame
        # cansend <device> <can_frame>
        send_result = subprocess.run(
            [cansend_path, can_interface, can_frame],
            capture_output=True,
            text=True
        )

        assert send_result.returncode == 0, f"cansend failed: {send_result.stderr}"

        time.sleep(0.5) # Wait until frame is processed

        # Terminate candump
        os.killpg(os.getpgid(dumpproc.pid), signal.SIGTERM)
        stdout, stderr = dumpproc.communicate()

        # 3. Analysis
        # candump -L Output Format (approx): (timestamp) vcan0 123#11223344
        print(f"Candump Output: {stdout}")

        assert can_id in stdout, "CAN-ID was not received"
        assert can_data in stdout, "CAN data were not received"

def test_cangen_generation(bin_path, can_interface):
    """
    Checks if cangen generates traffic.
    """
    cangen_path = os.path.join(bin_path, "cangen")
    candump_path = os.path.join(bin_path, "candump")

    # Start candump
    with subprocess.Popen(
        [candump_path, "-L", can_interface],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        preexec_fn=os.setsid
    ) as dumpproc:

        # Start cangen: Send 5 frames (-n 5) and then terminate
        subprocess.run(
            [cangen_path, "-n", "5", can_interface],
            check=True
        )

        time.sleep(1)

        os.killpg(os.getpgid(dumpproc.pid), signal.SIGTERM)
        stdout, _ = dumpproc.communicate()

        # We expect 5 lines of output (or more, if other traffic is present)
        lines = [l for l in stdout.splitlines() if can_interface in l]
        assert len(lines) >= 5, "cangen did not generate enough frames/candump did not see them"
