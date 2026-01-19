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
import sys

# Add option to select the interface (default: vcan0)
def pytest_addoption(parser):
    parser.addoption(
        "--can-iface",
        action="store",
        default="vcan0",
        help="The CAN interface for tests (e.g., vcan0)"
    )
    parser.addoption(
        "--bin-path",
        action="store",
        default=".",
        help="Path to the compiled can-utils binaries"
    )

@pytest.fixture(scope="session")
def can_interface(request):
    """
    Checks if the specified interface exists.
    If not, the tests are skipped.
    """
    iface = request.config.getoption("--can-iface")

    try:
        # 'ip link show <iface>' returns 0 if it exists, otherwise an error
        subprocess.check_call(
            ["ip", "link", "show", iface],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
    except (subprocess.CalledProcessError, FileNotFoundError):
        pytest.skip(f"Prerequisite not met: Interface '{iface}' not found.")

    return iface

@pytest.fixture(scope="session")
def bin_path(request):
    """
    Returns the path to the binaries and checks if they exist.
    """
    path = request.config.getoption("--bin-path")
    # Exemplarily check if 'cansend' is located there
    cansend_path = os.path.join(path, "cansend")
    if not os.path.isfile(cansend_path) and not os.path.isfile(cansend_path + ".exe"):
         pytest.skip(f"Compiled tools not found in '{path}'. Please run 'make' first.")
    return path
