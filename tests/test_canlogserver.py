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
import socket
import select

# --- Helper Functions ---

def get_free_port():
    """Finds a free TCP port on localhost."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        # Bind to port 0 lets the OS choose an available port
        s.bind(('localhost', 0))
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        # Get the port chosen by the OS
        return s.getsockname()[1]

class CanLogServerMonitor:
    """
    Context manager to run canlogserver.
    """
    def __init__(self, bin_path, interface, args=None, port=None):
        self.cmd = [os.path.join(bin_path, "canlogserver")]

        # Determine port: use provided one or find a free one
        if port is None:
            self.port = get_free_port()
        else:
            self.port = port

        # Explicitly add port argument
        self.cmd.extend(["-p", str(self.port)])

        if args:
            self.cmd.extend(args)

        self.cmd.append(interface)

        self.process = None
        self.client_socket = None

    def __enter__(self):
        # Redirect stderr to DEVNULL to prevent buffer filling deadlocks
        # If the server writes too much to stderr and we don't read it, it hangs.
        self.process = subprocess.Popen(
            self.cmd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
            preexec_fn=os.setsid
        )
        time.sleep(0.5) # Wait for server startup
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.client_socket:
            try:
                self.client_socket.close()
            except OSError:
                pass

        if self.process:
            # Check if process is still running before attempting to kill
            if self.process.poll() is None:
                try:
                    # Send SIGTERM to the process group
                    os.killpg(os.getpgid(self.process.pid), signal.SIGTERM)
                    # Wait with timeout to avoid infinite hang
                    self.process.wait(timeout=2)
                except (ProcessLookupError, subprocess.TimeoutExpired):
                    # Force kill if it doesn't exit nicely
                    try:
                        os.killpg(os.getpgid(self.process.pid), signal.SIGKILL)
                        self.process.wait(timeout=1)
                    except (ProcessLookupError, subprocess.TimeoutExpired):
                        pass

    def connect(self):
        """Connects to the canlogserver via TCP."""
        retries = 10
        for i in range(retries):
            try:
                self.client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.client_socket.connect(('localhost', self.port))
                return True
            except ConnectionRefusedError:
                time.sleep(0.2)
        return False

    def read_data(self, timeout=2.0):
        """Reads data from the TCP socket until data arrives or timeout."""
        if not self.client_socket:
            return ""

        start_time = time.time()
        received_data = ""

        while time.time() - start_time < timeout:
            try:
                ready = select.select([self.client_socket], [], [], 0.1)
                if ready[0]:
                    chunk = self.client_socket.recv(4096).decode('utf-8', errors='ignore')
                    if chunk:
                        received_data += chunk
                        if len(chunk) > 0:
                             time.sleep(0.1)
                             ready_more = select.select([self.client_socket], [], [], 0)
                             if ready_more[0]:
                                 received_data += self.client_socket.recv(4096).decode('utf-8', errors='ignore')
                             break
                    else:
                        # Connection closed
                        break
            except OSError:
                break

        return received_data

def run_cansend(bin_path, interface, frame):
    """Executes cansend."""
    subprocess.run([os.path.join(bin_path, "cansend"), interface, frame], check=True)

# --- Tests for canlogserver ---

def test_help_option(bin_path):
    """
    Tests the help option '-h'.
    Manual reproduction: $ ./canlogserver -h
    """
    result = subprocess.run([os.path.join(bin_path, "canlogserver"), "-h"], capture_output=True, text=True)
    assert "Usage: canlogserver" in result.stderr or "Usage: canlogserver" in result.stdout

def test_basic_logging(bin_path, can_interface):
    """
    Tests basic logging functionality.
    Manual reproduction:
    $ ./canlogserver vcan0 &
    $ nc localhost 28700
    $ ./cansend vcan0 123#11
    """
    with CanLogServerMonitor(bin_path, can_interface) as server:
        assert server.connect(), f"Could not connect to canlogserver on port {server.port}"
        run_cansend(bin_path, can_interface, "123#112233")
        data = server.read_data(timeout=2.0)
        assert "123" in data
        assert "112233" in data

def test_custom_port(bin_path, can_interface):
    """
    Tests the custom port option '-p'.
    Manual reproduction: ./canlogserver -p 28701 vcan0
    """
    custom_port = get_free_port()
    with CanLogServerMonitor(bin_path, can_interface, port=custom_port) as server:
        assert server.connect(), f"Could not connect to canlogserver on port {custom_port}"
        run_cansend(bin_path, can_interface, "456#AA")
        data = server.read_data(timeout=2.0)
        assert "456" in data

def test_id_filter_mask_value(bin_path, can_interface):
    """
    Tests ID filtering with mask (-m) and value (-v).
    Manual reproduction: ./canlogserver -m 0x7FF -v 0x123 vcan0
    """
    args = ["-m", "0x7FF", "-v", "0x123"]
    with CanLogServerMonitor(bin_path, can_interface, args=args) as server:
        assert server.connect()
        run_cansend(bin_path, can_interface, "123#11")
        data = server.read_data(timeout=2.0)
        assert "123" in data

        run_cansend(bin_path, can_interface, "456#22")
        data = server.read_data(timeout=1.0)
        assert "456" not in data

def test_id_filter_invert(bin_path, can_interface):
    """
    Tests the inverted filter option '-i'.
    Manual reproduction: ./canlogserver -m 0x7FF -v 0x123 -i 1 vcan0
    """
    args = ["-m", "0x7FF", "-v", "0x123", "-i", "1"]
    with CanLogServerMonitor(bin_path, can_interface, args=args) as server:
        assert server.connect()
        run_cansend(bin_path, can_interface, "123#11")
        data = server.read_data(timeout=1.0)
        assert "123" not in data

        run_cansend(bin_path, can_interface, "456#22")
        data = server.read_data(timeout=2.0)
        assert "456" in data

def test_multiple_interfaces(bin_path, can_interface):
    """
    Tests passing multiple interfaces.
    Manual reproduction: ./canlogserver vcan0 vcan0
    """
    with CanLogServerMonitor(bin_path, can_interface, args=[can_interface]) as server:
        assert server.connect(), "Could not connect to canlogserver with multiple interfaces"

def test_error_frame_mask(bin_path, can_interface):
    """
    Tests the error frame mask option '-e'.
    Manual reproduction: ./canlogserver -e 0xFFFFFFFF vcan0
    """
    args = ["-e", "0xFFFFFFFF"]
    with CanLogServerMonitor(bin_path, can_interface, args=args) as server:
        assert server.connect()
        run_cansend(bin_path, can_interface, "123#11")
        data = server.read_data(timeout=2.0)
        assert "123" in data

def test_signal_sigint_shutdown(bin_path, can_interface):
    """
    Tests the server shutdown on SIGINT (Ctrl-C) during 'accept()'.

    Verifies that the server terminates correctly when receiving SIGINT while
    waiting for a client connection.

    Manual reproduction:
    1. Start server: $ ./canlogserver vcan0
    2. Press Ctrl-C (send SIGINT).
    3. Check exit code (should be 130).
    """
    with CanLogServerMonitor(bin_path, can_interface) as server:
        assert server.process.poll() is None
        try:
            os.killpg(os.getpgid(server.process.pid), signal.SIGINT)
        except ProcessLookupError:
            pytest.fail("Server process not found for SIGINT")

        try:
            server.process.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            pytest.fail("Server did not exit on SIGINT (Infinite Accept Loop Bug)")

        assert server.process.returncode == 130

def test_bind_retry_shutdown(bin_path, can_interface):
    """
    Tests shutdown behavior when the server is stuck in the bind retry loop.

    This reproduces the bug where the server would print dots endlessly
    and ignore SIGINT if the port was already in use.

    Manual reproduction:
    1. Open a listening socket on port 28700: $ nc -l 28700 &
    2. Start server: $ ./canlogserver -p 28700 vcan0
       -> Server prints dots ............
    3. Press Ctrl-C.
       -> Server should exit immediately, not hang.
    """
    # 1. Occupy a port locally
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('localhost', 0))
        s.listen(1)
        busy_port = s.getsockname()[1]

        # 2. Start canlogserver trying to bind to the same port
        with CanLogServerMonitor(bin_path, can_interface, port=busy_port) as server:
            # Allow time for server to start and enter the retry loop
            time.sleep(1.0)

            # 3. Send SIGINT
            # The server should be stuck in the bind loop printing dots.
            try:
                os.killpg(os.getpgid(server.process.pid), signal.SIGINT)
            except ProcessLookupError:
                pytest.fail("Server process died unexpectedly")

            # 4. Wait for graceful exit
            try:
                server.process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                pytest.fail("Server hung in bind retry loop (Infinite Bind Loop Bug)")

            # Check expected exit code (128 + 2 = 130)
            assert server.process.returncode == 130
