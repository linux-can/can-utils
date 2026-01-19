# SPDX-License-Identifier: GPL-2.0-only

import subprocess
import time
import os
import pytest
import socket
import signal
import pty
import select
import sys
import tempfile

# Note: bin_path and interface fixtures are provided implicitly by conftest.py

def check_isotp_support(bin_path, interface):
    """
    Helper to check if ISO-TP kernel module is loaded/available.
    Reused logic to prevent failing tests on systems without can-isotp.
    """
    isotpsend = os.path.join(bin_path, "isotpsend")
    if not os.path.exists(isotpsend):
        return False

    try:
        proc = subprocess.Popen(
            [isotpsend, "-s", "123", "-d", "321", interface],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        try:
            outs, errs = proc.communicate(input="00", timeout=0.5)
        except subprocess.TimeoutExpired:
            proc.kill()
            outs, errs = proc.communicate()

        if "socket: Protocol not supported" in errs or "socket: Protocol not supported" in outs:
            return False
        return True
    except Exception:
        return False

def get_free_port():
    """Find a free port on localhost."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

def test_isotpserver_usage(bin_path):
    """
    Test usage/help output for isotpserver.

    Manual Reproduction:
    1. Run: ./isotpserver -h
    2. Expect: Output containing "Usage: isotpserver".
    """
    isotpserver = os.path.join(bin_path, "isotpserver")

    if not os.path.exists(isotpserver):
        pytest.skip("isotpserver binary not found")

    result = subprocess.run(
        [isotpserver, "-h"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    assert "Usage: isotpserver" in result.stderr or "Usage: isotpserver" in result.stdout

def test_isotpserver_bridging(bin_path, can_interface):
    """
    Test TCP <-> CAN bridging using isotpserver with specific format <HEX>.

    Description:
    1. Start candump to monitor bus traffic (diagnostics).
    2. Start isotpserver on a local port.
    3. Start isotprecv to capture CAN frames sent by server.
    4. Connect a TCP client to the server.
    5. Send ASCII HEX via TCP in format <112233> -> Verify reception on CAN (via isotprecv as '11 22 33').
    6. Send ASCII HEX via CAN (isotpsend '44 55 66') -> Verify reception on TCP as <445566>.

    Manual Reproduction:
    1. ./isotpserver -l 12345 -s 123 -d 321 vcan0
    2. ./isotprecv -s 321 -d 123 vcan0
    3. telnet localhost 12345
    4. Type "<112233>" in telnet -> Check isotprecv output for "11 22 33".
    5. echo "44 55 66" | ./isotpsend -s 321 -d 123 vcan0
    6. Check telnet output for "<445566>".
    """
    isotpserver = os.path.join(bin_path, "isotpserver")
    isotprecv = os.path.join(bin_path, "isotprecv")
    isotpsend = os.path.join(bin_path, "isotpsend")
    candump = os.path.join(bin_path, "candump")

    for tool in [isotpserver, isotprecv, isotpsend, candump]:
        if not os.path.exists(tool):
            pytest.skip(f"{tool} not found")

    if not check_isotp_support(bin_path, can_interface):
        pytest.skip("ISO-TP kernel support missing")

    port = get_free_port()
    SRC_ID = "123"
    DST_ID = "321"

    # 0. Start candump for diagnostics
    dump_out = tempfile.TemporaryFile(mode='w+')
    print(f"DEBUG: Starting candump on {can_interface}")
    dump_proc = subprocess.Popen(
        [candump, "-L", can_interface],
        stdout=dump_out,
        stderr=subprocess.PIPE,
        text=True
    )

    # 1. Start isotpserver
    # We use PIPE for stderr to catch errors
    server_cmd = [isotpserver, "-l", str(port), "-s", SRC_ID, "-d", DST_ID, can_interface]
    print(f"DEBUG: Starting isotpserver: {' '.join(server_cmd)}")
    server_proc = subprocess.Popen(
        server_cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    recv_master_fd = None
    recv_slave_fd = None
    recv_proc = None
    sock = None

    try:
        time.sleep(0.5)
        if server_proc.poll() is not None:
            _, err = server_proc.communicate()
            pytest.fail(f"isotpserver failed to start. Stderr: {err}")

        # 2. Start isotprecv (to receive what server sends to CAN)
        # Use PTY to avoid buffering issues
        recv_master_fd, recv_slave_fd = pty.openpty()

        recv_cmd = [isotprecv, "-s", DST_ID, "-d", SRC_ID, can_interface]
        print(f"DEBUG: Starting isotprecv: {' '.join(recv_cmd)}")
        recv_proc = subprocess.Popen(
            recv_cmd,
            stdout=recv_slave_fd,
            stderr=subprocess.PIPE, # Capture errors normally
            close_fds=True
        )
        os.close(recv_slave_fd) # Close slave in parent
        recv_slave_fd = None # Mark as closed

        time.sleep(0.5)

        # 3. Connect TCP Client
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(2.0)
            sock.connect(('127.0.0.1', port))
        except ConnectionRefusedError:
            pytest.fail(f"Could not connect to isotpserver on port {port}")

        # --- TCP -> CAN ---
        # The test requirement is to use <HEX> format.
        # Sending <112233> to the server.
        payload_tcp = "<112233>"
        print(f"DEBUG: Sending via TCP: {payload_tcp}")
        sock.sendall(payload_tcp.encode('ascii'))

        # Give it a moment to process and forward to CAN
        time.sleep(1.0)

        # Read isotprecv output via PTY
        recv_out = ""
        try:
            # Simple non-blocking read loop
            while True:
                r, _, _ = select.select([recv_master_fd], [], [], 0.1)
                if recv_master_fd in r:
                    chunk = os.read(recv_master_fd, 1024)
                    if not chunk:
                        break
                    recv_out += chunk.decode('utf-8', errors='replace')
                else:
                    break
        except OSError:
            pass

        print(f"DEBUG: isotprecv output: {recv_out}")

        # Check success
        # isotprecv prints standard space-separated hex (e.g., "11 22 33")
        # even if the input was compact <112233>.
        if "11 22 33" not in recv_out:
             print("DEBUG: Failure detected in TCP->CAN. Checking candump...")
             time.sleep(0.5)

        assert "11 22 33" in recv_out, "TCP -> CAN failed: Data not received on CAN bus (isotprecv empty or mismatch)"

        # --- CAN -> TCP ---
        # Sending standard space-separated hex on CAN
        payload_can = "44 55 66"
        print(f"DEBUG: Sending via CAN (isotpsend): {payload_can}")

        send_cmd = [isotpsend, "-s", DST_ID, "-d", SRC_ID, can_interface]
        subprocess.run(
            send_cmd,
            input=payload_can,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=True
        )

        # Receive on TCP
        # Expecting the server to format the output as <445566> based on the requirement.
        try:
            tcp_data = sock.recv(1024).decode('ascii')
            print(f"DEBUG: Received via TCP: {tcp_data}")
            # The expectation is <445566> (compact hex inside brackets)
            assert "<445566>" in tcp_data, "CAN -> TCP failed: Data format mismatch or not received on TCP socket"
        except socket.timeout:
            pytest.fail("CAN -> TCP failed: Timeout waiting for data on TCP socket")

    finally:
        if sock:
            sock.close()

        server_proc.terminate()
        server_out, server_err = server_proc.communicate()
        if server_out or server_err:
            print(f"DEBUG: isotpserver stdout: {server_out}")
            print(f"DEBUG: isotpserver stderr: {server_err}")

        if recv_proc:
            recv_proc.terminate()
            recv_proc.wait()

        if recv_master_fd:
            os.close(recv_master_fd)
        if recv_slave_fd: # Should be closed already, but for safety
            os.close(recv_slave_fd)

        dump_proc.terminate()
        dump_proc.wait()
        dump_out.seek(0)
        print(f"DEBUG: candump output:\n{dump_out.read()}")
        dump_out.close()
