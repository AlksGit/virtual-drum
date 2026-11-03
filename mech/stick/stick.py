"""Printed drumstick holding a XIAO nRF52840 Sense, in two parts plus a butt cap.

Run: python mech/stick/stick.py  ->  mech/stick/out/{handle,shaft,cap}.{stl,step}, plus short
test_* pieces to tune the fits.

Stick frame (matches docs/PROTOCOL.md): X along the stick, from the butt (X = 0) towards the
tip; Z is the top side of the board (LEDs, reset button); Y completes a right-handed frame.

Board dimensions come from the vendor model `mech/vendor/XIAO-nRF52840 v15.step` (not
committed: unclear licence). When it is present, the script places the board in the handle
and checks that nothing intersects.
"""

from pathlib import Path

import cadquery as cq

HERE = Path(__file__).resolve().parent
OUT = HERE / "out"
VENDOR_STEP = HERE.parent / "vendor" / "XIAO-nRF52840 v15.step"

# --- Board, read from the vendor STEP (board frame: x along the board, USB-C at +x,
# y = thickness, z = width) ---------------------------------------------------------------
PCB_X = (-8.67, 12.28)  # PCB edges; the USB-C side is +x
PCB_Z = (-15.00, 2.78)  # 17.78 mm wide
BOARD_Y = (-0.25, 3.00)  # bottom pads to the top of the RF shield (tallest part but USB-C)
USB_X0 = 6.51  # inner end of the USB-C receptacle (it overhangs the PCB to x = 13.81)
USB_Y = (0.01, 4.21)
USB_Z = (-10.59, -1.65)
RESET_XZ = (10.41, -11.83)  # centre of the reset button (SKTAAAE010), top side
LEDS_X = (8.99, 11.97)  # user RGB LED and charge LED, top side
LEDS_Z = (-1.04, 0.23)

# --- Fits (mm) ------------------------------------------------------------------------
CLEARANCE = 0.15  # per side, board in its pocket
TOP_GAP = 0.35  # above the RF shield
BOTTOM_GAP = 1.0  # under the PCB: room for 1 mm double-sided foam tape that holds the board
USB_CLEARANCE = 0.3
PEG_CLEARANCE = 0.1  # radial, cap pegs in their holes (friction fit)
JOINT_CLEARANCE = 0.15  # radial, shaft spigot in the handle socket (glued)

# --- Stick shape (mm) -----------------------------------------------------------------
LENGTH = 406.0  # a 7A stick
SPLIT_X = 215.0  # handle | shaft; both parts stay under 220 mm to print
CAP_T = 1.6  # butt cap plate; the PCB edge rests against it
BUTT_A, BUTT_B = 11.25, 7.25  # semi-axes of the oval butt (Y, Z) around the board
BUTT_END_X = 28.0  # oval section up to here
ROUND_X = 62.0  # round from here
R = 7.5  # 15 mm stick
TAPER_X = 260.0
TIP_R = 4.0  # end of the taper, inside the tip bead
TIP_LEN, TIP_R_BEAD = 7.0, 5.2  # ellipsoid semi-axes of the tip bead
GROOVE_X, GROOVE_W, GROOVE_D = 70.0, 3.0, 0.8  # zip tie that holds the USB cable along the stick
TEST_BUTT_X = 32.0  # test pieces: board pocket, and both sides of the joint
TEST_JOINT_MARGIN = 8.0

PEG_R, PEG_LEN = 1.2, 5.0
PEG_YZ = ((3.0, -4.5), (-3.0, -4.5))
# The flat keys the orientation and vents the glue. It is on top (+Z): printed flat, the socket
# roof is then a short bridge instead of a sagging arch.
SPIGOT_R, SPIGOT_LEN, SPIGOT_FLAT = 4.5, 25.0, 3.7

# Board frame -> stick frame: rotation (x -> -X, y -> Z, z -> Y), PCB USB edge on the cap.
BOARD_DX = CAP_T + PCB_X[1]
BOARD_DY = -(PCB_Z[0] + PCB_Z[1]) / 2
POCKET_Y0 = BOARD_Y[0] - BOTTOM_GAP
POCKET_Y1 = BOARD_Y[1] + TOP_GAP
BOARD_DZ = -(POCKET_Y0 + POCKET_Y1) / 2  # pocket centred on the stick axis


def to_stick(x, y, z):
    return (BOARD_DX - x, z + BOARD_DY, y + BOARD_DZ)


def box(x0, x1, y0, y1, z0, z1):
    x0, x1 = sorted((x0, x1))
    y0, y1 = sorted((y0, y1))
    z0, z1 = sorted((z0, z1))
    return cq.Solid.makeBox(x1 - x0, y1 - y0, z1 - z0, pnt=cq.Vector(x0, y0, z0))


def board_box(x0, x1, y0, y1, z0, z1):
    """Axis-aligned box given in board coordinates."""
    a = to_stick(x0, y0, z0)
    b = to_stick(x1, y1, z1)
    return box(a[0], b[0], a[1], b[1], a[2], b[2])


def section(x, a, b):
    if abs(a - b) < 1e-6:
        return cq.Wire.makeCircle(a, cq.Vector(x, 0, 0), cq.Vector(1, 0, 0))
    return cq.Wire.makeEllipse(a, b, cq.Vector(x, 0, 0), cq.Vector(1, 0, 0), cq.Vector(0, 1, 0))


def cylinder_x(r, x0, x1, y=0.0, z=0.0):
    return cq.Solid.makeCylinder(r, x1 - x0, cq.Vector(x0, y, z), cq.Vector(1, 0, 0))


def outer_body():
    butt = cq.Solid.makeLoft([section(CAP_T, BUTT_A, BUTT_B), section(BUTT_END_X, BUTT_A, BUTT_B)], True)
    mid_x = (BUTT_END_X + ROUND_X) / 2
    transition = cq.Solid.makeLoft(
        [
            section(BUTT_END_X, BUTT_A, BUTT_B),
            section(mid_x, (BUTT_A + R) / 2 - 0.4, (BUTT_B + R) / 2),
            section(ROUND_X, R, R),
        ]
    )
    shaft = cylinder_x(R, ROUND_X, TAPER_X)
    tip_x = LENGTH - TIP_LEN
    taper = cq.Solid.makeLoft([section(TAPER_X, R, R), section(tip_x - 3.0, TIP_R, TIP_R)], True)
    bead = cq.Solid.makeSphere(1.0, angleDegrees1=-90, angleDegrees2=90).transformGeometry(
        cq.Matrix([[TIP_LEN, 0, 0, tip_x], [0, TIP_R_BEAD, 0, 0], [0, 0, TIP_R_BEAD, 0]])
    )
    body = butt.fuse(transition, shaft, taper, bead).clean()
    groove = cylinder_x(R + 1, GROOVE_X, GROOVE_X + GROOVE_W).cut(
        cylinder_x(R - GROOVE_D, GROOVE_X, GROOVE_X + GROOVE_W)
    )
    return body.cut(groove)


def board_cavity():
    pcb_and_parts = board_box(
        PCB_X[0] - 2 * CLEARANCE, PCB_X[1] + 1.0,  # open towards the cap
        POCKET_Y0, POCKET_Y1,
        PCB_Z[0] - CLEARANCE, PCB_Z[1] + CLEARANCE,
    )
    usb = board_box(
        USB_X0 - USB_CLEARANCE, PCB_X[1] + 1.0,
        POCKET_Y0, USB_Y[1] + USB_CLEARANCE,
        USB_Z[0] - USB_CLEARANCE, USB_Z[1] + USB_CLEARANCE,
    )
    rx, ry, _ = to_stick(RESET_XZ[0], 0.0, RESET_XZ[1])
    reset = cq.Solid.makeCylinder(1.25, 20.0, cq.Vector(rx, ry, 0.0), cq.Vector(0, 0, 1))
    leds = board_box(LEDS_X[0] - 0.4, LEDS_X[1] + 0.4, 0.0, 20.0, LEDS_Z[0] - 0.5, LEDS_Z[1] + 0.5)
    pegs = [cylinder_x(PEG_R + PEG_CLEARANCE, CAP_T - 1, CAP_T + PEG_LEN + 0.5, y, z) for y, z in PEG_YZ]
    return pcb_and_parts.fuse(usb, reset, leds, *pegs)


def keyed_cylinder(r, x0, flat):
    """Cylinder ending at the split, with a flat at Z = flat (orientation key, glue vent)."""
    return cylinder_x(r, x0, SPLIT_X).cut(box(x0 - 1, SPLIT_X + 1, -r - 1, r + 1, flat, r + 1))


def build():
    body = outer_body().cut(board_cavity())
    socket = keyed_cylinder(SPIGOT_R + JOINT_CLEARANCE, SPLIT_X - SPIGOT_LEN - 0.5, SPIGOT_FLAT + JOINT_CLEARANCE)
    handle = body.cut(box(SPLIT_X, LENGTH + 10, -20, 20, -20, 20)).cut(socket)
    shaft = body.cut(box(-10, SPLIT_X, -20, 20, -20, 20)).fuse(
        keyed_cylinder(SPIGOT_R, SPLIT_X - SPIGOT_LEN, SPIGOT_FLAT)
    ).clean()
    spigot_end = [e for e in shaft.Edges() if abs(e.Center().x - (SPLIT_X - SPIGOT_LEN)) < 1e-3]
    shaft = shaft.chamfer(0.5, None, spigot_end)

    plate = cq.Solid.makeLoft(
        [
            section(0.0, BUTT_A - 0.8, BUTT_B - 0.8),
            section(0.8, BUTT_A - 0.15, BUTT_B - 0.15),
            section(CAP_T, BUTT_A, BUTT_B),
        ],
        True,
    )
    usb_hole = board_box(
        PCB_X[1], PCB_X[1] + 5.0,
        USB_Y[0] - USB_CLEARANCE, USB_Y[1] + USB_CLEARANCE,
        USB_Z[0] - USB_CLEARANCE, USB_Z[1] + USB_CLEARANCE,
    )
    pegs = [cylinder_x(PEG_R, CAP_T - 0.1, CAP_T + PEG_LEN, y, z) for y, z in PEG_YZ]
    cap = plate.cut(usb_hole).fuse(*pegs).clean()
    return {"handle": handle, "shaft": shaft, "cap": cap}


def build_tests(parts):
    """Short pieces to tune the fits before printing the whole stick."""
    big = 20
    return {
        "test_butt": parts["handle"].intersect(box(-1, TEST_BUTT_X, -big, big, -big, big)),
        "test_socket": parts["handle"].intersect(
            box(SPLIT_X - SPIGOT_LEN - TEST_JOINT_MARGIN, SPLIT_X, -big, big, -big, big)
        ),
        "test_spigot": parts["shaft"].intersect(box(SPLIT_X - SPIGOT_LEN - 1, SPLIT_X + TEST_JOINT_MARGIN, -big, big, -big, big)),
    }


def load_board():
    """Vendor board model moved into the stick frame, or None when the file is absent."""
    if not VENDOR_STEP.exists():
        return None
    board = cq.importers.importStep(str(VENDOR_STEP)).val()
    origin = cq.Vector(0, 0, 0)
    # Rx(90): (x, y, z) -> (x, -z, y), then Rz(180): -> (-x, z, y), as in to_stick().
    board = board.rotate(origin, cq.Vector(1, 0, 0), 90).rotate(origin, cq.Vector(0, 0, 1), 180)
    return board.translate(cq.Vector(BOARD_DX, BOARD_DY, BOARD_DZ))


def check_fit(parts, board):
    """Volume of board material inside each printed part: must be zero."""
    report = {}
    for name, part in parts.items():
        report[name] = sum(solid.intersect(part).Volume() for solid in board.Solids())
    return report


def main():
    parts = build()
    OUT.mkdir(exist_ok=True)
    for name, part in {**parts, **build_tests(parts)}.items():
        assert part.isValid(), name
        cq.exporters.export(part, str(OUT / f"{name}.stl"), tolerance=0.02, angularTolerance=0.1)
        cq.exporters.export(part, str(OUT / f"{name}.step"))
        bb = part.BoundingBox()
        print(f"{name}: {bb.xlen:.1f} x {bb.ylen:.1f} x {bb.zlen:.1f} mm, {part.Volume() / 1000:.1f} cm3")
    board = load_board()
    if board is None:
        print(f"fit check skipped: {VENDOR_STEP} not found")
        return
    for name, overlap in check_fit(parts, board).items():
        print(f"board overlap with {name}: {overlap:.3f} mm3")


if __name__ == "__main__":
    main()
