#!/usr/bin/env python3
"""
Test the push-up counter Arduino firmware protocol via /dev/ttyUSB0.

Usage: sudo python3 ArduinoProtocolTest.py

Tests what we can reliably verify:
- PING → PONG
- TEST → SET + STATE SEARCHING_BACK (+ full WORK sequence when waiting)
- START with 4 args → STATE ERROR BAD_START
- Unknown command → STATE ERROR UNKNOWN_CMD
"""

import os
import termios
import select
import time
import sys

DEVICE = "/dev/ttyUSB0"

def open_serial():
    fd = os.open(DEVICE, os.O_RDWR | os.O_NOCTTY)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0
    attrs[1] = 0
    attrs[2] &= ~(termios.PARENB | termios.CSTOPB | termios.CSIZE)
    attrs[2] |= termios.CLOCAL | termios.CREAD | termios.CS8
    attrs[3] &= ~(termios.ICANON | termios.ECHO | termios.ISIG)
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 2
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd

def drain(fd, timeout=0.5):
    deadline = time.time() + timeout
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r:
            break
        try:
            os.read(fd, 256)
        except OSError:
            break

def wait_for_boot(fd, timeout=8.0):
    lines = []
    deadline = time.time() + timeout
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.05)
        if not r:
            continue

        data = os.read(fd, 256)
        for l in data.decode(errors='replace').split('\n'):
            s = l.strip()
            if s and not s.startswith('DIST'):
                lines.append(s)
                if s.startswith('HELLO') or s.startswith('STATE '):
                    return lines

    return lines

def cmd(fd, cmd_str, timeout=1.0, skip_dist=True):
    """Send command, return non-DIST responses within timeout."""
    os.write(fd, cmd_str.encode() + b'\n')
    time.sleep(0.05)
    lines = []
    deadline = time.time() + timeout
    while time.time() < deadline:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            data = os.read(fd, 256)
            for l in data.decode(errors='replace').split('\n'):
                s = l.strip()
                if s and (not skip_dist or not s.startswith('DIST')):
                    lines.append(s)
        elif lines:
            break
    return lines

def check(label, got, expected):
    ok = (got == expected)
    if ok:
        print(f"  [PASS] {label}")
    else:
        print(f"  [FAIL] {label}")
        print(f"         Expected: {expected}")
        print(f"         Got:      {got}")
    return ok

def contains(fd, cmd_str, substrings, timeout=1.0, skip_dist=True):
    """Send command, check all substrings appear in response."""
    r = cmd(fd, cmd_str, timeout=timeout, skip_dist=skip_dist)
    all_found = all(any(s in x for x in r) for s in substrings)
    if all_found:
        print(f"  [PASS] {' + '.join(substrings)}")
    else:
        print(f"  [FAIL] {' + '.join(substrings)}")
        print(f"         Got: {r}")
    return all_found

def main():
    if os.geteuid() != 0:
        print("Error: must run as root (sudo)", file=sys.stderr)
        sys.exit(1)

    print(f"Opening {DEVICE}")
    fd = open_serial()
    boot = wait_for_boot(fd, timeout=8.0)
    if boot:
        print(f"Boot: {boot}")
    drain(fd, timeout=0.5)

    results = []

    # 1. PING → PONG
    print("\n=== 1. PING ===")
    drain(fd, 0.3)
    results.append(check("PING", cmd(fd, "PING", timeout=1.0), ["PONG"]))

    # 2. TEST → SET + SEARCHING_BACK immediately
    print("\n=== 2. TEST command ===")
    drain(fd, 0.3)
    r = cmd(fd, "TEST 11111 3 100", timeout=0.5)
    ok = "SET 1 3 3" in r and "STATE SEARCHING_BACK" in r
    if ok:
        print(f"  [PASS] SET 1 3 3 + STATE SEARCHING_BACK (got: {r})")
    else:
        print(f"  [FAIL] SET + SEARCHING_BACK (got: {r})")
    results.append(ok)

    # Wait for full sequence: 250ms delay + 3*100ms reps = ~600ms
    # Read everything without intervening drain()
    time.sleep(0.9)
    r2 = []
    deadline = time.time() + 1.0
    while time.time() < deadline:
        r_, _, _ = select.select([fd], [], [], 0.05)
        if r_:
            data = os.read(fd, 256)
            for l in data.decode(errors='replace').split('\n'):
                s = l.strip()
                if s and not s.startswith('DIST'):
                    r2.append(s)
        elif r2:
            break
    ok2 = all(s in r2 for s in ["STATE WORK", "REP 1", "REP 3", "STATE COMPLETE"])
    if ok2:
        print(f"  [PASS] Full sequence: STATE WORK + REP 1..3 + STATE COMPLETE")
    else:
        print(f"  [FAIL] Full sequence (got: {r2})")
    results.append(ok2)
    drain(fd, 0.3)

    # 3. START with 4 args → BAD_START
    print("\n=== 3. START with insufficient args ===")
    drain(fd, 0.3)
    results.append(check("START 4 args → BAD_START",
                        cmd(fd, "START 1 2 3 4", timeout=0.5),
                        ["STATE ERROR BAD_START"]))
    drain(fd, 0.3)

    # 4. Unknown command → UNKNOWN_CMD
    print("\n=== 4. Unknown command ===")
    drain(fd, 0.3)
    results.append(check("UNKNOWN → ERROR",
                        cmd(fd, "JUNK CMD", timeout=0.5),
                        ["STATE ERROR UNKNOWN_CMD"]))

    # 5. PING still alive
    print("\n=== 5. PING (still alive) ===")
    drain(fd, 0.3)
    results.append(check("PING", cmd(fd, "PING", timeout=1.0), ["PONG"]))

    os.close(fd)

    print("\n" + "=" * 50)
    passed = sum(results)
    total = len(results)
    print(f"Results: {passed}/{total} passed")
    if passed == total:
        print("All Arduino protocol checks PASSED")
    else:
        print("Some checks FAILED")
    return 0 if passed == total else 1

if __name__ == "__main__":
    sys.exit(main())
