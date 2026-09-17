#!/usr/bin/env python3
"""
ESP32 CH32V003 Binary Flasher Tool
--------------------------------------
Host workflow utility for programming and verifying binary firmware images
on the WCH CH32V003 microcontroller via the ESP32 programmer.

Usage:
  python tools/ch32_flash.py --port /dev/ttyUSB0 --bin firmware.bin [--addr 0x08000000] [--reset]
  python tools/ch32_flash.py --port /dev/ttyUSB0 [--bin firmware.bin] --monitor
"""

import argparse
import os
import re
import sys
import time
import serial

CH32V003_FLASH_BASE = 0x08000000
CH32V003_FLASH_SIZE = 16 * 1024  # 16 KB
DEFAULT_BIN_PATH = os.path.join("firmware", "ch32_blink", "blink.bin")

# The programmer's serial CLI caps each flash/verify command at 512 bytes of
# hex-decoded payload (payloadBuffer[512] in the ESP32 firmware). Larger images
# are therefore sent in multiple commands. This must be a multiple of the
# CH32V003 flash page size (64 bytes) so chunk boundaries stay page-aligned and
# no page is erased/reprogrammed twice across chunk boundaries.
CHUNK_SIZE = 512
DEBUG_MONITOR_EXIT = b"\x1d"  # Ctrl-]
PROGRESS_PATTERN = re.compile(
    r"^\s*Progress:\s*(\d+)/(\d+) bytes \(at (0x[0-9A-Fa-f]+)\)\s*$"
)
QUIET_PROGRAMMER_PREFIXES = (
    "FLASH_CTLR before page erase:",
    "FLASH_STATR after page erase wait:",
)


class ProgressBar:
    """Single-line animated progress display with a plain-text fallback."""

    def __init__(self, label, total, force_ascii=False):
        self.label = label
        self.total = total
        self.completed = 0
        self.address = None
        self.started = time.monotonic()
        self.last_render = 0.0
        self.rendered_width = 0
        self.spinner_index = 0
        self.last_plain_percent = -10
        self.interactive = sys.stdout.isatty()
        self.unicode = not force_ascii and self._supports_unicode()
        self.spinner = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏" if self.unicode else "|/-\\"
        self.full = "█" if self.unicode else "#"
        self.empty = "░" if self.unicode else "-"

    @staticmethod
    def _supports_unicode():
        encoding = sys.stdout.encoding or "ascii"
        try:
            "✓█░⠋".encode(encoding)
            return True
        except UnicodeEncodeError:
            return False

    def _line(self, final=None):
        ratio = min(1.0, self.completed / self.total) if self.total else 1.0
        width = 28
        filled = int(ratio * width)
        bar = self.full * filled + self.empty * (width - filled)
        elapsed = max(time.monotonic() - self.started, 0.001)
        rate = self.completed / elapsed
        address = f"0x{self.address:08X}" if self.address is not None else "----------"

        if final is None:
            icon = self.spinner[self.spinner_index % len(self.spinner)]
            self.spinner_index += 1
        elif final:
            icon = "✓" if self.unicode else "OK"
        else:
            icon = "✗" if self.unicode else "!!"

        return (
            f"{icon} {self.label:<7} [{bar}] {ratio * 100:6.2f}%  "
            f"{self.completed:>5}/{self.total:<5}  {address}  {rate:6.0f} B/s"
        )

    def _write_interactive(self, line, newline=False):
        padding = " " * max(0, self.rendered_width - len(line))
        sys.stdout.write("\r" + line + padding + ("\n" if newline else ""))
        sys.stdout.flush()
        self.rendered_width = 0 if newline else len(line)

    def tick(self, force=False):
        now = time.monotonic()
        if not self.interactive or (not force and now - self.last_render < 0.08):
            return
        self.last_render = now
        self._write_interactive(self._line())

    def update(self, completed, address):
        self.completed = min(completed, self.total)
        self.address = address
        if self.interactive:
            self.tick(force=True)
            return

        percent = int(self.completed * 100 / self.total) if self.total else 100
        if self.completed != self.total and percent >= self.last_plain_percent + 10:
            self.last_plain_percent = percent
            print(self._line())

    def message(self, line):
        if self.interactive and self.rendered_width:
            self._write_interactive("", newline=True)
        print(line)
        self.tick(force=True)

    def finish(self, success):
        if success:
            self.completed = self.total
        line = self._line(final=success)
        if self.interactive:
            self._write_interactive(line, newline=True)
        elif self.completed != self.total or self.last_plain_percent < 100:
            print(line)


def _send_and_read_until(serial_port, command, ok_marker, timeout=30.0,
                         progress=None, progress_base=0):
    """Send a command and wait until its marker or a period of inactivity."""
    # Write in small pieces with a short delay so the ESP32's serial RX buffer
    # (much smaller than a 512-byte hex command) is not overrun. Overrun drops
    # the trailing bytes (including the terminating newline), so the command
    # would never execute.
    for i in range(0, len(command), 64):
        serial_port.write(command[i:i + 64])
        time.sleep(0.001)

    out = ""
    pending = ""
    last_activity = time.monotonic()
    while time.monotonic() - last_activity < timeout:
        if serial_port.in_waiting:
            chunk = serial_port.read(serial_port.in_waiting).decode(
                "utf-8", errors="ignore"
            )
            out += chunk
            pending += chunk
            last_activity = time.monotonic()

            while "\n" in pending:
                line, pending = pending.split("\n", 1)
                line = line.rstrip("\r")
                match = PROGRESS_PATTERN.match(line)
                if progress is not None and match:
                    progress.update(
                        progress_base + int(match.group(1)),
                        int(match.group(3), 16),
                    )
                elif progress is not None and (
                    ok_marker in line or
                    line.startswith(QUIET_PROGRAMMER_PREFIXES)
                ):
                    pass
                elif line:
                    if progress is not None:
                        progress.message(line)
                    else:
                        print(line)
        if ok_marker in out:
            break
        if progress is not None:
            progress.tick()
        time.sleep(0.05)

    if pending and ok_marker not in pending:
        if progress is not None:
            progress.message(pending.rstrip("\r"))
        else:
            sys.stdout.write(pending)
            sys.stdout.flush()
    return out


def _monitor_debug_output(serial_port):
    """Forward target DEBUGPRINTF bytes until the user presses Ctrl-C."""
    print("\n=== CH32 DEBUG MONITOR (Ctrl-C to exit) ===")
    serial_port.timeout = 0.1
    try:
        while True:
            chunk = serial_port.read(serial_port.in_waiting or 1)
            if chunk:
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
    except KeyboardInterrupt:
        serial_port.write(DEBUG_MONITOR_EXIT)
        serial_port.flush()
        time.sleep(0.1)
        while serial_port.in_waiting:
            chunk = serial_port.read(serial_port.in_waiting)
            sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()
        print("\nDebug monitor closed.")


def main():
    parser = argparse.ArgumentParser(description="ESP32 CH32V003 Binary Flasher Tool")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="Serial port (default: /dev/ttyUSB0)")
    parser.add_argument("--bin", help="Binary to flash before reset/monitor (optional with --monitor)")
    parser.add_argument("--addr", default="0x08000000", help="Target flash memory address (default: 0x08000000)")
    parser.add_argument("--reset", action="store_true", help="Execute target reset/run after flashing and verification")
    parser.add_argument("--monitor", action="store_true", help="Reset/run and continuously forward CH32 FUNCONF_USE_DEBUGPRINTF output")
    parser.add_argument("--ascii", action="store_true", help="Use an ASCII progress bar instead of Unicode")
    args = parser.parse_args()

    # Preserve the original default image for normal flashing, while allowing
    # `--monitor` by itself to attach to firmware already present on the CH32.
    should_flash = args.bin is not None or not args.monitor
    if args.bin is None and should_flash:
        args.bin = DEFAULT_BIN_PATH

    data = b""
    file_size = 0
    addr_val = CH32V003_FLASH_BASE
    if should_flash:
        if not os.path.exists(args.bin):
            print(f"Error: Binary file not found at '{args.bin}'", file=sys.stderr)
            sys.exit(1)

        try:
            with open(args.bin, "rb") as f:
                data = f.read()
        except Exception as e:
            print(f"Error: Failed to read binary file '{args.bin}': {e}", file=sys.stderr)
            sys.exit(1)

        file_size = len(data)
        if file_size == 0:
            print(f"Error: Binary file '{args.bin}' is empty (0 bytes).", file=sys.stderr)
            sys.exit(1)

        try:
            addr_val = int(args.addr, 16)
        except ValueError:
            print(f"Error: Invalid hex address string '{args.addr}'", file=sys.stderr)
            sys.exit(1)

        print(f"Loaded binary '{args.bin}': {file_size} bytes")
        print(f"Target memory address: 0x{addr_val:08X}")

        if addr_val < CH32V003_FLASH_BASE or (addr_val + file_size) > (CH32V003_FLASH_BASE + CH32V003_FLASH_SIZE):
            print(
                f"Error: Binary bounds (0x{addr_val:08X} - 0x{(addr_val + file_size):08X}) "
                f"exceed CH32V003 16KB flash memory (0x{CH32V003_FLASH_BASE:08X} - 0x{(CH32V003_FLASH_BASE + CH32V003_FLASH_SIZE):08X}).",
                file=sys.stderr
            )
            sys.exit(1)

        if addr_val % 64 != 0:
            print(
                f"Error: --addr 0x{addr_val:08X} is not 64-byte (flash page) aligned. "
                f"Chunked flashing requires a page-aligned start address.",
                file=sys.stderr
            )
            sys.exit(1)

    # 3. Connect to the ESP32 programmer over serial
    print(f"Connecting to ESP32 programmer on port {args.port} (115200 baud)...")
    try:
        s = serial.Serial(args.port, 115200, timeout=3)
    except Exception as e:
        print(f"Error: Failed to open serial port {args.port}: {e}", file=sys.stderr)
        sys.exit(1)

    s.dtr = False
    s.rts = True
    time.sleep(0.1)
    s.rts = False
    time.sleep(0.5)

    startup_out = ""
    t_end = time.time() + 3
    while time.time() < t_end:
        if s.in_waiting:
            startup_out += s.read(s.in_waiting).decode("utf-8", errors="ignore")
        time.sleep(0.05)

    print("=== PROGRAMMER INITIALIZATION LOG ===")
    print(startup_out.strip())

    # 4. Target detection check
    if "Target detect: OK" not in startup_out:
        # Re-send explicit detect command if startup trace was truncated
        s.write(b"detect\n")
        detect_out = ""
        t_end = time.time() + 2
        while time.time() < t_end:
            if s.in_waiting:
                detect_out += s.read(s.in_waiting).decode("utf-8", errors="ignore")
            time.sleep(0.05)
        if "Target detect: OK" not in detect_out:
            print("Error: Target CH32V003 not detected on programmer!", file=sys.stderr)
            s.close()
            sys.exit(1)

    print("\n[STEP 1] Target Detection: OK")

    if should_flash:
        # 5. Program binary payload via targetProgramBinary() in page-aligned chunks
        print(f"\n[STEP 2] Programming target binary via targetProgramBinary() "
              f"({file_size} bytes, {CHUNK_SIZE}-byte chunks)...")
        flash_bar = ProgressBar("FLASH", file_size, args.ascii)
        flash_bar.tick(force=True)
        for offset in range(0, file_size, CHUNK_SIZE):
            chunk = data[offset:offset + CHUNK_SIZE]
            chunk_addr = addr_val + offset
            flash_cmd = f"flash {chunk_addr:08X} {chunk.hex()}\n".encode("utf-8")
            flash_out = _send_and_read_until(
                s,
                flash_cmd,
                "flash: OK",
                progress=flash_bar,
                progress_base=offset,
            )
            if "flash: OK" not in flash_out:
                flash_bar.finish(False)
                print(f"\nError: Programming chunk @ 0x{chunk_addr:08X} failed!", file=sys.stderr)
                s.close()
                sys.exit(1)
            flash_bar.update(
                offset + len(chunk), chunk_addr + len(chunk) - 1
            )
        flash_bar.finish(True)

        print("\n[STEP 2 RESULT] targetProgramBinary: OK")

        # 6. Verify programmed binary via targetVerifyBinary() in the same chunks
        print(f"\n[STEP 3] Verifying programmed memory via targetVerifyBinary() ({file_size} bytes)...")
        verify_bar = ProgressBar("VERIFY", file_size, args.ascii)
        verify_bar.tick(force=True)
        for offset in range(0, file_size, CHUNK_SIZE):
            chunk = data[offset:offset + CHUNK_SIZE]
            chunk_addr = addr_val + offset
            verify_cmd = f"verify {chunk_addr:08X} {chunk.hex()}\n".encode("utf-8")
            verify_out = _send_and_read_until(
                s,
                verify_cmd,
                "verify: OK",
                progress=verify_bar,
                progress_base=offset,
            )
            if "verify: OK" not in verify_out:
                verify_bar.finish(False)
                print(f"\nError: Verification chunk @ 0x{chunk_addr:08X} failed!", file=sys.stderr)
                s.close()
                sys.exit(1)
            verify_bar.update(
                offset + len(chunk), chunk_addr + len(chunk) - 1
            )
        verify_bar.finish(True)

        print("\n[STEP 3 RESULT] targetVerifyBinary: OK")
        print("\n=== FLASHING & VERIFICATION SUMMARY ===")
        print("SUCCESS: Firmware successfully programmed and verified on CH32V003!")
    else:
        print("\nMonitor-only mode: keeping the existing CH32 firmware.")

    # 7. Optional debug monitor. The programmer enters monitor mode before it
    # resets/runs the target, so early SystemInit()/printf output is not lost.
    if args.monitor:
        monitor_step = 4 if should_flash else 2
        print(f"\n[STEP {monitor_step}] Starting CH32 debug monitor...")
        monitor_out = _send_and_read_until(
            s, b"monitor\n", "Debug monitor: active", timeout=3.0
        )
        if "Debug monitor: active" not in monitor_out:
            print("Error: Failed to start CH32 debug monitor!", file=sys.stderr)
            s.close()
            sys.exit(1)
        _monitor_debug_output(s)
    elif args.reset:
        print("\n[STEP 4] Executing explicit targetResetRun()...")
        reset_out = _send_and_read_until(s, b"reset\n", "Reset/run: OK", timeout=3.0)
        if "Reset/run: OK" in reset_out:
            print("[STEP 4 RESULT] targetResetRun: OK (Target execution resumed)")
        else:
            print("Error: Target reset/run failed!", file=sys.stderr)
            s.close()
            sys.exit(1)
    else:
        print("Note: Target remains in debug halt state. (Pass --reset or --monitor to execute binary).")

    s.close()
    sys.exit(0)

if __name__ == "__main__":
    main()
