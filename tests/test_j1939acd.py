# SPDX-License-Identifier: GPL-2.0-only

import subprocess
import time
import os
import pytest
import signal
import tempfile

# Note: bin_path and interface fixtures are provided implicitly by conftest.py

def wait_for_can_msg(file_obj, pattern, timeout=5.0):
    """
    Helper to poll the log file for a specific pattern.
    Returns True if found within timeout, False otherwise.
    """
    start = time.time()
    while time.time() - start < timeout:
        file_obj.seek(0)
        content = file_obj.read()
        if pattern in content:
            return True
        time.sleep(0.1)
    return False

def test_j1939acd_usage(bin_path):
    """
    Test usage/help output for j1939acd.
    """
    j1939acd = os.path.join(bin_path, "j1939acd")
    if not os.path.exists(j1939acd):
        pytest.skip("j1939acd binary not found")

    result = subprocess.run(
        [j1939acd, "-h"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    assert "Usage: j1939acd" in result.stderr or "Usage: j1939acd" in result.stdout

def test_j1939acd_claiming_lifecycle(bin_path, can_interface):
    """
    Test J1939 Address Claiming procedure (Startup & Shutdown).
    Corresponds to Test Scenario 1 (Happy Flow) and Cleanup.
    """
    j1939acd = os.path.join(bin_path, "j1939acd")
    candump = os.path.join(bin_path, "candump")

    if not os.path.exists(j1939acd):
        pytest.skip("j1939acd binary not found")

    nodename_hex = "1122334455667788"
    # candump -L format uses compact hex strings (no spaces)
    nodename_payload = "8877665544332211"

    with tempfile.NamedTemporaryFile(suffix=".jacd", delete=False) as tmp_cache:
        cache_file = tmp_cache.name

    # Start candump
    dump_out = tempfile.TemporaryFile(mode='w+')
    dump_proc = subprocess.Popen([candump, "-L", can_interface], stdout=dump_out, stderr=subprocess.PIPE, text=True)
    time.sleep(0.5) # Allow candump to initialize

    daemon_proc = None
    try:
        # Start j1939acd with range starting at 80 (0x50).
        # We rely on -r because -a is reportedly not working properly.
        # Range: 80 to 128 (Decimal) -> 0x50 to 0x80.
        cmd_daemon = [j1939acd, "-r", "80-128", "-c", cache_file, nodename_hex, can_interface]
        print(f"DEBUG: Starting j1939acd: {' '.join(cmd_daemon)}")
        daemon_proc = subprocess.Popen(cmd_daemon, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        # Wait for initial claim of 0x50 (which is 80 decimal)
        if not wait_for_can_msg(dump_out, f"18EEFF50#{nodename_payload}", timeout=3.0):
             dump_out.seek(0)
             print(f"DEBUG: Timeout waiting for start. Log:\n{dump_out.read()}")
             pytest.fail("j1939acd did not send Initial Address Claim (0x50) within timeout")

        # Stop
        daemon_proc.send_signal(signal.SIGINT)
        daemon_proc.wait(timeout=2.0)

        # Analyze shutdown
        if not wait_for_can_msg(dump_out, f"18EEFFFE#{nodename_payload}", timeout=3.0):
             dump_out.seek(0)
             print(f"DEBUG: Timeout waiting for stop. Log:\n{dump_out.read()}")
             pytest.fail("j1939acd did not send Address Release (0xFE) after SIGINT")

    finally:
        if daemon_proc and daemon_proc.poll() is None: daemon_proc.kill()
        if dump_proc.poll() is None: dump_proc.terminate()
        dump_out.close()
        if os.path.exists(cache_file): os.remove(cache_file)

def test_j1939acd_conflict_defense(bin_path, can_interface):
    """
    Test Scenario 2: Conflict Resolution - Defense (Winning).
    The DUT (Name: 11...) should defend address 0x50 against a lower priority challenger (Name: FF...).
    """
    j1939acd = os.path.join(bin_path, "j1939acd")
    candump = os.path.join(bin_path, "candump")
    cansend = os.path.join(bin_path, "cansend")

    if not os.path.exists(j1939acd): pytest.skip("j1939acd missing")

    nodename_hex = "1122334455667788"
    dut_payload = "8877665544332211" # Little Endian of DUT Name, compact for candump -L

    # Challenger: Name FFFFFFFFFFFFFFFF (Lower Priority)
    challenger_payload_send = "FFFFFFFFFFFFFFFF"
    challenger_payload_dump = "FFFFFFFFFFFFFFFF"

    with tempfile.NamedTemporaryFile(suffix=".jacd", delete=False) as tmp_cache:
        cache_file = tmp_cache.name

    dump_out = tempfile.TemporaryFile(mode='w+')
    dump_proc = subprocess.Popen([candump, "-L", can_interface], stdout=dump_out, stderr=subprocess.PIPE, text=True)
    time.sleep(0.5)

    daemon_proc = None
    try:
        # 1. Start DUT (Claims 0x50)
        # Using range starting at 80 (decimal) for 0x50
        cmd_daemon = [j1939acd, "-r", "80-128", "-c", cache_file, nodename_hex, can_interface]
        daemon_proc = subprocess.Popen(cmd_daemon, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        # Wait for DUT to actually claim the address BEFORE injecting conflict
        print("DEBUG: Waiting for DUT to claim 0x50...")
        if not wait_for_can_msg(dump_out, f"18EEFF50#{dut_payload}", timeout=3.0):
            dump_out.seek(0)
            print(f"DEBUG: DUT failed to claim 0x50 initially. Log:\n{dump_out.read()}")
            pytest.fail("DUT did not claim preferred address 0x50 on startup")

        # 2. Inject Conflict (Lower Priority)
        # ID 18EEFF50 (Claim for 0x50), Payload F...
        conflict_cmd = [cansend, can_interface, f"18EEFF50#{challenger_payload_send}"]
        print(f"DEBUG: Injecting Conflict (Defense): {' '.join(conflict_cmd)}")
        subprocess.run(conflict_cmd, check=True)

        # 3. Wait for Defense (DUT should re-send claim)
        time.sleep(1.0)

        # Analyze
        dump_proc.terminate()
        dump_proc.wait()
        dump_out.seek(0)
        log = dump_out.read()

        lines = log.splitlines()
        injection_index = -1
        defense_index = -1

        for i, line in enumerate(lines):
            # Check for Challenger Frame
            if "18EEFF50" in line and challenger_payload_dump in line:
                injection_index = i
            # Check for DUT Frame
            elif "18EEFF50" in line and dut_payload in line:
                # We want a DUT claim that appears AFTER the injection
                if injection_index != -1 and i > injection_index:
                    defense_index = i

        if injection_index == -1:
             print(f"DEBUG: Log content:\n{log}")
             pytest.fail("Injected conflict frame not found in candump")

        if defense_index == -1:
             print(f"DEBUG: Log content:\n{log}")
             pytest.fail("DUT did not defend address (no Claim 0x50 sent after injection)")

    finally:
        if daemon_proc: daemon_proc.send_signal(signal.SIGINT); daemon_proc.wait()
        if dump_proc.poll() is None: dump_proc.terminate()
        dump_out.close()
        if os.path.exists(cache_file): os.remove(cache_file)

def test_j1939acd_conflict_yielding(bin_path, can_interface):
    """
    Test Scenario 3: Conflict Resolution - Yielding (Losing).
    The DUT (Name: 11...) should yield address 0x50 to a higher priority challenger (Name: 01...)
    and claim a new address (e.g., 0x51).
    """
    j1939acd = os.path.join(bin_path, "j1939acd")
    candump = os.path.join(bin_path, "candump")
    cansend = os.path.join(bin_path, "cansend")

    if not os.path.exists(j1939acd): pytest.skip("j1939acd missing")

    nodename_hex = "1122334455667788"
    dut_payload = "8877665544332211"

    # Challenger: Name 0000000000000001 (Higher Priority)
    challenger_payload_send = "0100000000000000"
    challenger_payload_dump = "0100000000000000"

    with tempfile.NamedTemporaryFile(suffix=".jacd", delete=False) as tmp_cache:
        cache_file = tmp_cache.name

    dump_out = tempfile.TemporaryFile(mode='w+')
    dump_proc = subprocess.Popen([candump, "-L", can_interface], stdout=dump_out, stderr=subprocess.PIPE, text=True)
    time.sleep(0.5)

    daemon_proc = None
    try:
        # 1. Start DUT (Claims 0x50)
        # Using decimal 80 for 0x50. Range 80-96.
        cmd_daemon = [j1939acd, "-r", "80-96", "-c", cache_file, nodename_hex, can_interface]
        daemon_proc = subprocess.Popen(cmd_daemon, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        # Wait for DUT to actually claim 0x50 BEFORE injecting
        print("DEBUG: Waiting for DUT to claim 0x50...")
        if not wait_for_can_msg(dump_out, f"18EEFF50#{dut_payload}", timeout=3.0):
            dump_out.seek(0)
            print(f"DEBUG: DUT failed to claim 0x50 initially. Log:\n{dump_out.read()}")
            pytest.fail("DUT did not claim preferred address 0x50 on startup")

        # 2. Inject Conflict (Higher Priority)
        conflict_cmd = [cansend, can_interface, f"18EEFF50#{challenger_payload_send}"]
        print(f"DEBUG: Injecting Conflict (Yielding): {' '.join(conflict_cmd)}")
        subprocess.run(conflict_cmd, check=True)

        time.sleep(1.0) # Wait for DUT response (New Claim)

        # Analyze
        dump_proc.terminate()
        dump_proc.wait()
        dump_out.seek(0)
        log = dump_out.read()

        lines = log.splitlines()
        injection_index = -1
        new_claim_index = -1

        for i, line in enumerate(lines):
            if "18EEFF50" in line and challenger_payload_dump in line:
                injection_index = i
            # Look for DUT claiming a NEW address (e.g. 51 hex / 81 dec)
            # Frame 18EEFF51
            elif "18EEFF51" in line and dut_payload in line:
                if injection_index != -1 and i > injection_index:
                    new_claim_index = i

        if injection_index == -1:
             print(f"DEBUG: Log content:\n{log}")
             pytest.fail("Injected conflict frame not found in candump")

        if new_claim_index == -1:
             print(f"DEBUG: Log content:\n{log}")
             pytest.fail("DUT did not claim new address (0x51) after yielding 0x50")

    finally:
        if daemon_proc: daemon_proc.send_signal(signal.SIGINT); daemon_proc.wait()
        if dump_proc.poll() is None: dump_proc.terminate()
        dump_out.close()
        if os.path.exists(cache_file): os.remove(cache_file)
