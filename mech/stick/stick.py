"""Printed drumstick holding a XIAO nRF52840 Sense and its LiPo, in two parts plus a butt cap
held by two self-tapping screws.

Run: python mech/stick/stick.py  ->  mech/stick/out/{handle,shaft,cap}.{stl,step}, plus short
test_* pieces to tune the fits.

Stick frame (matches docs/PROTOCOL.md): X along the stick, from the butt (X = 0) towards the
tip; Z is the top side of the board (LEDs, reset button); Y completes a right-handed frame.

Board dimensions come from the vendor model `mech/vendor/XIAO-nRF52840 v15.step` (not
committed: unclear licence). When it is present, the script places the board in the handle
and checks that nothing intersects.

The LiPo sits flat right after the board, towards the tip, in a pocket that continues the
board pocket: it slides in through the butt before the board. A channel under the board
leaves room for its wires and the solder joints on the BAT pads.
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

# --- Battery: LP321323, 3.2 x 13 x 23 mm (seller datasheet of the 80 mAh version,
# li-polymer-battery.com; the name encodes the size). The 120 mAh cells in use look the same
# (not measured): BATTERY_PLAY leaves room.
BATTERY_L, BATTERY_W, BATTERY_T = 23.0, 13.0, 3.2
BATTERY_PLAY = 1.5  # total, in each dimension (thickness: above the tape)
BATTERY_TAPE = 1.0  # double-sided tape under the cell
WIRE_GAP = 3.0  # between the board and the cell: the wires leave the cell's protection board
# Channel in the floor under the whole board, for the wires and the solder joints on the BAT
# pads (position of the pads not checked on a drawing, hence the full length).
WIRE_CHANNEL_W, WIRE_CHANNEL_D = 6.0, 1.2
MIN_WALL = 1.5  # around the battery pocket, checked by battery_wall_check()

# --- Fits (mm) ------------------------------------------------------------------------
CLEARANCE = 0.15  # per side, board in its pocket
TOP_GAP = 0.35  # above the RF shield
BOTTOM_GAP = 1.0  # under the PCB: room for 1 mm double-sided foam tape that holds the board
USB_CLEARANCE = 0.3
JOINT_CLEARANCE = 0.15  # radial, shaft spigot in the handle socket (glued)

# --- Stick shape (mm) -----------------------------------------------------------------
LENGTH = 406.0  # a 7A stick
SPLIT_X = 215.0  # handle | shaft; both parts stay under 220 mm to print
CAP_T = 1.6  # butt cap plate; the PCB edge rests against it
# Semi-axes of the oval butt (Y, Z) around the board. B was 7.25: the screw holes above and
# below the board pocket then had 0.1 mm of wall.
BUTT_A, BUTT_B = 11.25, 9.0
BUTT_END_X = 38.0  # oval section up to here (28 before the battery)
ROUND_X = 80.0  # round from here (62 before the battery: its pocket walls fell under MIN_WALL)
R = 7.5  # 15 mm stick
TAPER_X = 260.0
TIP_R = 4.0  # end of the taper, inside the tip bead
TIP_LEN, TIP_R_BEAD = 7.0, 5.2  # ellipsoid semi-axes of the tip bead
GROOVE_X, GROOVE_W, GROOVE_D = 86.0, 3.0, 0.8  # zip tie that holds the USB cable along the stick
TEST_BUTT_MARGIN = 3.0  # test pieces: board and battery pockets, and both sides of the joint
TEST_JOINT_MARGIN = 8.0

# Cap screws: self-tapping, along X, above and below the board pocket on the stick axis. The
# top one clears the USB-C cut-out, the bottom one the wire channel (screw_wall_check()).
SCREW_Z = (6.3, -6.0)
CAP_HOLE_D = 3.0  # through the cap
PILOT_D, PILOT_DEPTH = 2.7, 10.0  # in the butt, for the screw to tap its thread
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
    return pcb_and_parts.fuse(usb, reset, leds)


def pilot_holes():
    return [cylinder_x(PILOT_D / 2, CAP_T - 0.1, CAP_T + PILOT_DEPTH, 0.0, z) for z in SCREW_Z]


def battery_pocket_x():
    """Stick X range of the battery pocket: from the tip-side end of the board pocket."""
    x0 = to_stick(PCB_X[0] - 2 * CLEARANCE, 0.0, 0.0)[0]
    return x0, x0 + WIRE_GAP + BATTERY_L + BATTERY_PLAY


def battery_section():
    """(z0, z1, half width) of the battery pocket, on the floor of the board pocket."""
    z0 = POCKET_Y0 + BOARD_DZ
    z1 = max(POCKET_Y1 + BOARD_DZ, z0 + BATTERY_TAPE + BATTERY_T + BATTERY_PLAY)
    return z0, z1, (BATTERY_W + BATTERY_PLAY) / 2


def battery_cavity():
    x0, x1 = battery_pocket_x()
    z0, z1, half_w = battery_section()
    pocket = box(x0 - 0.1, x1, -half_w, half_w, z0, z1)
    board_x0 = to_stick(PCB_X[1] + 1.0, 0.0, 0.0)[0]
    channel = box(board_x0, x0, -WIRE_CHANNEL_W / 2, WIRE_CHANNEL_W / 2, z0 - WIRE_CHANNEL_D, z0 + 0.1)
    return pocket.fuse(channel)


def battery_wall_check():
    """Volume of the battery pocket grown by MIN_WALL (round corners) outside the stick: must be 0."""
    x0, x1 = battery_pocket_x()
    z0, z1, half_w = battery_section()
    rect = cq.Wire.makePolygon(
        [cq.Vector(x0, y, z) for y, z in ((-half_w, z0), (half_w, z0), (half_w, z1), (-half_w, z1))], close=True
    )
    grown = cq.Solid.extrudeLinear(rect.offset2D(MIN_WALL, "arc")[0], [], cq.Vector(x1 - x0, 0, 0))
    return grown.cut(outer_body()).Volume()


def screw_wall_check():
    """Thinnest wall around each pilot hole, to the outside and to the pockets (mm)."""
    outer = outer_body()
    sides = [f for f in outer.Faces() if abs(f.BoundingBox().xmax - CAP_T) > 1e-3]  # not the butt face
    pockets = board_cavity().fuse(battery_cavity())
    walls = []
    for z in SCREW_Z:
        hole = cylinder_x(PILOT_D / 2, CAP_T + 0.01, CAP_T + PILOT_DEPTH, 0.0, z)
        walls.append((min(hole.distance(f) for f in sides), hole.distance(pockets)))
    return walls


def keyed_cylinder(r, x0, flat):
    """Cylinder ending at the split, with a flat at Z = flat (orientation key, glue vent)."""
    return cylinder_x(r, x0, SPLIT_X).cut(box(x0 - 1, SPLIT_X + 1, -r - 1, r + 1, flat, r + 1))


def build():
    body = outer_body().cut(board_cavity()).cut(battery_cavity()).cut(*pilot_holes())
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
    holes = [cylinder_x(CAP_HOLE_D / 2, -1.0, CAP_T + 1.0, 0.0, z) for z in SCREW_Z]
    cap = plate.cut(usb_hole).cut(*holes).clean()
    return {"handle": handle, "shaft": shaft, "cap": cap}


def build_tests(parts):
    """Short pieces to tune the fits before printing the whole stick."""
    big = 20
    return {
        "test_butt": parts["handle"].intersect(
            box(-1, battery_pocket_x()[1] + TEST_BUTT_MARGIN, -big, big, -big, big)
        ),
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
    print(f"battery pocket X {battery_pocket_x()[0]:.1f}-{battery_pocket_x()[1]:.1f} mm, "
          f"wall under {MIN_WALL} mm: {battery_wall_check():.3f} mm3")
    for z, (outside, pocket) in zip(SCREW_Z, screw_wall_check()):
        print(f"screw at Z {z:+.1f}: wall {outside:.2f} mm to the outside, {pocket:.2f} mm to the pockets")
    board = load_board()
    if board is None:
        print(f"fit check skipped: {VENDOR_STEP} not found")
        return
    for name, overlap in check_fit(parts, board).items():
        print(f"board overlap with {name}: {overlap:.3f} mm3")


if __name__ == "__main__":
    main()
