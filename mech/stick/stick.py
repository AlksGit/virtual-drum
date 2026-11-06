"""Printed drumstick holding a XIAO nRF52840 Sense and its LiPo: handle, shaft, and a lid.

Run: python mech/stick/stick.py  ->  mech/stick/out/{handle,shaft,lid}.{stl,step}, plus short
test_* pieces to tune the fits.

Stick frame (matches docs/PROTOCOL.md): X along the stick, from the butt (X = 0) towards the
tip; Z is the top side of the board (LEDs, reset button); Y completes a right-handed frame.

The board and the battery sit one after the other in the butt, both open towards -Z. A lid
closes the underside over the whole electronics, held by two self-tapping screws: nothing has
to be slid in blind. The board lies on the lid by the edges of its PCB, pressed down by a foam
pad on its RF shield; its USB-C receptacle sits in a notch of the butt end wall that the lid
closes. The battery is taped to the ceiling of its pocket. Its wires run under the board, in a
recess of the lid, and past the first screw in a groove.

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

# --- Battery: LP321323, 3.2 x 13 x 23 mm (seller datasheet of the 80 mAh version,
# li-polymer-battery.com; the name encodes the size). The 120 mAh cells in use look the same
# (not measured): BATTERY_PLAY leaves room.
BATTERY_L, BATTERY_W, BATTERY_T = 23.0, 13.0, 3.2
BATTERY_PLAY = 1.5  # total, in each dimension
BATTERY_TAPE = 1.0  # double-sided tape between the cell and the ceiling of its pocket
WIRE_BAY = 5.0  # pocket length before the cell: the wires leave its protection board and fold
MIN_WALL = 1.5  # around the battery pocket, checked by battery_wall_check()

# --- Fits (mm) ------------------------------------------------------------------------
CLEARANCE = 0.15  # per side, board in its pocket
TOP_FOAM = 1.0  # foam pad on the RF shield, pressing the board onto the lid
UNDER_GAP = 2.2  # recess of the lid under the board: wires and solder joints on the BAT pads
UNDER_HALF_W = 7.5  # half width of that recess; the PCB edges outside it rest on the lid
WIRE_GROOVE_Y = (3.0, 6.5)  # groove in the lid, from under the board to the battery pocket
USB_CLEARANCE = 0.3
JOINT_CLEARANCE = 0.15  # radial, shaft spigot in the handle socket (glued)

# --- Lid screws: self-tapping, from below, 3 mm holes in the lid and 2.7 mm pilots in the
# handle, head in a counterbore. One between the board and the battery (off axis, clear of
# the wire groove), one past the battery.
SCREW_LEN = 10.0
HOLE_D, PILOT_D = 3.0, 2.7
HEAD_D, HEAD_H = 6.0, 2.4  # pan head, counterbore
BULKHEAD = 7.0  # solid between the board and battery pockets, takes the first screw
SCREW_A_Y = -1.5
SCREW_B_MARGIN = 4.0  # second screw, past the battery pocket
LID_END_MARGIN = 4.5  # lid length past the second screw

# --- Stick shape (mm) -----------------------------------------------------------------
LENGTH = 406.0  # a 7A stick
SPLIT_X = 215.0  # handle | shaft; both parts stay under 220 mm to print
END_WALL = 1.6  # butt end wall; the PCB edge rests against it, USB-C receptacle in a notch
BUTT_A, BUTT_B = 11.25, 7.25  # semi-axes of the oval butt (Y, Z) around the board
BUTT_END_X = 48.0  # oval section up to here, past the battery (28 before it)
ROUND_X = 88.0  # round from here (62 before the battery)
R = 7.5  # 15 mm stick
TAPER_X = 260.0
TIP_R = 4.0  # end of the taper, inside the tip bead
TIP_LEN, TIP_R_BEAD = 7.0, 5.2  # ellipsoid semi-axes of the tip bead
GROOVE_X, GROOVE_W, GROOVE_D = 94.0, 3.0, 0.8  # zip tie that holds the USB cable along the stick
TEST_BUTT_MARGIN = 3.0  # test pieces: past the lid, and both sides of the joint
TEST_JOINT_MARGIN = 8.0

# The flat keys the orientation and vents the glue. It is on top (+Z).
SPIGOT_R, SPIGOT_LEN, SPIGOT_FLAT = 4.5, 25.0, 3.7

# Board frame -> stick frame: rotation (x -> -X, y -> Z, z -> Y), PCB USB edge on the end wall.
# In board coordinates (y up): the lid face is just under the PCB, at the bottom of the USB-C
# notch; the stack from the lid recess to the ceiling is centred on the stick axis.
SPLIT_Y = USB_Y[0] - USB_CLEARANCE
CEILING_Y = BOARD_Y[1] + TOP_FOAM
RECESS_Y = BOARD_Y[0] - UNDER_GAP
BOARD_DX = END_WALL + PCB_X[1]
BOARD_DY = -(PCB_Z[0] + PCB_Z[1]) / 2
BOARD_DZ = -(RECESS_Y + CEILING_Y) / 2
Z_SPLIT = SPLIT_Y + BOARD_DZ  # lid | handle, under the electronics
Z_RECESS = RECESS_Y + BOARD_DZ


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


def cylinder_z(r, z0, z1, x, y):
    return cq.Solid.makeCylinder(r, z1 - z0, cq.Vector(x, y, z0), cq.Vector(0, 0, 1))


def outer_body():
    butt = cq.Solid.makeLoft(
        [
            section(0.0, BUTT_A - 0.8, BUTT_B - 0.8),  # chamfered butt edge
            section(0.8, BUTT_A - 0.15, BUTT_B - 0.15),
            section(END_WALL, BUTT_A, BUTT_B),
            section(BUTT_END_X, BUTT_A, BUTT_B),
        ],
        True,
    )
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


def board_pocket_end_x():
    return to_stick(PCB_X[0] - 2 * CLEARANCE, 0.0, 0.0)[0]


def battery_pocket_x():
    x0 = board_pocket_end_x() + BULKHEAD
    return x0, x0 + WIRE_BAY + BATTERY_L + BATTERY_PLAY


def battery_section():
    """(z0, z1, half width) of the battery pocket, centred on the stick axis."""
    half_t = (BATTERY_TAPE + BATTERY_T + BATTERY_PLAY) / 2
    return -half_t, half_t, (BATTERY_W + BATTERY_PLAY) / 2


def screw_positions():
    x_a = board_pocket_end_x() + BULKHEAD / 2
    x_b = battery_pocket_x()[1] + SCREW_B_MARGIN
    return ((x_a, SCREW_A_Y), (x_b, 0.0))


def lid_end_x():
    return screw_positions()[1][0] + LID_END_MARGIN


def outer_z(x, y):
    """(bottom, top) of the outer surface on the vertical line through (x, y)."""
    bb = outer_body().intersect(box(x - 0.05, x + 0.05, y - 0.05, y + 0.05, -20, 20)).BoundingBox()
    return bb.zmin, bb.zmax


def screw_depths(x, y):
    """(lid under the head, thread in the handle) for the screw at (x, y), in mm."""
    bottom, _ = outer_z(x, y)
    through = Z_SPLIT - (bottom + HEAD_H)
    return through, SCREW_LEN - through


def board_cavity():
    pocket_end = board_pocket_end_x()
    pcb_and_parts = board_box(
        PCB_X[0] - 2 * CLEARANCE, PCB_X[1],
        SPLIT_Y - 0.01, CEILING_Y,
        PCB_Z[0] - CLEARANCE, PCB_Z[1] + CLEARANCE,
    )
    # Notch through the end wall, open down to the lid.
    usb = board_box(
        USB_X0 - USB_CLEARANCE, PCB_X[1] + 5.0,
        SPLIT_Y - 0.01, USB_Y[1] + USB_CLEARANCE,
        USB_Z[0] - USB_CLEARANCE, USB_Z[1] + USB_CLEARANCE,
    )
    rx, ry, _ = to_stick(RESET_XZ[0], 0.0, RESET_XZ[1])
    reset = cylinder_z(1.25, 0.0, 20.0, rx, ry)
    leds = board_box(LEDS_X[0] - 0.4, LEDS_X[1] + 0.4, 0.0, 20.0, LEDS_Z[0] - 0.5, LEDS_Z[1] + 0.5)
    under = box(END_WALL, pocket_end, -UNDER_HALF_W, UNDER_HALF_W, Z_RECESS, Z_SPLIT + 0.01)
    groove = box(pocket_end - 0.1, battery_pocket_x()[0] + 0.1, *WIRE_GROOVE_Y, Z_RECESS, Z_SPLIT + 0.01)
    return pcb_and_parts.fuse(usb, reset, leds, under, groove)


def battery_cavity():
    x0, x1 = battery_pocket_x()
    z0, z1, half_w = battery_section()
    return box(x0, x1, -half_w, half_w, z0, z1)


def screw_cavities():
    """Pilots in the handle; clearance holes and counterbores in the lid."""
    solids = []
    for x, y in screw_positions():
        bottom, _ = outer_z(x, y)
        _, thread = screw_depths(x, y)
        solids += [
            cylinder_z(PILOT_D / 2, Z_SPLIT - 0.1, Z_SPLIT + thread + 0.5, x, y),
            cylinder_z(HOLE_D / 2, bottom - 1.0, Z_SPLIT + 0.01, x, y),
            cylinder_z(HEAD_D / 2, bottom - 1.0, bottom + HEAD_H, x, y),
        ]
    return solids


def battery_wall_check():
    """Volume of the battery pocket grown by MIN_WALL (round corners) outside the stick: must be 0."""
    x0, x1 = battery_pocket_x()
    z0, z1, half_w = battery_section()
    rect = cq.Wire.makePolygon(
        [cq.Vector(x0, y, z) for y, z in ((-half_w, z0), (half_w, z0), (half_w, z1), (-half_w, z1))], close=True
    )
    grown = cq.Solid.extrudeLinear(rect.offset2D(MIN_WALL, "arc")[0], [], cq.Vector(x1 - x0, 0, 0))
    return grown.cut(outer_body()).Volume()


def screw_report():
    """Per screw: (x, lid under the head, thread, wall above the pilot, wall to the pockets), mm."""
    pockets = board_cavity().fuse(battery_cavity())
    rows = []
    for x, y in screw_positions():
        _, top = outer_z(x, y)
        through, thread = screw_depths(x, y)
        pilot = cylinder_z(PILOT_D / 2, Z_SPLIT + 0.01, Z_SPLIT + thread + 0.5, x, y)
        rows.append((x, through, thread, top - (Z_SPLIT + thread + 0.5), pilot.distance(pockets)))
    return rows


def keyed_cylinder(r, x0, flat):
    """Cylinder ending at the split, with a flat at Z = flat (orientation key, glue vent)."""
    return cylinder_x(r, x0, SPLIT_X).cut(box(x0 - 1, SPLIT_X + 1, -r - 1, r + 1, flat, r + 1))


def build():
    lid_zone = box(-1, lid_end_x(), -20, 20, -20, Z_SPLIT)
    body = outer_body().cut(board_cavity()).cut(battery_cavity()).cut(*screw_cavities())
    lid = body.intersect(lid_zone).clean()
    body = body.cut(lid_zone)
    socket = keyed_cylinder(SPIGOT_R + JOINT_CLEARANCE, SPLIT_X - SPIGOT_LEN - 0.5, SPIGOT_FLAT + JOINT_CLEARANCE)
    handle = body.cut(box(SPLIT_X, LENGTH + 10, -20, 20, -20, 20)).cut(socket)
    shaft = body.cut(box(-10, SPLIT_X, -20, 20, -20, 20)).fuse(
        keyed_cylinder(SPIGOT_R, SPLIT_X - SPIGOT_LEN, SPIGOT_FLAT)
    ).clean()
    spigot_end = [e for e in shaft.Edges() if abs(e.Center().x - (SPLIT_X - SPIGOT_LEN)) < 1e-3]
    shaft = shaft.chamfer(0.5, None, spigot_end)
    return {"handle": handle, "shaft": shaft, "lid": lid}


def build_tests(parts):
    """Short pieces to tune the fits before printing the whole stick (test_butt takes the lid)."""
    big = 20
    return {
        "test_butt": parts["handle"].intersect(box(-1, lid_end_x() + TEST_BUTT_MARGIN, -big, big, -big, big)),
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
    x0, x1 = battery_pocket_x()
    print(f"battery pocket X {x0:.1f}-{x1:.1f} mm, wall under {MIN_WALL} mm: {battery_wall_check():.3f} mm3")
    for x, through, thread, above, pocket in screw_report():
        print(f"screw at X {x:.1f}: lid {through:.1f} mm under the head, {thread:.1f} mm of thread in the "
              f"handle, {above:.1f} mm above the pilot, {pocket:.2f} mm to the pockets")
    print(f"lid: X 0-{lid_end_x():.1f} mm; PCB {BOARD_Y[0] - SPLIT_Y:.2f} mm above its face")
    board = load_board()
    if board is None:
        print(f"fit check skipped: {VENDOR_STEP} not found")
        return
    for name, overlap in check_fit(parts, board).items():
        print(f"board overlap with {name}: {overlap:.3f} mm3")


if __name__ == "__main__":
    main()
