#!/usr/bin/env python3
"""Regenerate the Phase 2 controller schematic sheets (rev A, module-based).

    pip install schemdraw
    python3 tools/make_controller_schematics.py

Sheets are drawn with net labels: a pin's net name means it connects to every
other pin with the same name on any sheet. The authoritative connection list is
hardware/controller/netlist.csv.
"""
import os
import schemdraw
import schemdraw.elements as elm

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "schematics")
os.makedirs(OUT, exist_ok=True)


def drawing(name):
    d = schemdraw.Drawing(file=os.path.join(OUT, name), show=False)
    d.config(fontsize=11, bgcolor="white", unit=2.2)
    return d


def title(d, text, sub=None, at=(0, 0)):
    d += elm.Label().label(text, fontsize=15, halign="left").at(at)
    if sub:
        d += elm.Label().label(sub, fontsize=10, halign="left", color="#555").at((at[0], at[1] - 0.6))


def net(d, start, direction, name, length=1.2):
    """Short stub from a point, ending in an open dot with a net label."""
    line = elm.Line().at(start)
    line = getattr(line, direction)(length)
    d += line
    loc = {"left": "left", "right": "right", "up": "top", "down": "bottom"}[direction]
    d += elm.Dot(open=True).label(name, loc, fontsize=10)


def module(d, label, left, right, at, width=3.2, top=None, bottom=None):
    """A carrier board as a box. left/right: lists of (pin_name, net_name or None)."""
    pins = []
    for i, (p, _) in enumerate(left):
        pins.append(elm.IcPin(name=p, side="left", slot=f"{len(left) - i}/{len(left)}"))
    for i, (p, _) in enumerate(right):
        pins.append(elm.IcPin(name=p, side="right", slot=f"{len(right) - i}/{len(right)}"))
    ic = elm.Ic(pins=pins, edgepadW=width, pinspacing=0.9, edgepadH=0.5).label(label, "top", fontsize=11).theta(0).at(at)
    d += ic
    for p, n in left:
        if n:
            net(d, ic[p], "left", n)
    for p, n in right:
        if n:
            net(d, ic[p], "right", n)
    return ic


# ---------------------------------------------------------------- power
with drawing("controller_power.svg") as d:
    title(d, "Controller rev A · sheet 1: power", "7.2–7.6V adapter in; VMOT for motors and elbows; separate 5V rails for logic and audio", (0, 8.6))
    d += elm.Dot(open=True).at((0, 6.5)).label("J1 barrel\n5.5×2.1, + center", "left")
    d += elm.Fuse().right().label("F1 5A\n(mini blade)")
    d += elm.PFet().right().label("Q1 P-FET\nreverse\nprotection", "bottom", ofst=0.3).reverse()
    d += elm.Line().right(1.6)
    d += (vm := elm.Dot())
    d += elm.Line().right(4.2)
    d += elm.Dot(open=True).label("VMOT", "right")
    d += elm.Capacitor(polar=True).at(vm.center).down().label("C1 1000µF 16V", "bottom", fontsize=10)
    d += elm.Ground()
    d += elm.Zener().at((vm.center[0] + 3.0, vm.center[1])).down().reverse().label("D1 TVS\nSMBJ8.5A", "bottom", fontsize=10)
    d += elm.Ground()
    d += elm.Dot(open=True).at((0, 5.5)).label("J1 GND", "left")
    d += elm.Line().right(0.8)
    d += elm.Ground()
    d += elm.Label().label("Star ground: motor returns and logic ground meet at J1", fontsize=10, halign="left", color="#555").at((0, 1.4))

    # logic 5V
    u1 = module(d, "U1 Pololu S7V7F5 (5V buck-boost, ~1A, VIN 2.7–11.8V)",
                [("VIN", "VMOT"), ("GND", "GND")], [("VOUT", None)], (2.5, -2.5), width=4.5)
    d += elm.Line().at(u1["VOUT"]).right(0.6)
    d += elm.Diode().right().label("D2 SS34\n(no USB backfeed)")
    d += (l5 := elm.Dot())
    d += elm.Line().right(0.8)
    d += elm.Dot(open=True).label("+5V_LOGIC → ESP32 5V pin", "right", fontsize=10)
    d += elm.Capacitor(polar=True).at(l5.center).down().label("C2 470µF", "bottom", fontsize=10)
    d += elm.Ground()

    # audio 5V
    module(d, "U2 MP1584 buck module (5V out; check before connecting the amp)",
           [("IN+", "VMOT"), ("IN−", "GND")], [("OUT+", "+5V_AUDIO")], (2.5, -8), width=4.5)
    d += elm.Label().label("C3 470µF across U1 VIN, C4 470µF + C5 100µF across U2 IN/OUT (not drawn). U2 OUT− = GND", fontsize=10, halign="left", color="#555").at((0, -10.2))

    # supply monitor
    d += elm.Label().label("Supply monitor", fontsize=12, halign="left").at((16, 6.9))
    d += elm.Dot(open=True).at((16, 6)).label("VMOT", "left")
    d += elm.Resistor().right().label("R1 47k")
    d += (mj := elm.Dot())
    d += elm.Line().right(0.8)
    d += elm.Dot(open=True).label("VMON → GPIO1", "right")
    d += elm.Resistor().at(mj.center).down().label("R2 22k", "bottom")
    d += elm.Ground()

# ---------------------------------------------------------------- motors
with drawing("controller_motors.svg") as d:
    title(d, "Controller rev A · sheet 2: motor drivers", "Shoulders: Pololu DRV8874 carriers (IN/IN mode). Eyes: TB6612FNG module", (0, 9.6))
    for i, side in enumerate(["R", "L"]):
        x = 2 + i * 17
        in1, in2, cs = ("GPIO11", "GPIO12", "GPIO2") if side == "R" else ("GPIO13", "GPIO14", "GPIO4")
        module(d, f"U{3 + i} DRV8874 · {'right' if side == 'R' else 'left'} shoulder",
               [("VM", "n/c (protected VM out)"), ("GND", "GND"), ("IN1", f"SH_{side}_IN1 ({in1})"), ("IN2", f"SH_{side}_IN2 ({in2})"),
                ("PMODE", "+3V3 (IN/IN)"), ("SLP", "+3V3"), ("VREF", "+3V3 · R24/25 DNP to GND")],
               [("VIN", "VMOT (reverse-protected)"), ("OUT1", f"J{3 if side == 'R' else 6}.1 motor"), ("OUT2", f"J{3 if side == 'R' else 6}.2 motor"),
                ("CS", f"→ 1k → CS_{side} ({cs})"), ("IMODE", "GND"), ("FLT", "TP (pull-up 10k)")],
               (x, 0.5), width=3.4)
    d += elm.Label().label("Pololu #4035 (2×7, rows 0.5\"). Onboard 2.49k on CS: 450µA/A × 2.49k = 1.12V/A; limit = VREF ÷ 1.12 = ≈2.9A at 3.3V. Power into VIN, not VM.", fontsize=10, halign="left", color="#555").at((0, -1.2))
    module(d, "U5 TB6612FNG · eye motor (A) + eye LEDs (B)",
           [("VM", "VMOT"), ("VCC", "+3V3"), ("GND", "GND"), ("STBY", "+3V3"), ("PWMA", "+3V3"), ("PWMB", "+3V3"),
            ("AIN1", "EYE_M1 (GPIO38)"), ("AIN2", "EYE_M2 (GPIO39)"), ("BIN1", "EYE_L1 (GPIO41)"), ("BIN2", "EYE_L2 (GPIO42)")],
           [("AO1", "J11.1 eye motor (yellow)"), ("AO2", "J11.2 eye motor (orange)"),
            ("BO1", "R3 220Ω → J10.1 LED + (yellow)"), ("BO2", "J10.2 LED − (white)")],
           (10, -12), width=3.6)
    d += elm.Label().label("PWM goes on the IN pins (PWMx tied high). Eye LEDs: one direction, ~2.8V at ~20mA (measured); R3 220Ω matches stock brightness.", fontsize=10, halign="left", color="#555").at((0, -13.4))

# ---------------------------------------------------------------- signals
with drawing("controller_signals.svg") as d:
    title(d, "Controller rev A · sheet 3: elbows, sensors, inputs", "Every external input: 1k series + pull resistor. Harness = JST SM pigtails soldered to pads", (0, 12.2))

    def elbow(y, side, gpio, j):
        d.add(elm.Label().label(f"{'Right' if side == 'R' else 'Left'} elbow · {j} (Freddy cable 2)", fontsize=12, halign="left").at((0, y + 1)))
        d.add(elm.Dot(open=True).at((0, y)).label(f"ELBOW_{side}_TX ({gpio})", "left"))
        d.add(elm.Resistor().right().label("1k"))
        d.add(elm.Line().right(1))
        d.add(elm.Dot(open=True).label(f"{j}.3 signal (row 5)", "right"))
        d.add(elm.Dot(open=True).at((5.2, y - 0.8)).label(f"{j}.2 power (row 4) = VMOT", "right"))
        d.add(elm.Dot(open=True).at((5.2, y - 1.5)).label(f"{j}.1 GND (row 3)", "right"))

    elbow(10, "R", "GPIO17 · UART1", "J4")
    elbow(6.6, "L", "GPIO18 · UART2", "J7")

    def switch_in(x, y, head, conn, gpio, pull, pull_to):
        d.add(elm.Label().label(head, fontsize=12, halign="left").at((x, y + 1)))
        d.add(elm.Dot(open=True).at((x, y)).label(conn, "left"))
        d.add(elm.Resistor().right().label("1k"))
        n = d.add(elm.Dot())
        d.add(elm.Line().right(1))
        d.add(elm.Dot(open=True).label(gpio, "right"))
        if pull_to == "GND":
            d.add(elm.Resistor().at(n.center).down().label(pull, "bottom"))
            d.add(elm.Ground())
        else:
            d.add(elm.Resistor().at(n.center).down().label(pull, "bottom"))
            d.add(elm.Dot(open=True).label("+3V3", "bottom"))

    switch_in(16, 10, "Right shoulder switch · J5", "J5.3 yellow", "SW_SH_R (GPIO5)", "100k", "GND")
    switch_in(16, 4.8, "Left shoulder switch · J8", "J8.3 yellow", "SW_SH_L (GPIO6)", "100k", "GND")
    d += elm.Label().label("J5/J8: pin 1 = +3V3 via 220Ω (R22/R23, red), 2 = GND, 3 = switch (yellow), 4 = white → TP", fontsize=10, halign="left", color="#555").at((16, 0.6))

    switch_in(0, 2.5, "Eye home switch · J9 (head cable 1)", "J9.1 yellow", "SW_EYE (GPIO7)", "10k", "3V3")
    d += elm.Label().label("J9.2 white = GND", fontsize=10, halign="left", color="#555").at((0, -1.5))
    switch_in(16, -1.4, "Trigger · J12 (3.5mm mono)", "J12 tip", "TRIG (GPIO8)", "10k", "3V3")
    d += elm.Label().label("J12 sleeve = GND; TVS (ESD5Z3V3) tip to GND", fontsize=10, halign="left", color="#555").at((16, -5.6))

    d += elm.Label().label("PIR · J13", fontsize=12, halign="left").at((0, -3))
    d += elm.Dot(open=True).at((0, -4)).label("J13.2 orange (signal)", "left")
    d += elm.Resistor().right().label("1k")
    d += elm.Line().right(1)
    d += elm.Dot(open=True).label("PIR (GPIO9, ADC) + 100k to GND + BAT54S clamp", "right")
    d += elm.Label().label("J13.1 = VMOT (yellow, power), J13.3 = GND (black). Output is ~1.3V on motion: read it with the ADC, threshold ~0.6V.", fontsize=10, halign="left", color="#555").at((0, -5))

    d += elm.Label().label("I2C · J14 (JST-SH 4-pin, Qwiic/STEMMA QT)", fontsize=12, halign="left").at((16, -6.8))
    d += elm.Label().label("GND, +3V3, SDA = GPIO15, SCL = GPIO16; 4.7k pull-ups to 3V3.\nFor INA219 (current) and MPU6050 (shoulder angle).", fontsize=10, halign="left").at((16, -8))

# ---------------------------------------------------------------- audio
with drawing("controller_audio.svg") as d:
    title(d, "Controller rev A · sheet 4: audio", "One I2S bus, full duplex: amplifier out, optional microphone in", (0, 7))
    module(d, "U6 MAX98357A I2S amp module (3W, class D)",
           [("VIN", "+5V_AUDIO"), ("GND", "GND"), ("BCLK", "I2S_BCLK (GPIO40)"), ("LRC", "I2S_WS (GPIO21)"),
            ("DIN", "I2S_DOUT (GPIO47)"), ("GAIN", "JP1: open 9dB / GND 12dB / VIN 6dB"), ("SD", None)],
           [("SPK+", "J15.1 speaker + (pad)"), ("SPK−", "J15.2 speaker − (pad)")], (3, -1), width=3.4)
    module(d, "J16 microphone header (INMP441 module)",
           [("VDD", "+3V3"), ("GND", "GND"), ("SCK", "I2S_BCLK (GPIO40)"), ("WS", "I2S_WS (GPIO21)"),
            ("SD", "I2S_DIN (GPIO10)"), ("L/R", "GND (left)")], [], (20, -1), width=2.6)
    d += elm.Label().label("Speaker: 4–8Ω, up to ~3W. Keep the amp and speaker leads away from the motor drivers.", fontsize=10, halign="left", color="#555").at((0, -6.8))

# ---------------------------------------------------------------- esp32
with drawing("controller_esp32.svg") as d:
    title(d, "Controller rev A · sheet 5: ESP32-S3 dev board", "Plugs into 2×22 female headers. Avoids strapping pins (0, 3, 45, 46), USB (19, 20), PSRAM (35–37) and UART0 (43, 44)", (0, 14.6))
    module(d, "ESP32-S3 DevKitC-1 style (N16R8)",
           [("5V", "+5V_LOGIC (via D2)"), ("3V3", "+3V3 (out, sensors/logic)"), ("GND", "GND"),
            ("GPIO1", "VMON"), ("GPIO2", "CS_R"), ("GPIO4", "CS_L"), ("GPIO5", "SW_SH_R"), ("GPIO6", "SW_SH_L"),
            ("GPIO7", "SW_EYE"), ("GPIO8", "TRIG"), ("GPIO9", "PIR"), ("GPIO10", "I2S_DIN (mic)"),
            ("GPIO11", "SH_R_IN1"), ("GPIO12", "SH_R_IN2")],
           [("GPIO13", "SH_L_IN1"), ("GPIO14", "SH_L_IN2"), ("GPIO15", "I2C_SDA"), ("GPIO16", "I2C_SCL"),
            ("GPIO17", "ELBOW_R_TX"), ("GPIO18", "ELBOW_L_TX"), ("GPIO21", "I2S_WS"), ("GPIO38", "EYE_M1"),
            ("GPIO39", "EYE_M2"), ("GPIO40", "I2S_BCLK"), ("GPIO41", "EYE_L1"), ("GPIO42", "EYE_L2"),
            ("GPIO47", "I2S_DOUT (amp)"), ("GPIO48", "onboard RGB LED")],
           (6, 0), width=5)

# ---------------------------------------------------------------- placement (to scale, mm)
# Cables leave by body part: head at the top, left arm on the left, right arm on the
# right, power at the bottom. Viewed from the component side, head end up.
W, H, S = 100, 100, 7  # board size in mm (≤100×100 keeps JLCPCB's cheapest price), px per mm
blocks = [
    # (label, x, y, w, h, color)  origin top-left, mm
    ("Head: J9 · J10 · J11", 26, 2, 36, 9, "#e0f2fe"),
    ("J15 speaker\n(solder pads)", 66, 2, 16, 9, "#ede9fe"),
    ("U5 TB6612\neyes", 12, 13, 20, 20, "#dcfce7"),
    ("U6 MAX98357A\namp", 66, 13, 20, 20, "#ede9fe"),
    ("antenna keep-out\n(no copper)", 38, 24, 24, 14, "#fef9c3"),
    ("U2 MP1584\naudio 5V", 68, 36, 12, 16, "#fff1d6"),
    ("Left arm:\nJ6 motor\nJ7 elbow\nJ8 switch", 2, 36, 11, 40, "#e0f2fe"),
    ("U4 DRV8874\nleft", 15, 40, 18, 20, "#dcfce7"),
    ("ESP32-S3 dev board\n(antenna up,\nUSB-C at the\nbottom edge)", 36, 34, 28.1, 63.6, "#dbeafe"),
    ("U3 DRV8874\nright", 67, 56, 18, 20, "#dcfce7"),
    ("Right arm:\nJ3 motor\nJ4 elbow\nJ5 switch", 87, 36, 11, 40, "#e0f2fe"),
    ("U1 S7V7F5\nlogic 5V", 21, 64, 10, 13, "#fff1d6"),
    ("J1 barrel · J2 terminal\nF1 fuse · Q1 · C1 · D1", 6, 80, 28, 14, "#fde2e2"),
    ("J12 trigger · J13 PIR\nJ14 I2C · J16 mic", 66, 80, 28, 14, "#e5e7eb"),
]
svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W * S + 160} {H * S + 120}" font-family="sans-serif">',
       f'<rect width="100%" height="100%" fill="white"/>',
       f'<text x="20" y="28" font-size="18" font-weight="600">Controller rev A · placement sketch (to scale, {W}×{H} mm, 2-layer, component side)</text>',
       f'<g transform="translate(80,64)"><rect x="0" y="0" width="{W * S}" height="{H * S}" rx="10" fill="#f8fafc" stroke="#334155" stroke-width="2"/>']
for (lbl, x, y, w, h, c) in blocks:
    svg.append(f'<rect x="{x * S}" y="{y * S}" width="{w * S}" height="{h * S}" rx="6" fill="{c}" stroke="#64748b"/>')
    lines = lbl.split("\n")
    for k, ln in enumerate(lines):
        svg.append(f'<text x="{(x + w / 2) * S}" y="{(y + h / 2) * S + (k - (len(lines) - 1) / 2) * 16 + 5}" font-size="13" text-anchor="middle">{ln}</text>')
for (cx, cy) in [(4, 4), (96, 4), (4, 96), (96, 96)]:
    svg.append(f'<circle cx="{cx * S}" cy="{cy * S}" r="{1.6 * S}" fill="white" stroke="#334155"/>')
# edge labels: where the cables go
svg.append(f'<text x="{W * S / 2}" y="-12" font-size="14" font-weight="600" text-anchor="middle" fill="#0369a1">TOP: head cables + speaker</text>')
svg.append(f'<text x="{W * S / 2}" y="{H * S + 26}" font-size="14" font-weight="600" text-anchor="middle" fill="#b91c1c">BOTTOM: power in · USB-C · trigger, PIR, I2C</text>')
svg.append(f'<text x="-40" y="{H * S / 2}" font-size="14" font-weight="600" text-anchor="middle" fill="#0369a1" transform="rotate(-90 -40 {H * S / 2})">LEFT ARM</text>')
svg.append(f'<text x="{W * S + 40}" y="{H * S / 2}" font-size="14" font-weight="600" text-anchor="middle" fill="#0369a1" transform="rotate(90 {W * S + 40} {H * S / 2})">RIGHT ARM</text>')
svg.append(f'<text x="{W * S / 2}" y="{H * S + 48}" font-size="12" text-anchor="middle" fill="#475569">Motor power runs up the sides from F1 to each arm driver · audio stays at the top, away from the motor drivers · M3 holes in the corners</text>')
svg.append('</g></svg>')
os.makedirs(os.path.join(HERE, "..", "hardware", "controller"), exist_ok=True)
with open(os.path.join(HERE, "..", "hardware", "controller", "placement.svg"), "w") as f:
    f.write("\n".join(svg))

print("wrote controller sheets and placement")
