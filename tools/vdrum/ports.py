"""Serial port helpers: find the Virtual Drum receiver by its USB ids (never a hard-coded port)."""

from __future__ import annotations

from typing import Optional

from serial.tools import list_ports

from .protocol import USB_RECEIVER_PID, USB_RECEIVER_VID


def list_serial_ports() -> list:
    """All serial ports as (device, vid, pid, description); vid/pid are None if unknown."""
    return [(p.device, p.vid, p.pid, p.description or "") for p in list_ports.comports()]


def find_receiver_port() -> Optional[str]:
    """Device name of the first port with the receiver's USB ids ("COM5", "/dev/cu.usbmodem...")."""
    for device, vid, pid, _ in list_serial_ports():
        if vid == USB_RECEIVER_VID and pid == USB_RECEIVER_PID:
            return device
    return None


def describe_ports() -> str:
    lines = []
    for device, vid, pid, description in list_serial_ports():
        ids = f"{vid:04X}:{pid:04X}" if vid is not None and pid is not None else "no USB ids"
        lines.append(f"  {device:<24} {ids:<11} {description}")
    return "\n".join(lines) if lines else "  (no serial port found)"
