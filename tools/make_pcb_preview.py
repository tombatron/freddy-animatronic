#!/usr/bin/env python3
"""Draw a to-scale preview of the rev A controller PCB: the bare board and the
assembled board side by side. Writes hardware/controller/pcb_preview.svg.

This is a picture of the placement, not the routed board: only the VMOT trunk is
drawn, and the DRV8874 (Pololu #4035) row spacing is 0.5" per the Pololu drawing.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "hardware", "controller", "pcb_preview.svg")

PX = 6.0          # px per mm
M = 16            # margin around the board, mm (room for pigtails)
W = H = 100       # board, mm
P = 2.54          # pin pitch

MASK, MASK_EDGE = "#14532d", "#0b3b1f"
COPPER = "#1f7a45"          # copper under soldermask
PAD, HOLE, SILK = "#d6a849", "#1b1b1b", "#f5f5f0"

# Freddy's wire colors, for the pigtail wires in the assembled view
COL = {"red": "#d32f2f", "black": "#222", "yellow": "#f2c94c", "white": "#eee",
       "orange": "#f08a24", "blue": "#3b6fd8"}


def esc(t):
    return t.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


class Pic:
    def __init__(self):
        self.s = []

    def add(self, x):
        self.s.append(x)

    def rect(self, x, y, w, h, fill, stroke="none", sw=0.2, rx=0, extra=""):
        self.add(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" {extra}/>')

    def circle(self, x, y, r, fill, stroke="none", sw=0.2):
        self.add(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{r:.2f}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>')

    def text(self, x, y, t, size=1.5, fill=SILK, anchor="middle", weight="600", rot=0):
        tr = f' transform="rotate({rot} {x:.2f} {y:.2f})"' if rot else ""
        self.add(f'<text x="{x:.2f}" y="{y:.2f}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}"{tr}>{esc(t)}</text>')

    def line(self, pts, color, w, cap="round", extra=""):
        d = " ".join(f"{x:.2f},{y:.2f}" for x, y in pts)
        self.add(f'<polyline points="{d}" fill="none" stroke="{color}" stroke-width="{w}" stroke-linecap="{cap}" stroke-linejoin="round" {extra}/>')

    def pad(self, x, y, square=False, r=1.0):
        if square:
            self.rect(x - r, y - r, 2 * r, 2 * r, PAD)
        else:
            self.circle(x, y, r, PAD)
        self.circle(x, y, 0.5, HOLE)

    def smd(self, x, y, w, h, label=None, vertical=False):
        if vertical:
            w, h = h, w
        self.rect(x - w / 2, y - h / 2, w, h, "none", SILK, 0.15)
        pw = min(w, h) * 0.9
        if w >= h:
            self.rect(x - w / 2, y - h / 2 + 0.1, pw * 0.5, h - 0.2, PAD)
            self.rect(x + w / 2 - pw * 0.5, y - h / 2 + 0.1, pw * 0.5, h - 0.2, PAD)
        else:
            self.rect(x - w / 2 + 0.1, y - h / 2, w - 0.2, pw * 0.5, PAD)
            self.rect(x - w / 2 + 0.1, y + h / 2 - pw * 0.5, w - 0.2, pw * 0.5, PAD)
        if label:
            self.text(x, y - h / 2 - 0.5, label, 1.1)


# ------------------------------------------------------------------ footprints
# Everything in mm, origin = board top-left, viewed from the component side.

def pins_col(x, y0, n):
    return [(x, y0 + i * P) for i in range(n)]


def pins_row(x0, y, n):
    return [(x0 + i * P, y) for i in range(n)]


# ESP32-S3 dev board: 2x22, rows 25.4 apart, board 28.1 x 63.6, antenna up
ESP = dict(x=36, y=34, w=28.1, h=63.6)
esp_l = pins_col(ESP["x"] + 1.35, 41.5, 22)
esp_r = pins_col(ESP["x"] + 1.35 + 25.4, 41.5, 22)

# Pololu DRV8874 carriers (#4035): 0.6 x 0.7in, 2x7, rows 0.5in (per the Pololu dimension drawing).
# The VIN/OUT column faces the arm connectors, the logic column faces the ESP32.
U4 = dict(x=17.23, y=39.73, w=15.24, h=17.78)       # left
U4_out, U4_log = pins_col(18.5, 41.0, 7), pins_col(18.5 + 12.7, 41.0, 7)
U3 = dict(x=67.53, y=54.73, w=15.24, h=17.78)       # right (mirrored)
U3_log, U3_out = pins_col(68.8, 56.0, 7), pins_col(68.8 + 12.7, 56.0, 7)

# TB6612 (2x8, rows 0.6")
U5 = dict(x=12, y=12.8, w=20.3, h=20.8)
U5_a, U5_b = pins_col(14.4, 14.4, 8), pins_col(14.4 + 15.24, 14.4, 8)

# MAX98357A: 1x7 row + speaker holes 3.5 apart, 13 mm from the row, centered on pin 4
U6 = dict(x=67.1, y=14.0, w=17.8, h=19.3)
U6_pins = pins_row(68.38, 31.0, 7)
U6_spk = [(74.25, 18.0), (77.75, 18.0)]

# MP1584: four corner pads, 8.3 x 13.2 grid
U2_pads = [(69.85, 38.0), (78.15, 38.0), (69.85, 51.2), (78.15, 51.2)]
U2 = dict(x=68.4, y=36.4, w=11.2, h=16.4)

# Pololu S7V7F5, standing upright: 1x3
U1_pins = pins_row(23.46, 76.0, 3)

# harness pad groups: (ref, label, pads, wire colors, exit side)
L_ARM = [("J6", "MOTOR", pins_col(8, 40, 2), ["black", "red"]),
         ("J7", "ELBOW", pins_col(8, 51, 3), ["black", "red", "yellow"]),
         ("J8", "SWITCH", pins_col(8, 63, 4), ["red", "black", "yellow", "white"])]
R_ARM = [("J3", "MOTOR", pins_col(92, 40, 2), ["black", "red"]),
         ("J4", "ELBOW", pins_col(92, 51, 3), ["black", "red", "yellow"]),
         ("J5", "SWITCH", pins_col(92, 63, 4), ["red", "black", "yellow", "white"])]
HEAD = [("J9", "EYE SW", pins_row(28.73, 9, 2), ["yellow", "white"]),
        ("J10", "EYE LED", pins_row(39.73, 9, 2), ["yellow", "white"]),
        ("J11", "EYE MTR", pins_row(50.73, 9, 2), ["yellow", "orange"]),
        ("J15", "SPEAKER", pins_row(73.73, 9, 2), ["red", "black"])]
PIR = ("J13", "PIR", pins_row(76.0, 90.0, 3), ["yellow", "orange", "black"])

J16_pins = pins_row(67.6, 81.0, 6)
JP1_pins = pins_row(89.5, 16.0, 3)

CAPS = [  # (ref, x, y, r, value)
    ("C1", 10.0, 79.5, 5.0, "1000µF"),
    ("C3", 20.0, 68.5, 4.0, "470µF"),
    ("C2", 30.5, 69.0, 3.15, "470µF"),
    ("C4", 92.0, 26.5, 4.0, "470µF"),
    ("C5", 83.0, 47.0, 2.5, "100µF"),
]

SMDS = [  # (ref, x, y, size, vertical)
    ("R22", 12.8, 74.5, "0805", False), ("R13", 12.8, 77.0, "0805", False),
    ("R23", 87.2, 74.5, "0805", False), ("R14", 87.2, 77.0, "0805", False),
    ("R24", 25.0, 61.5, "0805", False), ("R4", 20.0, 61.5, "0805", False),
    ("R5", 30.0, 61.5, "0805", False), ("R6", 20.0, 64.0, "0805", False),
    ("R19", 30.0, 64.0, "0805", False),
    ("R25", 75.5, 76.5, "0805", False), ("R7", 70.5, 76.5, "0805", False),
    ("R8", 80.5, 76.5, "0805", False), ("R20", 85.0, 79.0, "0805", False),
    ("R3", 34.0, 20.0, "1206", True), ("R15", 24.0, 10.0, "0805", False),
    ("R1", 33.6, 84.0, "0805", False), ("R2", 33.6, 87.0, "0805", False),
    ("Q1", 33.6, 91.0, "SOT23", False), ("D1", 31.0, 80.8, "SMB", False),
    ("D2", 26.0, 81.0, "SMA", False),
    ("D3", 71.0, 84.5, "SOD", False), ("D4", 82.0, 86.5, "SOT23", False),
    ("R16", 74.5, 86.5, "0805", False), ("R21", 79.0, 86.5, "0805", True),
    ("R17", 88.0, 84.5, "0805", False), ("R18", 91.5, 84.5, "0805", False),
    ("C6", 26.0, 36.0, "0805", False), ("C7", 64.0, 36.5, "0805", True),
    ("C8", 85.5, 33.0, "0805", True), ("C9", 22.0, 58.8, "0805", False),
    ("C10", 72.0, 52.7, "0805", False),
]
SIZES = {"0805": (2.0, 1.25), "1206": (3.2, 1.6), "SOT23": (2.9, 1.6), "SMA": (4.3, 2.6),
         "SMB": (4.6, 3.6), "SOD": (2.6, 1.6)}

HOLES = [(4, 4), (96, 4), (4, 96), (96, 96)]

# VMOT trunk: F1 up the left, across the top (above the antenna keep-out), down the right
VMOT = [
    [(24.0, 85.0), (14.0, 85.0), (14.0, 12.0), (86.0, 12.0), (86.0, 56.0), (81.5, 56.0)],
    [(14.0, 41.0), (18.5, 41.0)],                    # U4 VIN
    [(29.64, 12.0), (29.64, 14.4)],                  # U5 VM
    [(86.0, 38.0), (78.15, 38.0)],                   # U2 IN+
    [(14.0, 76.0), (23.46, 76.0)],                   # U1 VIN
]


# ------------------------------------------------------------------ drawing
def zip_slots(p, pads, side):
    """Two zip-tie slots straddling a pad group, between the pads and the board edge."""
    xs = [x for x, _ in pads]
    ys = [y for _, y in pads]
    if side in ("L", "R"):
        x = 3.6 if side == "L" else 96.4
        for y in (min(ys) - 2.6, max(ys) + 2.6):
            p.rect(x - 0.6, y - 1.3, 1.2, 2.6, HOLE, PAD, 0.3, rx=0.6)
    else:
        y = 4.2
        for x in (min(xs) - 2.6, max(xs) + 2.6):
            p.rect(x - 1.3, y - 0.6, 2.6, 1.2, HOLE, PAD, 0.3, rx=0.6)


def outline(p, d, ref, sub=None, dashed=False):
    extra = 'stroke-dasharray="1 0.6"' if dashed else ""
    p.rect(d["x"], d["y"], d["w"], d["h"], "none", SILK, 0.2, rx=0.6, extra=extra)
    p.text(d["x"] + d["w"] / 2, d["y"] + d["h"] / 2 - (0.6 if sub else -0.5), ref, 1.8)
    if sub:
        p.text(d["x"] + d["w"] / 2, d["y"] + d["h"] / 2 + 1.6, sub, 1.2, weight="400")


def board(p, assembled):
    # mask + edge
    p.rect(0, 0, W, H, MASK, MASK_EDGE, 0.6, rx=2.5)
    # VMOT copper under the mask
    for seg in VMOT:
        p.line(seg, COPPER, 3.0)
    # antenna keep-out: no copper, hatched silkscreen
    p.add('<rect x="38" y="24" width="24" height="14" fill="#0f3d22" stroke="#f5f5f0" stroke-width="0.2" stroke-dasharray="0.8 0.6"/>')
    p.add('<rect x="38" y="24" width="24" height="14" fill="url(#hatch)"/>')
    if not assembled:
        p.text(50, 30.2, "ANTENNA KEEP-OUT", 1.4)
        p.text(50, 32.2, "no copper, both layers", 1.1, weight="400")

    # mounting holes
    for x, y in HOLES:
        p.circle(x, y, 3.0, PAD)
        p.circle(x, y, 1.6, "#0a0a0a")

    # module footprints (silkscreen + pads)
    outline(p, ESP, "A1 ESP32-S3", "USB-C ▼", dashed=False)
    for x, y in esp_l + esp_r:
        p.pad(x, y, square=(x, y) in (esp_l[0], esp_r[0]))
    outline(p, U4, "U4", "DRV8874 L")
    outline(p, U3, "U3", "DRV8874 R")
    for pins in (U4_out, U4_log, U3_log, U3_out):
        for i, (x, y) in enumerate(pins):
            p.pad(x, y, square=(i == 0))
    outline(p, U5, "U5", "TB6612 eyes")
    for pins in (U5_a, U5_b):
        for i, (x, y) in enumerate(pins):
            p.pad(x, y, square=(i == 0))
    outline(p, U6, "U6", "MAX98357A")
    for i, (x, y) in enumerate(U6_pins):
        p.pad(x, y, square=(i == 0))
    for x, y in U6_spk:
        p.pad(x, y)
    outline(p, U2, "U2", "MP1584 5V")
    for x, y in U2_pads:
        p.pad(x, y, square=True, r=0.9)
    p.rect(21.4, 74.6, 9.0, 2.8, "none", SILK, 0.2, rx=0.4)
    for i, (x, y) in enumerate(U1_pins):
        p.pad(x, y, square=(i == 0))
    if not assembled:
        p.text(26.0, 73.6, "U1 5V", 1.3)

    # power entry: J1 barrel, J2 terminal, F1 fuse holder
    p.rect(6.5, 86.0, 9.0, 14.0, "none", SILK, 0.2)
    for x, y, sq in ((11.0, 89.0, True), (11.0, 95.0, False), (7.6, 92.0, False)):
        p.rect(x - 1.6, y - 0.9, 3.2, 1.8, PAD, rx=0.9)
        p.rect(x - 1.0, y - 0.35, 2.0, 0.7, HOLE, rx=0.35)
    p.text(11.0, 85.3, "J1 7.2V", 1.2)
    p.rect(17.0, 92.0, 10.2, 8.0, "none", SILK, 0.2)
    p.pad(19.56, 96.0, square=True, r=1.3)
    p.pad(24.64, 96.0, r=1.3)
    p.text(22.1, 91.3, "J2  +  −", 1.2)
    p.rect(17.0, 82.4, 13.5, 6.0, "none", SILK, 0.2)
    for x in (19.0, 22.0, 25.5, 28.5):
        p.rect(x - 0.4, 84.4, 0.8, 2.0, PAD)
    p.text(23.7, 89.9, "F1 5A", 1.2)

    # bottom-right: J12 trigger jack, J13 PIR pads, J14 Qwiic, J16 mic header
    p.rect(65.5, 88.0, 6.5, 12.0, "none", SILK, 0.2)
    for x, y in ((67.2, 91.0), (70.3, 93.0), (67.2, 97.0)):
        p.pad(x, y, r=0.9)
    p.text(68.75, 87.3, "J12 TRIG", 1.1)
    p.rect(85.0, 88.5, 7.0, 4.5, "none", SILK, 0.2)
    for i in range(4):
        p.rect(86.2 + i * 1.0 - 0.3, 91.3, 0.6, 1.4, PAD)
    p.rect(85.3, 88.8, 0.8, 1.6, PAD)
    p.rect(90.9, 88.8, 0.8, 1.6, PAD)
    p.text(88.5, 94.6, "J14 I2C", 1.1)
    for i, (x, y) in enumerate(J16_pins):
        p.pad(x, y, square=(i == 0))
    if not assembled:
        p.text(74.0, 83.6, "J16 MIC", 1.1)
    for i, (x, y) in enumerate(JP1_pins):
        p.pad(x, y, square=(i == 0))
    p.text(92.0, 13.8, "JP1 GAIN", 1.1)
    p.text(89.5, 19.0, "6", 1.0)
    p.text(94.6, 19.0, "12dB", 1.0)

    # harness pad groups
    for ref, lbl, pads, cols in L_ARM:
        for i, (x, y) in enumerate(pads):
            p.pad(x, y, square=(i == 0))
        zip_slots(p, pads, "L")
        p.text(10.4, pads[0][1] - 0.2, ref, 1.3, anchor="start")
        p.text(10.4, pads[0][1] + 1.5, lbl, 1.0, anchor="start", weight="400")
    for ref, lbl, pads, cols in R_ARM:
        for i, (x, y) in enumerate(pads):
            p.pad(x, y, square=(i == 0))
        zip_slots(p, pads, "R")
        p.text(89.6, pads[0][1] - 0.2, ref, 1.3, anchor="end")
        p.text(89.6, pads[0][1] + 1.5, lbl, 1.0, anchor="end", weight="400")
    for ref, lbl, pads, cols in HEAD + [PIR]:
        for i, (x, y) in enumerate(pads):
            p.pad(x, y, square=(i == 0))
        top = ref != "J13"
        if top:
            zip_slots(p, pads, "T")
        cx = sum(x for x, _ in pads) / len(pads)
        y0 = pads[0][1] + (2.6 if top else -1.8)
        p.text(cx, y0, f"{ref} {lbl}", 1.1)
    for i, (x, y) in enumerate(PIR[2]):
        pass
    p.rect(73.5, 94.6, 10.2, 1.2, HOLE, PAD, 0.3, rx=0.6)   # PIR zip-tie slot

    # passives
    for ref, x, y, r, val in CAPS:
        p.circle(x, y, r, "none", SILK, 0.2)
        p.add(f'<path d="M {x - r * 0.7:.2f} {y - r * 0.7:.2f} A {r:.2f} {r:.2f} 0 0 0 {x - r * 0.7:.2f} {y + r * 0.7:.2f} Z" fill="{SILK}" opacity="0.85"/>')
        p.pad(x - min(2.5, r * 0.5), y, r=0.8)
        p.pad(x + min(2.5, r * 0.5), y, square=True, r=0.8)
        if not assembled:
            p.text(x + r * 0.2, y - r - 0.5, f"{ref}", 1.1)
    for ref, x, y, size, vert in SMDS:
        w, h = SIZES[size]
        p.smd(x, y, w, h, None if assembled else ref, vertical=vert)

    # edge legends + title
    p.text(64, 2.6, "▲ HEAD", 1.5)
    p.text(1.9, 28, "◀ LEFT ARM", 1.3, rot=-90)
    p.text(98.1, 28, "RIGHT ARM ▶", 1.3, rot=90)
    p.text(50, 99.2, "", 1.4)
    p.text(8.0, 9.5, "FREDDY", 2.0, anchor="start")
    p.text(8.0, 11.4, "controller rev A", 1.1, anchor="start", weight="400")


def module_board(p, d, color, label, sub=None, chip=None, ly=None):
    p.rect(d["x"], d["y"], d["w"], d["h"], color, "#00000055", 0.25, rx=0.8)
    if chip:
        cx, cy, cs = chip
        p.rect(cx - cs / 2, cy - cs / 2, cs, cs, "#151515", rx=0.3)
    ly = ly if ly is not None else d["y"] + d["h"] - 2.4
    p.text(d["x"] + d["w"] / 2, ly, label, 1.6, fill="#fff")
    if sub:
        p.text(d["x"] + d["w"] / 2, ly + 1.5, sub, 1.0, fill="#ffffffcc", weight="400")


def header_dots(p, pins):
    for x, y in pins:
        p.circle(x, y, 0.55, "#c9c9c9")


def populate(p):
    # pigtails: wires off the board edge to a white JST SM housing
    def pig(pads, cols, side):
        n = len(pads)
        for (x, y), c in zip(pads, cols):
            if side == "L":
                p.line([(x, y), (-9.0, y)], COL[c], 0.9, extra='stroke-opacity="1"')
            elif side == "R":
                p.line([(x, y), (109.0, y)], COL[c], 0.9)
            elif side == "T":
                p.line([(x, y), (x, -9.0)], COL[c], 0.9)
            else:
                p.line([(x, y), (x, 109.0)], COL[c], 0.9)
            p.circle(x, y, 0.9, "#c0c0c0")
        xs = [x for x, _ in pads]
        ys = [y for _, y in pads]
        if side in ("L", "R"):
            hx = -15.0 if side == "L" else 109.0
            p.rect(hx, min(ys) - 1.8, 6.0, max(ys) - min(ys) + 3.6, "#f4f1e8", "#9a9585", 0.2, rx=0.5)
        elif side == "T":
            p.rect(min(xs) - 1.8, -15.0, max(xs) - min(xs) + 3.6, 6.0, "#f4f1e8", "#9a9585", 0.2, rx=0.5)
        else:
            p.rect(min(xs) - 1.8, 109.0, max(xs) - min(xs) + 3.6, 6.0, "#f4f1e8", "#9a9585", 0.2, rx=0.5)
        # zip tie across the bundle
        if side in ("L", "R"):
            zx = 3.6 if side == "L" else 96.4
            p.rect(zx - 0.5, min(ys) - 2.8, 1.0, max(ys) - min(ys) + 5.6, "#111", rx=0.4)
        elif side == "T":
            p.rect(min(xs) - 2.8, 3.7, max(xs) - min(xs) + 5.6, 1.0, "#111", rx=0.4)

    for _, _, pads, cols in L_ARM:
        pig(pads, cols, "L")
    for _, _, pads, cols in R_ARM:
        pig(pads, cols, "R")
    for ref, _, pads, cols in HEAD:
        if ref == "J15":
            for (x, y), c in zip(pads, cols):
                p.line([(x, y), (x, -12.0)], COL[c], 0.9)
                p.circle(x, y, 0.9, "#c0c0c0")
            p.rect(min(x for x, _ in pads) - 2.8, 3.7, 8.1, 1.0, "#111", rx=0.4)
            p.text(75.0, -13.0, "to speaker", 1.3, fill="#555", weight="400")
        else:
            pig(pads, cols, "T")
    pig(PIR[2], PIR[3], "B")

    # modules
    module_board(p, U5, "#b3261e", "TB6612", "eyes", chip=(22.0, 23.0, 4.0))
    header_dots(p, U5_a + U5_b)
    module_board(p, U4, "#1f8a5a", "DRV8874", "left arm", chip=(24.85, 47.0, 4.0))
    header_dots(p, U4_out + U4_log)
    module_board(p, U3, "#1f8a5a", "DRV8874", "right arm", chip=(75.15, 62.0, 4.0))
    header_dots(p, U3_log + U3_out)
    module_board(p, U6, "#1f3c88", "MAX98357A", "amp", chip=(76.0, 24.0, 3.0), ly=28.0)
    header_dots(p, U6_pins + U6_spk)
    p.rect(U2["x"], U2["y"], U2["w"], U2["h"], "#1d4ed8", "#00000055", 0.25, rx=0.6)
    p.rect(70.0, 40.5, 8.0, 7.0, "#555", "#333", 0.2, rx=0.6)        # inductor
    p.text(74.0, 44.5, "MP1584", 1.2, fill="#fff")
    p.text(74.0, 50.8, "audio 5V", 1.0, fill="#fff", weight="400")
    # Pololu stands upright: seen edge-on from above
    p.rect(21.55, 75.2, 8.9, 1.6, "#2e7d32", "#0b3b1f", 0.2, rx=0.2)
    p.text(26.0, 74.2, "S7V7F5 (upright)", 1.1, fill="#fff", weight="400")

    # ESP32 dev board on female headers
    e = ESP
    p.rect(e["x"], e["y"], e["w"], e["h"], "#161616", "#000", 0.3, rx=1.0)
    p.rect(e["x"] + 4.5, e["y"] + 0.8, e["w"] - 9.0, 6.0, "#1f1f1f")
    ax = e["x"] + 6.0
    p.line([(ax, 37.5), (ax + 3, 35.6), (ax + 6, 39.4), (ax + 9, 35.6), (ax + 12, 39.4), (ax + 15, 35.6)], PAD, 0.5)
    p.rect(e["x"] + 4.5, e["y"] + 7.0, 19.1, 25.5, "#b8bcc2", "#8a8f96", 0.3, rx=0.6)
    p.text(50.0, e["y"] + 18.0, "ESP32-S3", 2.0, fill="#333")
    p.text(50.0, e["y"] + 20.5, "N16R8", 1.4, fill="#333", weight="400")
    p.rect(46.5, 74.0, 7.0, 7.0, "#2a2a2a")                       # USB-serial chip
    p.circle(50.0, 86.0, 1.1, "#e8e8e8")                          # RGB LED
    p.text(50.0, 88.8, "RGB", 1.0, fill="#aaa", weight="400")
    for bx in (39.5, 57.0):
        p.rect(bx, 89.5, 3.6, 2.8, "#d9d9d9", "#999", 0.2, rx=0.3)
        p.circle(bx + 1.8, 90.9, 0.8, "#222")
    p.text(41.3, 93.6, "BOOT", 0.9, fill="#aaa", weight="400")
    p.text(58.8, 93.6, "RST", 0.9, fill="#aaa", weight="400")
    for ux in (39.3, 51.6):
        p.rect(ux, 94.6, 9.0, 3.6, "#c8ccd2", "#7d828a", 0.3, rx=1.6)
    p.text(50.0, 99.4, "USB-C ×2 at the board edge", 1.0, fill="#cfd", weight="400")
    header_dots(p, esp_l + esp_r)

    # electrolytics (top view)
    for ref, x, y, r, val in CAPS:
        p.circle(x, y, r, "#24364f", "#111", 0.2)
        p.add(f'<path d="M {x - r * 0.7:.2f} {y - r * 0.7:.2f} A {r:.2f} {r:.2f} 0 0 0 {x - r * 0.7:.2f} {y + r * 0.7:.2f} Z" fill="#9aa7b8"/>')
        p.circle(x, y, r * 0.72, "#c7ccd3")
        p.line([(x - r * 0.4, y), (x + r * 0.4, y)], "#8d939b", 0.25)
        p.line([(x, y - r * 0.4), (x, y + r * 0.4)], "#8d939b", 0.25)

    # SMD parts
    for ref, x, y, size, vert in SMDS:
        w, h = SIZES[size]
        if vert:
            w, h = h, w
        body = "#202020" if ref[0] in "RQD" else "#b9946a"
        p.rect(x - w / 2 + 0.25, y - h / 2 + 0.05, w - 0.5, h - 0.1, body, rx=0.15)

    # power entry
    p.rect(6.5, 86.0, 9.0, 15.5, "#1a1a1a", "#000", 0.2, rx=0.4)
    p.circle(11.0, 100.0, 2.8, "#333")
    p.circle(11.0, 100.0, 1.0, "#bbb")
    p.text(11.0, 91.0, "7.2V", 1.4, fill="#ddd")
    p.rect(17.0, 92.0, 10.2, 8.5, "#2e9e4f", "#1b6b33", 0.2, rx=0.3)
    for x in (19.56, 24.64):
        p.circle(x, 95.0, 1.6, "#cfd3d8", "#777", 0.2)
        p.line([(x - 1.1, 95.0), (x + 1.1, 95.0)], "#555", 0.35)
    p.rect(17.0, 82.4, 13.5, 6.0, "#1a1a1a", "#000", 0.2, rx=0.4)
    p.rect(19.2, 83.4, 9.0, 4.0, "#d8a55a", "#9b6e2c", 0.2, rx=0.5)  # 5A mini blade (tan)
    p.text(23.7, 86.0, "5", 1.6, fill="#5a3b10")

    # bottom right
    p.rect(65.5, 88.0, 6.5, 13.5, "#1a1a1a", "#000", 0.2, rx=0.4)
    p.circle(68.75, 100.5, 1.6, "#555")
    p.text(68.75, 92.5, "TRIG", 1.0, fill="#ddd", weight="400")
    p.rect(85.0, 88.5, 7.0, 4.5, "#f4f1e8", "#9a9585", 0.2, rx=0.3)
    p.text(88.5, 91.3, "Qwiic", 1.0, fill="#555", weight="400")
    p.rect(66.4, 79.8, 15.1, 2.4, "#1a1a1a", "#000", 0.2)
    for x, y in J16_pins:
        p.rect(x - 0.4, y - 0.4, 0.8, 0.8, "#000")
    p.text(74.0, 78.8, "J16 mic header", 1.0, fill="#fff", weight="400")
    p.rect(88.3, 14.8, 7.5, 2.4, "#1a1a1a", "#000", 0.2)
    for x, y in JP1_pins:
        p.rect(x - 0.3, y - 0.3, 0.6, 0.6, "#d6a849")


def panel(ox, oy, title, assembled):
    p = Pic()
    p.add(f'<g transform="translate({ox},{oy})">')
    p.add(f'<text x="{(M + W / 2) * PX}" y="-14" font-size="20" font-weight="650" text-anchor="middle" fill="#1d1d1f">{esc(title)}</text>')
    p.add(f'<g transform="scale({PX}) translate({M},{M})" font-family="Helvetica, Arial, sans-serif">')
    board(p, assembled)
    if assembled:
        populate(p)
    p.add("</g></g>")
    return "\n".join(p.s)


def main():
    pw = (W + 2 * M) * PX
    total_w = 2 * pw + 60
    total_h = (H + 2 * M) * PX + 150
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {total_w:.0f} {total_h:.0f}" font-family="Helvetica, Arial, sans-serif">',
           '<defs><pattern id="hatch" width="1.2" height="1.2" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">'
           '<line x1="0" y1="0" x2="0" y2="1.2" stroke="#f5f5f0" stroke-width="0.18" opacity="0.45"/></pattern></defs>',
           '<rect width="100%" height="100%" fill="#f6f4ef"/>',
           f'<text x="30" y="40" font-size="24" font-weight="700" fill="#1d1d1f">Freddy controller rev A · PCB preview (to scale, 100 × 100 mm)</text>']
    out.append(panel(20, 90, "Bare board (component side)", False))
    out.append(panel(40 + pw, 90, "Assembled, pigtails soldered in", True))
    ny = (H + 2 * M) * PX + 115
    notes = ("Preview of the placement, not the routed board: only the VMOT trunk (darker green) is drawn; signal routing comes in KiCad. "
             "Pigtail wire colors show which of Freddy's wires each pad meets.")
    out.append(f'<text x="30" y="{ny}" font-size="14" fill="#555">{esc(notes)}</text>')
    out.append("</svg>")
    with open(OUT, "w") as f:
        f.write("\n".join(out))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
