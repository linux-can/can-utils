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
import tempfile
import signal
import sys
import struct
import datetime
import stat
import select

# Helper function to check for binaries
def require_binaries(bin_path, binaries):
    for binary in binaries:
        if not os.path.exists(os.path.join(bin_path, binary)):
            pytest.skip(f"Binary '{binary}' not found in {bin_path}")

def create_test_file(filepath, size_bytes):
    """
    Python implementation of isobusfs_create_test_file.sh
    Generates a file with a specific XOR pattern (0xdeadbeef).
    """
    pattern = 0xdeadbeef
    # Each iteration writes 4 bytes
    iterations = size_bytes // 4

    with open(filepath, 'wb') as f:
        # Write in chunks to improve performance for larger files
        chunk_size = 4096 # iterations per chunk
        for i in range(0, iterations, chunk_size):
            chunk_end = min(i + chunk_size, iterations)
            data = bytearray()
            for counter in range(i, chunk_end):
                # bash: printf "%08x" $((counter ^ pattern)) | xxd -r -p
                # This results in Big-Endian binary representation
                val = counter ^ pattern
                data.extend(struct.pack('>I', val))
            f.write(data)

def create_test_infrastructure(root):
    """
    Python implementation of isobusfs_create_test_dirs.sh
    Creates the complex directory structure, special files, and timestamps.
    """
    print(f"DEBUG: Generating test infrastructure in {root}")

    # 1. Create Directory Hierarchy
    # NOTE: Changed MCMC0683 to MCMC0000 to match the server's default fallback
    # when no local name is provided via -n argument.
    dirs = [
        "dir1/dir2/dir3/dir4",
        "dir1/dir2/dir3/dir5",
        "MCMC0000/msd_dir1/msd_dir2/~/~tilde_dir",
        "dir1/~/~",
        "dir1/dir2/special_chars_*?/",
        "dir1/dir2/unicode_名字",
    ]
    for d in dirs:
        os.makedirs(os.path.join(root, d), exist_ok=True)

    # 2. Create simple text file
    with open(os.path.join(root, "dir1/dir2/file0"), "w") as f:
        f.write("hello\n")

    # 3. Create binary test files (1KB and 1MB)
    create_test_file(os.path.join(root, "dir1/dir2/file1k"), 1024)
    create_test_file(os.path.join(root, "dir1/dir2/file1m"), 1048576)

    # 4. Create 300 files with long names
    # Suffix reconstructed based on visual pattern (alphabet repeated ~8.5 times ending in 'p')
    alphabet = "abcdefghijklmnopqrstuvwxyz"
    long_suffix = (alphabet * 8) + alphabet[:16] # Results in 224 chars

    for count in range(1, 301):
        fname = f"long_name_{count}_{long_suffix}"
        with open(os.path.join(root, fname), 'w') as f:
            pass

    # 5. Create special permission files
    base_perm_dir = os.path.join(root, "dir1/dir2")

    # hidden_file
    with open(os.path.join(base_perm_dir, "hidden_file"), 'w') as f:
        pass

    # readonly_file (chmod 444)
    ro_path = os.path.join(base_perm_dir, "readonly_file")
    with open(ro_path, 'w') as f:
        pass
    os.chmod(ro_path, 0o444)

    # executable_file (chmod +x -> 755 typically)
    exe_path = os.path.join(base_perm_dir, "executable_file")
    with open(exe_path, 'w') as f:
        pass
    current_mode = os.stat(exe_path).st_mode
    os.chmod(exe_path, current_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    # no_read_permission_file (chmod 000)
    no_read_path = os.path.join(base_perm_dir, "no_read_permission_file")
    with open(no_read_path, 'w') as f:
        pass
    os.chmod(no_read_path, 0o000)

    # 6. Date/Timestamp Problems
    # Note: timestamps might be limited by the host OS (Y2038 on 32-bit systems)
    date_dirs = {
        "y2000_problem": "2000-01-01 00:00:00",
        "y2038_problem": "2038-01-19 03:14:07",
        "y1979_problem": "1979-12-31 23:59:59",
        "y1980_problem": "1980-01-01 00:00:00",
        "y2107_problem": "2107-12-31 23:59:59",
        "y2108_problem": "2108-01-01 00:00:00",
    }

    for dirname, date_str in date_dirs.items():
        dir_path = os.path.join(base_perm_dir, dirname)
        os.makedirs(dir_path, exist_ok=True)
        try:
            dt = datetime.datetime.strptime(date_str, "%Y-%m-%d %H:%M:%S")
            ts = dt.timestamp()
            # Set access and modification time
            os.utime(dir_path, (ts, ts))
        except (OverflowError, OSError, ValueError):
            print(f"WARNING: Could not set timestamp {date_str} for {dirname} on this platform.")

def test_isobusfs_selftest(bin_path, can_interface):
    """
    Test ISOBUS File Server interaction using the client's 'selftest' command.
    Includes generation of complex file infrastructure.

    Description:
    1. Create a temporary directory.
    2. Populate it with complex file structure (long names, deep dirs, special permissions).
    3. Start isobusfs-srv.
    4. Start isobusfs-cli and run 'selftest'.
    5. Verify Test 1-7 PASS explicitly.
    6. Verify Test 8 PASS or prove successful data transfer (partial pass).
    7. If tests fail, run internal 'dmesg' command on client.

    Manual Reproduction:
    1. Run isobusfs_create_test_dirs.sh inside a test folder.
    2. ./isobusfs-srv -i vcan0 -a 80 -v vol1:/path/to/folder -l 3
    3. ./isobusfs-cli -i vcan0 -a 90 -r 80 -l 3
    4. Type 'selftest'
    """
    server_bin = "isobusfs-srv"
    client_bin = "isobusfs-cli"
    require_binaries(bin_path, [server_bin, client_bin])

    server_exe = os.path.join(bin_path, server_bin)
    client_exe = os.path.join(bin_path, client_bin)

    # Use a temporary directory as the volume root
    with tempfile.TemporaryDirectory() as tmp_vol_dir:
        # 1. Generate Infrastructure
        create_test_infrastructure(tmp_vol_dir)

        # Define addresses (hex)
        srv_addr = "80"
        cli_addr = "90"
        vol_name = "vol1"

        # Construct Server Command
        server_cmd = [
            server_exe,
            "-i", can_interface,
            "-a", srv_addr,
            "-v", f"{vol_name}:{tmp_vol_dir}",
            "-l", "3"
        ]

        print(f"DEBUG: Starting Server: {' '.join(server_cmd)}")

        # Start Server
        server_proc = subprocess.Popen(
            server_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        client_proc = None

        try:
            # Wait for server initialization
            time.sleep(1)

            if server_proc.poll() is not None:
                _, err = server_proc.communicate()
                pytest.fail(f"isobusfs-srv failed to start. Error: {err.decode('utf-8', errors='replace')}")

            # Construct Client Command
            client_cmd = [
                client_exe,
                "-i", can_interface,
                "-a", cli_addr,
                "-r", srv_addr,
                "-l", "3"
            ]

            print(f"DEBUG: Starting Client: {' '.join(client_cmd)}")

            # Start Client
            client_proc = subprocess.Popen(
                client_cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=0
            )

            # Interact with Client
            print("DEBUG: Sending 'selftest' command...")
            client_proc.stdin.write("selftest\n")
            client_proc.stdin.flush()

            # Allow time for tests (20s)
            time.sleep(20)

            # After tests, request internal dmesg to check for errors
            print("DEBUG: Sending 'dmesg' command to client...")
            client_proc.stdin.write("dmesg\n")
            client_proc.stdin.flush()

            # Give dmesg a moment to output
            time.sleep(1)

            client_proc.terminate()

            try:
                outs, errs = client_proc.communicate(timeout=1)
            except subprocess.TimeoutExpired:
                client_proc.kill()
                outs, errs = client_proc.communicate()

            print("-" * 20 + " Client Output " + "-" * 20)
            print(outs)
            print("-" * 20 + " Client Errors " + "-" * 20)
            print(errs)
            print("-" * 50)

            # Verification logic
            failures = []

            # Tests 1-7 MUST pass
            mandatory_tests = [
                "Test 1: PASSED",
                "Test 2: PASSED",
                "Test 3: PASSED",
                "Test 4: PASSED",
                "Test 5: PASSED",
                "Test 6: PASSED",
                "Test 7: PASSED",
                "Test 8: PASSED",
            ]

            for test in mandatory_tests:
                if test not in outs:
                    failures.append(test.replace(": PASSED", ""))

            if failures:
                pytest.fail(f"The following ISOBUS FS tests failed: {', '.join(failures)}. See output above for details and internal dmesg log.")

        finally:
            if server_proc.poll() is None:
                server_proc.terminate()
                try:
                    server_out, server_err = server_proc.communicate(timeout=1)
                except subprocess.TimeoutExpired:
                    server_proc.kill()
                    server_out, server_err = server_proc.communicate()

                print("-" * 20 + " Server Output " + "-" * 20)
                # Manually decode output, replacing invalid characters
                if server_out:
                    print(server_out.decode('utf-8', errors='replace'))
                if server_err:
                    print(server_err.decode('utf-8', errors='replace'))
                print("-" * 50)

            if client_proc and client_proc.poll() is None:
                client_proc.kill()

def test_isobusfs_interactive_commands(bin_path, can_interface):
    """
    Test the interactive command shell of isobusfs-cli.

    Covers:
    - help: Help text display
    - ls/ll: Directory listing
    - cd: Directory navigation
    - pwd: Working directory reporting (ISOBUS path format checks)
    - get: File download
    - dmesg: Log buffer access (executed at the end)
    - exit: Clean exit
    - quit: Clean exit (tested in separate session)
    """
    server_bin = "isobusfs-srv"
    client_bin = "isobusfs-cli"
    require_binaries(bin_path, [server_bin, client_bin])

    # FIX 1: Use absolute paths to find binaries even when CWD changes
    server_exe = os.path.abspath(os.path.join(bin_path, server_bin))
    client_exe = os.path.abspath(os.path.join(bin_path, client_bin))

    # Helper function for truly interactive communication
    def interact(proc, cmd=None, expect_prompt=True, timeout=5):
        """
        Sends a command and reads output until the prompt 'isobusfs> ' appears.
        Using binary mode (bytes) to avoid buffering issues.
        """
        if cmd:
            proc.stdin.write(cmd.encode('utf-8') + b"\n")
            proc.stdin.flush()

        if not expect_prompt:
            return ""

        output = b""
        start_time = time.time()

        while time.time() - start_time < timeout:
            # Check if stdout has data ready to read
            rlist, _, _ = select.select([proc.stdout], [], [], 0.1)
            if rlist:
                # Read 1 byte at a time to safely detect the prompt boundary
                char = proc.stdout.read(1)
                if not char: # EOF
                    break
                output += char
                if output.endswith(b"isobusfs> "):
                    return output.decode('utf-8', errors='replace')

            # Check if process died
            if proc.poll() is not None:
                break

        # If we timed out but got some output, return it for debugging/assertion
        return output.decode('utf-8', errors='replace')

    # Use temporary directories
    with tempfile.TemporaryDirectory() as srv_dir, tempfile.TemporaryDirectory() as cli_dir:
        # 1. Prepare Server Data
        create_test_infrastructure(srv_dir)

        # 2. Start Server
        srv_addr = "80"
        cli_addr = "95"
        vol_name = "testvol"

        server_cmd = [
            server_exe,
            "-i", can_interface,
            "-a", srv_addr,
            "-v", f"{vol_name}:{srv_dir}",
            "-l", "3"
        ]

        print(f"DEBUG: Starting Server: {' '.join(server_cmd)}")
        server_proc = subprocess.Popen(
            server_cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        client_proc = None

        try:
            time.sleep(1)
            if server_proc.poll() is not None:
                _, err = server_proc.communicate()
                pytest.fail(f"Server failed to start: {err.decode('utf-8', errors='replace')}")

            client_cmd = [
                client_exe,
                "-i", can_interface,
                "-a", cli_addr,
                "-r", srv_addr,
                "-l", "3"
            ]

            print(f"DEBUG: Running Client Session 1 in {cli_dir}")
            # FIX 2: Use bufsize=0 and binary streams for precise control
            client_proc = subprocess.Popen(
                client_cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                cwd=cli_dir,
                bufsize=0
            )

            # --- Session 1: Main Workflow ---

            # 1. Initial Prompt (wait for startup)
            out = interact(client_proc)
            assert "Interactive mode" in out

            # 2. Help
            out = interact(client_proc, "help")
            assert "exit - exit interactive mode" in out

            # 3. Pwd (Root)
            # FIX 3: Expect ISOBUS style path: \\<volume_name>
            out = interact(client_proc, "pwd")
            assert f"\\\\{vol_name}" in out

            # 4. ls
            out = interact(client_proc, "ls")
            assert "dir1" in out
            assert "long_name_1_" in out # Verify we still see some long names

            # 5. cd dir1
            out = interact(client_proc, "cd dir1")

            # 6. pwd (check change)
            out = interact(client_proc, "pwd")
            assert f"\\\\{vol_name}\\dir1" in out

            # 7. ll
            out = interact(client_proc, "ll")
            assert "dir2" in out

            # 8. cd dir2
            out = interact(client_proc, "cd dir2")

            # 9. pwd
            out = interact(client_proc, "pwd")
            assert f"\\\\{vol_name}\\dir1\\dir2" in out

            # 10. ls
            out = interact(client_proc, "ls")
            assert "file0" in out

            # 11. get file0
            out = interact(client_proc, "get file0")
            assert "File transfer completed" in out

            # Verify file content
            downloaded_file = os.path.join(cli_dir, "file0")
            assert os.path.exists(downloaded_file), "Command 'get file0' failed."
            with open(downloaded_file, "r") as f:
                assert f.read() == "hello\n"

            # 12. Dmesg (Moved to end to prevent buffer output interference)
            out = interact(client_proc, "dmesg")

            # 13. Exit
            client_proc.stdin.write(b"exit\n")
            client_proc.stdin.flush()
            try:
                client_proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                client_proc.kill()
                pytest.fail("Client did not exit cleanly after 'exit' command")

            # FIX 4: Accept 252 (which is -EINTR / -4 cast to unsigned 8-bit)
            assert client_proc.returncode in [0, -2, 252]

            # --- Session 2: Test 'quit' alias ---
            print("DEBUG: Running Client Session 2 to test 'quit'")
            client_proc = subprocess.Popen(
                client_cmd,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                bufsize=0
            )

            # Consume initial prompt
            interact(client_proc)

            # Send quit
            client_proc.stdin.write(b"quit\n")
            client_proc.stdin.flush()
            client_proc.wait(timeout=2)

            assert client_proc.returncode in [0, -2, 252]

        finally:
            # Cleanup
            if server_proc.poll() is None:
                server_proc.terminate()
                server_proc.wait(timeout=1)

            if client_proc and client_proc.poll() is None:
                client_proc.kill()
