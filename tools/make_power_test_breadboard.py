#!/usr/bin/env python3
"""Breadboard layout for the Phase 2 power test (docs/phase2-power-test.md).

    python3 tools/make_power_test_breadboard.py

Hole-by-hole layout on a standard breadboard. Wires that leave the board are
drawn as tags: Freddy's cables and the adapter go UP, ESP32 pins go DOWN.
"""
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "schematics", "power_test_breadboard.svg")

P = 26                   # hole pitch, px
COLS = 50
X0 = 140                 # x of column 1
Y0 = 260                 # y of the top outer rail

ROWS = {"T-": Y0, "T+": Y0 + P}                    # top: outer GND, inner VMOT
for i, r in enumerate("abcde"):
    ROWS[r] = Y0 + P * (2.6 + i)
for i, r in enumerate("fghij"):
    ROWS[r] = Y0 + P * (8.6 + i)
ROWS["B-"] = Y0 + P * 14.2                          # bottom: inner GND, outer unused
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
    text(x, y + (17 if below else -10), name, 8.5, color="white", weight="bold")


# ------------------------------------------------------------------ canvas
W = X0 + (COLS - 1) * P + 220
H = Y0 + P * 15.2 + 330
add(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H:.0f}" viewBox="0 0 {W} {H:.0f}">')
add('<rect width="100%" height="100%" fill="white"/>')
text(24, 34, "Power test: breadboard layout", 22, "start", weight="bold")
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
                         ("B-", BLUE, "GND"), ("Bx", GRAY, "(not used)")):
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
text(x + 42, y - 52, "1000µF 16V", 12, "start", weight="bold")
text(x + 42, y - 37, "long leg → VMOT, stripe → GND", 12, "start")

# ------------------------------------------------------------------ Pololu (pins in row c, cols 10-13)
x1, _ = xy(10, "c")
x2, _ = xy(13, "c")
yc = ROWS["c"]
add(f'<rect x="{x1 - 14}" y="{yc - 34}" width="{x2 - x1 + 28}" height="48" rx="4" fill="#2e7d32" stroke="#000"/>')
text((x1 + x2) / 2, yc - 20, "Pololu S7V7F5", 10, color="white", weight="bold")
for i, n in enumerate(["SHDN", "VIN", "GND", "VOUT"]):
    pin(10 + i, "c", n, below=False)
wire((11, "a"), (11, "T+"), RED)
wire((12, "a"), (12, "T-"), BLUE, via=[(xy(12, "a")[0] - 9, ROWS["T+"] + 6)])
wire((13, "e"), (13, "f"), ORANGE)                 # VOUT down to the bottom half
text(x1 - 16, yc + 34, "SHDN: leave open", 11, "start", GRAY)

# bottom column 13 = 5V_POL: diode to col 17, 47k to col 16
add(f'<line x1="{xy(13, "g")[0]}" y1="{ROWS["g"]}" x2="{xy(17, "g")[0]}" y2="{ROWS["g"]}" stroke="#666" stroke-width="2.5"/>')
xm = (xy(13, "g")[0] + xy(17, "g")[0]) / 2
add(f'<rect x="{xm - 20}" y="{ROWS["g"] - 8}" width="40" height="16" rx="3" fill="#222"/>')
add(f'<rect x="{xm + 12}" y="{ROWS["g"] - 8}" width="7" height="16" fill="#ddd"/>')
for c in (13, 17):
    add(f'<circle cx="{xy(c, "g")[0]}" cy="{ROWS["g"]}" r="3" fill="#666"/>')
text(xm, ROWS["g"] - 13, "D2 1N5819, stripe →", 11, weight="bold")
resistor((13, "h"), (16, "h"), "47k", below=True)
resistor((16, "i"), (19, "i"), "22k", below=True)
wire((19, "j"), (19, "B-"), BLUE)
tag(16, "j", "GPIO10", GREEN, False, 4.2)
tag(17, "j", "ESP32 5V", ORANGE, False, 2.2)

# ------------------------------------------------------------------ main rail divider (top col 21)
wire((19, "a"), (19, "T+"), RED)
resistor((19, "b"), (22, "b"), "47k")
resistor((22, "c"), (24, "c"), "22k", below=True)
wire((24, "a"), (24, "T-"), BLUE, via=[(xy(24, "a")[0] - 9, ROWS["T+"] + 6)])
wire((22, "e"), (22, "f"), GREEN)
tag(22, "j", "GPIO1", GREEN, False, 2.2)

# ------------------------------------------------------------------ DRV8876 (cols 26-31, straddling the gap)
x1, ye = xy(26, "e")
x2, yf = xy(31, "f")
add(f'<rect x="{x1 - 16}" y="{ye - 18}" width="{x2 - x1 + 32}" height="{yf - ye + 36}" rx="4" fill="#c0392b" stroke="#000"/>')
text((x1 + x2) / 2, (ye + yf) / 2 + 5, "DRV8876", 14, color="white", weight="bold")
for i, n in enumerate(["VM", "GND", "OUT1", "OUT2", "", ""]):
    pin(26 + i, "e", n, below=True)
for i, n in enumerate(["IN1", "IN2", "SLP", "PMD", "IPR", "FLT"]):
    pin(26 + i, "f", n, below=False)
wire((26, "a"), (26, "T+"), RED)
wire((27, "a"), (27, "T-"), BLUE, via=[(xy(27, "a")[0] - 9, ROWS["T+"] + 6)])
tag(28, "b", "cable 1 black", BLACK, True, 5.6)
tag(29, "b", "cable 1 red", RED, True, 3.4)
tag(26, "j", "GPIO11", PURPLE, False, 4.2)
tag(27, "j", "GPIO12", PURPLE, False, 6.2)
resistor((30, "i"), (32, "i"), "1k", below=True)
tag(32, "j", "GPIO2", GREEN, False, 2.2)
# 3V3 strip: bottom col 35 (fed from the ESP32), bridged to top col 35 for cable 3 red
wire((28, "g"), (35, "g"), ORANGE)
wire((29, "h"), (35, "h"), ORANGE)
wire((35, "e"), (35, "f"), ORANGE)
tag(35, "j", "ESP32 3V3", ORANGE, False, 4.2)
tag(35, "a", "cable 3 red", RED, True, 2.2)
text(xy(36, "i")[0] - 4, ROWS["i"] + 4, "3V3 strip", 11, "start", ORANGE, "bold")

# ------------------------------------------------------------------ shoulder switch
tag(40, "a", "cable 3 yellow", GOLD, True, 4.4)
wire((40, "e"), (40, "f"), GOLD)
resistor((40, "g"), (43, "g"), "1k")
resistor((43, "i"), (46, "i"), "100k", below=True)
wire((46, "j"), (46, "B-"), BLUE)
tag(43, "j", "GPIO5", GREEN, False, 2.2)

# ------------------------------------------------------------------ grounds out
tag(47, "T-", "cable 3 black", BLUE, True, 2.2)
tag(48, "B-", "ESP32 GND", BLUE, False, 3.4)

# ------------------------------------------------------------------ notes
ny = ROWS["Bx"] + 230
notes = [
    "Columns are 5-hole strips: a–e is one strip, f–j is another. The orange/green/gold jumpers across the gap join the two halves of a column.",
    "Main rail monitor: VMOT → col 19 → 47k → col 22 → 22k → col 24 → GND; col 22 → GPIO1.   5V monitor: col 13 → 47k → col 16 → 22k → col 19 → GND; col 16 → GPIO10.",
    "Pololu VOUT feeds column 13 (bottom). That one column is the whole \"5V rail\": the diode to the ESP32 and the 5V monitor both start there.",
    "DRV8876 pin order is a placeholder: generic modules differ. Keep the connections, go by the names on your module. SLP = nSLEEP, PMD = PMODE, IPR = IPROPI, FLT = nFAULT (leave open).",
    "Add a 1.5k from IPROPI to GND only if the module has no IPROPI resistor.   Optional elbow / PIR: see the table in docs/phase2-power-test.md.",
]
for i, n in enumerate(notes):
    text(24, ny + i * 21, "• " + n, 13, "start")
add("</svg>")

open(OUT, "w").write("\n".join(svg))
print("wrote", OUT)
