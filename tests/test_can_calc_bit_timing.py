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
import re

# --- Helper Functions ---

def run_ccbt(bin_path, args):
    """
    Helper to run can-calc-bit-timing.
    Returns the subprocess.CompletedProcess object.
    """
    cmd = [os.path.join(bin_path, "can-calc-bit-timing")] + args
    return subprocess.run(cmd, capture_output=True, text=True)

# --- Tests ---

def test_usage_on_invalid_option(bin_path):
    """
    Description:
        Tests that providing an invalid option (like -h) results in an error
        message and prints the usage information.
        Note: The tool does not support -h explicitly, it treats it as invalid.

    Manual reproduction:
        $ can-calc-bit-timing -h
        > invalid option -- 'h'
        > Usage: can-calc-bit-timing [options] ...
    """
    result = run_ccbt(bin_path, ["-h"])

    # Expect failure code usually for invalid options
    assert result.returncode != 0
    assert "Usage: can-calc-bit-timing" in result.stderr or "Usage: can-calc-bit-timing" in result.stdout

def test_list_controllers(bin_path):
    """
    Description:
        Tests the -l option to list all supported CAN controller names.

    Manual reproduction:
        $ can-calc-bit-timing -l
        > ...
        > mcp251x
        > ...
    """
    result = run_ccbt(bin_path, ["-l"])

    assert result.returncode == 0
    # Check for a common controller known to be in the list (e.g., mcp251x or sja1000)
    # The list is usually quite long.
    assert "mcp251x" in result.stdout or "sja1000" in result.stdout

def test_basic_calculation_stdout(bin_path):
    """
    Description:
        Tests a basic bit timing calculation for a standard bitrate (500k)
        and clock (8MHz). Verifies that a table is produced.

    Manual reproduction:
        $ can-calc-bit-timing -c 8000000 -b 500000
        > Bit rate : 500000 Rate error : 0.00% ...
    """
    # 8MHz clock, 500kbit/s
    result = run_ccbt(bin_path, ["-c", "8000000", "-b", "500000"])

    assert result.returncode == 0
    # Output should contain column headers like "SampP" (Sample Point) and the bitrate
    assert "SampP" in result.stdout
    assert "500000" in result.stdout

def test_quiet_mode(bin_path):
    """
    Description:
        Tests the -q option (quiet mode), which should suppress the header line.

    Manual reproduction:
        $ can-calc-bit-timing -q -c 8000000 -b 500000
        > (Output should start with data, not the 'Bit timing parameters...' header)
    """
    # Run without -q first to confirm header exists
    res_std = run_ccbt(bin_path, ["-c", "8000000", "-b", "500000"])
    assert "Bit timing parameters" in res_std.stdout

    # Run with -q
    res_quiet = run_ccbt(bin_path, ["-q", "-c", "8000000", "-b", "500000"])
    assert res_quiet.returncode == 0
    assert "Bit timing parameters" not in res_quiet.stdout
    # Data should still be there
    assert "500000" in res_quiet.stdout

def test_verbose_output(bin_path):
    """
    Description:
        Tests the -v option for verbose output.

    Manual reproduction:
        $ can-calc-bit-timing -v -c 8000000 -b 500000
    """
    result = run_ccbt(bin_path, ["-v", "-c", "8000000", "-b", "500000"])
    assert result.returncode == 0
    # Verbose mode usually prints more details, but the table should definitely be there.
    # We check for "SampP" to ensure valid calculation output.
    assert "SampP" in result.stdout

def test_data_bitrate_fd(bin_path):
    """
    Description:
        Tests the -d option to specify a data bitrate (CAN FD).

    Manual reproduction:
        $ can-calc-bit-timing -c 40000000 -b 1000000 -d 2000000
    """
    # 40MHz clock, 1M nominal, 2M data
    result = run_ccbt(bin_path, ["-c", "40000000", "-b", "1000000", "-d", "2000000"])

    assert result.returncode == 0
    # Should calculate for both
    assert "SampP" in result.stdout
    assert "1000000" in result.stdout
    assert "2000000" in result.stdout

def test_specific_sample_point(bin_path):
    """
    Description:
        Tests the -s option to define a specific sample point (e.g., 875 for 87.5%).

    Manual reproduction:
        $ can-calc-bit-timing -c 8000000 -b 500000 -s 875
        > ... Sample Point : 87.5% ...
    """
    # 875 means 87.5%
    result = run_ccbt(bin_path, ["-c", "8000000", "-b", "500000", "-s", "875"])

    assert result.returncode == 0
    assert "87.5%" in result.stdout

def test_specific_algorithm(bin_path):
    """
    Description:
        Tests the --alg option to select a different algorithm.
        Assuming 'v4_8' is a valid algorithm key based on source files
        (can-calc-bit-timing-v4_8.c). Note: Valid keys depend on compilation.
        We check if it accepts the flag without crashing, even if default is used.

    Manual reproduction:
        $ can-calc-bit-timing -c 8000000 -b 500000 --alg v4.8
        (Note: algorithm naming convention might vary, using a safe check)
    """
    # Trying to list supported algorithms first would be ideal if possible,
    # but based on file list, let's try a standard calculation with a potentially valid alg string.
    # If the alg string is invalid, it usually warns or errors.

    # We simply check that the flag is parsed.
    # "default" should always be valid.
    result = run_ccbt(bin_path, ["-c", "8000000", "-b", "500000", "--alg", "default"])
    assert result.returncode == 0
