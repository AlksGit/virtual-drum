"""Flash a stick over the air of the receiver: no double tap on the reset button.

    python tools/flash_stick.py --stick left --uf2 build/h0/stick/zephyr/zephyr.uf2
    python tools/flash_stick.py --stick right --uf2 build/h1/stick/zephyr/zephyr.uf2
    python tools/flash_stick.py --stick left --bootloader-only   # just open the UF2 drive

The receiver relays a CMD_ENTER_BOOTLOADER command to the stick in an ESB ACK payload
(docs/PROTOCOL.md, v5); the stick restarts into its UF2 bootloader, whose drive (XIAO-SENSE)
appears on the computer the stick is plugged into. The script then copies the UF2 file to it,
and the stick restarts on the new firmware.

The stick must be powered and in range, running firmware 3 or later (first install: one last
double tap). It must be plugged into this computer by USB for the drive to appear.
"""

from __future__ import annotations

import argparse
import ctypes
import os
import shutil
import sys
import time
from pathlib import Path

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent))

import serial  # noqa: E402

from vdrum.ports import describe_ports, find_receiver_port  # noqa: E402
from vdrum.protocol import CMD_ENTER_BOOTLOADER, encode_command_frame  # noqa: E402

DRIVE_LABEL = "XIAO-SENSE"
STICKS = {"left": 0, "right": 1, "0": 0, "1": 1}
COMMAND_ATTEMPTS = 3
WAIT_PER_ATTEMPT_S = 4.0


def uf2_drives() -> set:
    """Mounted UF2 bootloader drives of the XIAO Sense, on Windows, macOS or Linux."""
    if sys.platform == "win32":
        drives = set()
        mask = ctypes.windll.kernel32.GetLogicalDrives()
        for i in range(26):
            if not mask & (1 << i):
                continue
            root = f"{chr(ord('A') + i)}:\\"
            label = ctypes.create_unicode_buffer(261)
            ok = ctypes.windll.kernel32.GetVolumeInformationW(root, label, len(label), None, None, None, None, 0)
            if ok and label.value == DRIVE_LABEL:
                drives.add(Path(root))
        return drives
    user = os.environ.get("USER", "")
    candidates = [Path("/Volumes"), Path("/media") / user, Path("/run/media") / user]
    return {p for base in candidates if base.is_dir() for p in base.glob(f"{DRIVE_LABEL}*")}


def enter_bootloader(port: str, stick_id: int) -> Path | None:
    """Sends the command until a new UF2 drive appears; returns it, or None."""
    before = uf2_drives()
    frame = encode_command_frame(stick_id, CMD_ENTER_BOOTLOADER)
    with serial.Serial(port, timeout=0.1) as link:
        for attempt in range(1, COMMAND_ATTEMPTS + 1):
            link.write(frame)
            link.flush()
            print(f"command sent to stick {stick_id} (attempt {attempt}), waiting for the {DRIVE_LABEL} drive...")
            deadline = time.monotonic() + WAIT_PER_ATTEMPT_S
            while time.monotonic() < deadline:
                link.reset_input_buffer()  # the receiver keeps streaming packets
                new = uf2_drives() - before
                if new:
                    return sorted(new)[0]
                time.sleep(0.2)
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--stick", required=True, choices=sorted(STICKS), help="left (0) or right (1)")
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--uf2", type=Path, help="firmware to copy (zephyr.uf2)")
    target.add_argument("--bootloader-only", action="store_true", help="only open the UF2 drive")
    parser.add_argument("--port", help="receiver serial port (default: auto-detect by USB ids)")
    args = parser.parse_args()

    if args.uf2 is not None and not args.uf2.is_file():
        print(f"not found: {args.uf2}", file=sys.stderr)
        return 1
    port = args.port or find_receiver_port()
    if port is None:
        print("receiver not found. Serial ports:\n" + describe_ports(), file=sys.stderr)
        return 1

    drive = enter_bootloader(port, STICKS[args.stick])
    if drive is None:
        print(
            f"no {DRIVE_LABEL} drive appeared: is the stick on, in range, plugged into this computer by USB, "
            "and running firmware 3 or later?",
            file=sys.stderr,
        )
        return 1
    print(f"bootloader drive: {drive}")
    if args.bootloader_only:
        return 0

    time.sleep(1.0)  # let the drive settle before writing
    try:
        shutil.copyfile(args.uf2, drive / args.uf2.name)
    except OSError as error:
        # The bootloader may restart the board (drive gone) before the copy call returns.
        if drive.exists():
            print(f"copy failed: {error}", file=sys.stderr)
            return 1
    print(f"flashed {args.uf2} to stick {args.stick}; it restarts on the new firmware")
    return 0


if __name__ == "__main__":
    sys.exit(main())
