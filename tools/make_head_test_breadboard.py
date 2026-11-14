#!/usr/bin/env python3
"""Breadboard layout for the head bench test (docs/head-bench-test.md).

    python3 tools/make_head_test_breadboard.py

Hole-by-hole layout on a standard breadboard. Wires that leave the board are
drawn as tags: Freddy's cables and the adapter go UP, ESP32 pins go DOWN.
"""
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "schematics", "head_test_breadboard.svg")

P = 26                   # hole pitch, px
COLS = 50
X0 = 140                 # x of column 1
Y0 = 260                 # y of the top outer rail

ROWS = {"T-": Y0, "T+": Y0 + P}                    # top: outer GND, inner VMOT
for i, r in enumerate("abcde"):
    ROWS[r] = Y0 + P * (2.6 + i)
for i, r in enumerate("fghij"):
    ROWS[r] = Y0 + P * (8.6 + i)
ROWS["B-"] = Y0 + P * 14.2                          # bottom: inner GND, outer 3V3
ROWS["Bx"] = Y0 + P * 15.2

RED, BLUE, ORANGE, GREEN, PURPLE, GOLD, BLACK, GRAY = (
    "#d62828", "#1d4ed8", "#f77f00", "#2a9d8f", "#7b2cbf", "#c9a227", "#222", "#999")
FONT = "DejaVu Sans, Arial, sans-serif"
svg = []


def add(s):
    svg.append(s)


def xy(col, row):
    return X0 + (col - 1) * P, ROWS[row]


def text(x, y, s, size=13, anchor="middle", color=BLACK, weight="normal"):
    s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    add(f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" text-anchor="{anchor}" fill="{color}" '
        f'font-weight="{weight}" font-family="{FONT}">{s}</text>')


def end(x, y, color):
    add(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.5" fill="{color}" stroke="#000" stroke-width="1"/>')


def wire(a, b, color, via=None):
    """Jumper between two holes (col, row). via: absolute (x, y) bend points."""
    pts = [xy(*a)] + list(via or []) + [xy(*b)]
    add('<polyline points="' + " ".join(f"{x:.1f},{y:.1f}" for x, y in pts) +
        f'" fill="none" stroke="{color}" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>')
    end(*pts[0], color)
    end(*pts[-1], color)


def resistor(a, b, value, below=False):
    (x1, y1), (x2, y2) = xy(*a), xy(*b)
    mx, my = (x1 + x2) / 2, (y1 + y2) / 2
    ang = math.degrees(math.atan2(y2 - y1, x2 - x1))
    add(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#666" stroke-width="2.5"/>')
    add(f'<g transform="translate({mx:.1f},{my:.1f}) rotate({ang:.1f})">'
        f'<rect x="-17" y="-7" width="34" height="14" rx="6" fill="#dcc08f" stroke="#7a5c33" stroke-width="1.5"/></g>')
    for x, y in ((x1, y1), (x2, y2)):
        add(f'<circle cx="{x}" cy="{y}" r="3" fill="#666"/>')
    if abs(ang) < 30 or abs(ang) > 150:          # horizontal-ish: label above
        text(mx, my + (22 if below else -12), value, 12, weight="bold")
    else:                                          # vertical-ish: label to the right
        text(mx + 14, my + 4, value, 12, "start", weight="bold")


def tag(col, row, label, color, up=True, length=3.0):
    x, y = xy(col, row)
    y2 = y - length * P if up else y + length * P
    add(f'<line x1="{x}" y1="{y}" x2="{x}" y2="{y2}" stroke="{color}" stroke-width="5" '
        f'stroke-linecap="round" stroke-dasharray="{"none"}"/>')
    end(x, y, color)
    w = 7.6 * len(label) + 16
    by = y2 - 26 if up else y2
    add(f'<rect x="{x - w / 2:.1f}" y="{by:.1f}" width="{w:.1f}" height="26" rx="7" fill="white" '
        f'stroke="{color}" stroke-width="2.5"/>')
    text(x, by + 18, label, 13, weight="bold")


def pin(col, row, name, below):
    x, y = xy(col, row)
    add(f'<rect x="{x - 5}" y="{y - 5}" width="10" height="10" fill="#e8c547" stroke="#000"/>')
    text(x, y + (17 if below else -10), name, 7.5, color="white", weight="bold")


# ------------------------------------------------------------------ canvas
W = X0 + (COLS - 1) * P + 220
H = Y0 + P * 15.2 + 390
add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H:.0f}" viewBox="0 0 {W} {H:.0f}">')
add('<rect width="100%" height="100%" fill="white"/>')
text(24, 34, "Head bench test: breadboard layout", 22, "start", weight="bold")
text(24, 58, "Rounded tags are wires that leave the board (Freddy's cables and the adapter up top, ESP32 pins below). "
     "Tag wires pass OVER the rails; they don't plug into them.", 13, "start", "#555")
text(24, 78, "Match rails by color and name: put VMOT on the + rail next to row a, and GND on the outer one.",
     13, "start", "#555")

# board
left, right = X0 - 34, X0 + (COLS - 1) * P + 34
add(f'<rect x="{left}" y="{Y0 - 26}" width="{right - left}" height="{ROWS["Bx"] - Y0 + 52}" rx="12" '
    f'fill="#f4f1ea" stroke="#bbb" stroke-width="2"/>')
add(f'<rect x="{left}" y="{ROWS["e"] + P * 0.6}" width="{right - left}" height="{P * 2.4}" fill="#e7e2d7"/>')

# rails
for row, color, name in (("T-", BLUE, "GND"), ("T+", RED, "VMOT (+7.2V)"),
                         ("B-", BLUE, "GND"), ("Bx", ORANGE, "3V3 (from ESP32)")):
    y = ROWS[row]
    add(f'<line x1="{X0 - 14}" y1="{y + (-12 if row in ("T-", "B-") else 12)}" x2="{X0 + (COLS - 1) * P + 14}" '
        f'y2="{y + (-12 if row in ("T-", "B-") else 12)}" stroke="{color}" stroke-width="2.5"/>')
    text(right + 14, y + 5, name, 14, "start", color, "bold")

for row in ROWS:
    for c in range(1, COLS + 1):
        x, y = xy(c, row)
        add(f'<rect x="{x - 3.5}" y="{y - 3.5}" width="7" height="7" rx="1" fill="#c3bdb0"/>')
for c in (1, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50):
    x, _ = xy(c, "a")
    text(x, ROWS["a"] - 14, str(c), 10, color=GRAY)
    text(x, ROWS["j"] + 22, str(c), 10, color=GRAY)
for r in "abcdefghij":
    text(X0 - 22, ROWS[r] + 4, r, 11, color=GRAY)

# ------------------------------------------------------------------ power in
tag(3, "T+", "adapter + (after fuse)", RED, True, 5.2)
tag(5, "T-", "adapter −", BLUE, True, 2.2)
# GND link: top outer rail to bottom inner rail, around the left edge
x1, y1 = xy(1, "T-")
x2, y2 = xy(1, "B-")
wire((1, "T-"), (1, "B-"), BLUE, via=[(left - 22, y1), (left - 22, y2)])
text(left - 30, (y1 + y2) / 2, "GND link", 12, "end", BLUE, "bold")

# C1 across the top rails
x, y = xy(7, "T-")
add(f'<line x1="{x}" y1="{y}" x2="{x + 10}" y2="{y - 26}" stroke="#666" stroke-width="2.5"/>')
add(f'<line x1="{x}" y1="{y + P}" x2="{x + 24}" y2="{y - 26}" stroke="#666" stroke-width="2.5"/>')
add(f'<circle cx="{x + 17}" cy="{y - 42}" r="19" fill="#1f3b73" stroke="#000"/>')
text(x + 17, y - 38, "C1", 12, color="white", weight="bold")
text(x + 42, y - 52, "470–1000µF 16V", 12, "start", weight="bold")
text(x + 42, y - 37, "long leg → VMOT, stripe → GND", 12, "start")


# ------------------------------------------------------------------ TB6612 (cols 20-27, straddling the gap)
x1, ye = xy(20, "e")
x2, yf = xy(27, "f")
add(f'<rect x="{x1 - 16}" y="{ye - 18}" width="{x2 - x1 + 32}" height="{yf - ye + 36}" rx="4" fill="#c0392b" stroke="#000"/>')
text((x1 + x2) / 2, (ye + yf) / 2 + 5, "TB6612FNG", 14, color="white", weight="bold")
for i, n in enumerate(["VM", "VCC", "GND", "AO1", "AO2", "BO2", "BO1", "GND"]):
    pin(20 + i, "e", n, below=True)
for i, n in enumerate(["PWMA", "AIN2", "AIN1", "STBY", "BIN1", "BIN2", "PWMB", "GND"]):
    pin(20 + i, "f", n, below=False)

# top side
wire((20, "a"), (20, "T+"), RED)                                        # VM
wire((22, "a"), (22, "T-"), BLUE, via=[(xy(22, "a")[0] - 9, ROWS["T+"] + 6)])
wire((27, "a"), (27, "T-"), BLUE, via=[(xy(27, "a")[0] - 9, ROWS["T+"] + 6)])
tag(23, "b", "head 3 yellow", GOLD, True, 6.0)                          # AO1: eye motor (long one first)
tag(24, "b", "head 3 orange", ORANGE, True, 3.4)                        # AO2
wire((25, "d"), (34, "d"), GRAY)                                        # BO2: LED - out to column 34
tag(34, "a", "head 2 white", GRAY, True, 7.0)
resistor((26, "c"), (30, "c"), "220Ω")                                  # BO1 -> 220R -> LED +
tag(30, "a", "head 2 yellow", GOLD, True, 3.4)
# VCC (top col 21) to the 3V3 rail: around the left of the module
wire((21, "c"), (17, "Bx"), ORANGE, via=[(xy(17, "c")[0], ROWS["c"])])

# bottom side
for c in (20, 23, 26):                                                  # PWMA, STBY, PWMB -> 3V3
    wire((c, "j"), (c, "Bx"), ORANGE, via=[(xy(c, "j")[0] - 9, ROWS["B-"] + 6)])
wire((27, "j"), (27, "B-"), BLUE)                                       # GND
tag(21, "i", "GPIO39", PURPLE, False, 7.2)                              # AIN2 (long ones first)
tag(22, "i", "GPIO38", PURPLE, False, 5.0)                              # AIN1
tag(24, "i", "GPIO41", GREEN, False, 7.2)                               # BIN1
tag(25, "i", "GPIO42", GREEN, False, 5.0)                               # BIN2

# ------------------------------------------------------------------ eye home switch
tag(38, "a", "head 1 yellow", GOLD, True, 4.4)
wire((38, "e"), (38, "f"), GOLD)
resistor((38, "g"), (41, "g"), "1k")
resistor((41, "h"), (44, "h"), "10k")
wire((44, "j"), (44, "Bx"), ORANGE, via=[(xy(44, "j")[0] - 9, ROWS["B-"] + 6)])
tag(41, "j", "GPIO7", GREEN, False, 3.4)
tag(46, "T-", "head 1 white", GRAY, True, 2.2)

# ------------------------------------------------------------------ ESP32 power
tag(47, "B-", "ESP32 GND", BLUE, False, 2.2)
tag(49, "Bx", "ESP32 3V3", ORANGE, False, 4.2)

# ------------------------------------------------------------------ notes
ny = ROWS["Bx"] + 260
notes = [
    "The ESP32 runs on USB for this test. The head runs on the adapter (VMOT) through the TB6612. No Pololu needed. Stock board disconnected from the head.",
    "Bottom outer rail = 3V3 from the ESP32: it feeds the TB6612 logic (VCC), STBY, PWMA, PWMB and the 10k pull-up on the eye switch.",
    "Eye motor: head cable 3 yellow → AO1, orange → AO2 (forward = stock direction). LEDs: head cable 2 yellow → 220Ω → BO1, white → BO2.",
    "Eye switch: head cable 1 yellow → 1k → column 41 → GPIO7, 10k from column 41 to 3V3. Cable 1 white → GND.",
    "Pin order matches the common red TB6612 boards (SparkFun layout). Check the labels on yours and wire by name if they differ.",
]
for i, n in enumerate(notes):
    text(24, ny + i * 21, "• " + n, 13, "start")
add("</svg>")

open(OUT, "w").write("\n".join(svg))
print("wrote", OUT)
