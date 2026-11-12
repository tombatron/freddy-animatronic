#!/usr/bin/env python3
"""Regenerate the SVG schematics in ../schematics.

    pip install schemdraw
    python3 tools/make_schematics.py
"""
import os
import schemdraw
import schemdraw.elements as elm

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "schematics")
os.makedirs(OUT, exist_ok=True)


def drawing(name):
    d = schemdraw.Drawing(file=os.path.join(OUT, name), show=False)
    d.config(fontsize=12, bgcolor="white", unit=2.5)
    return d


# 1. Power breakout: adapter -> fuse -> stock board, with ground and +7.6V taps
with drawing("power_breakout.svg") as d:
    d += elm.Label().label("Power breakout (Phase 1)", fontsize=14, halign="left").at((0, 3.2))
    d += (jin := elm.Dot(open=True).at((0, 1.5)).label("Adapter in\n(+ center)", "left"))
    d += elm.Line().right(1)
    d += elm.Fuse().right().label("5 A")
    d += (tap := elm.Dot())
    d += elm.Line().right(2.5)
    d += elm.Dot(open=True).label("To stock board\n(or Phase 2 controller)", "right")
    d += elm.Line().at(tap.center).up(1).label("+7.6V tap (optional)", "right", ofst=0.1)
    d += elm.Dot(open=True)
    d += elm.Dot(open=True).at((0, 0)).label("Adapter GND", "left")
    d += elm.Line().right(3.5)
    d += (g := elm.Dot())
    d += elm.Line().right(2.5)
    d += elm.Dot(open=True)
    d += elm.Line().at(g.center).down(1)
    d += elm.Dot(open=True).label("GND tap: common ground for\nESP32, analyzer, tap boards", "right")
    d += elm.Ground().at((0, 0))

# 2. One tap-board channel
with drawing("tap_channel.svg") as d:
    d += elm.Label().label("Tap board channel (x10, listen-only)", fontsize=14, halign="left").at((0, 4.6))
    d += elm.Dot(open=True).at((0, 3)).label("Freddy wire\n(tapped at breakout)", "left")
    d += (hot := elm.Dot())
    d += elm.Resistor().right().label("47 kΩ")
    d += (j := elm.Dot())
    d += elm.Resistor().down().label("22 kΩ")
    d += elm.Ground()
    d += elm.Line().at(j.center).right(2)
    d += elm.Dot(open=True).label("Junction pin → ESP32 ADC\nVout = Vin × 0.319 (7.6V → 2.4V)", "right")
    d += elm.Line().at(hot.center).up(1)
    d += elm.Dot(open=True).label("Hot pin (rows 5, 7, 8 only)\nanalyzer only, full voltage", "right")

# 3. Elbow replay rig with motion sense
with drawing("elbow_replay_rig.svg") as d:
    d += elm.Label().label("Elbow replay rig (stock board disconnected)", fontsize=14, halign="left").at((0, 7.5))
    esp = elm.Ic(pins=[elm.IcPin(name="GPIO17 (TX)", side="right", slot="3/3"),
                       elm.IcPin(name="GPIO1 (ADC)", side="right", slot="2/3"),
                       elm.IcPin(name="GND", side="right", slot="1/3")],
                 edgepadW=2.6, pinspacing=1.5).label("ESP32-S3 (USB power)", "top").at((0, 2))
    d += esp
    mod = elm.Ic(pins=[elm.IcPin(name="SIG (row 5)", side="left", slot="3/3"),
                       elm.IcPin(name="7V (row 4)", side="left", slot="2/3"),
                       elm.IcPin(name="GND (row 3)", side="left", slot="1/3")],
                 edgepadW=2.6, pinspacing=1.5).label("Elbow servo module (cable 2)", "top").at((13, 2))
    d += mod
    # signal: GPIO17 -> 1k -> SIG
    d += elm.Line().at(esp["GPIO17 (TX)"]).right(1.5)
    d += elm.Resistor().right().label("1 kΩ")
    d += elm.Line().tox(mod["SIG (row 5)"])
    # power: 7.6V net -> module 7V
    d += elm.Line().at(mod["7V (row 4)"]).left(1.5)
    d += elm.Dot(open=True).label("+7.6V (adapter)", "top")
    # grounds
    d += elm.Line().at(esp["GND"]).right(1)
    d += elm.Ground()
    d += elm.Line().at(mod["GND (row 3)"]).left(1)
    d += elm.Ground()
    # motion sense: separate subcircuit joined by net labels
    d += elm.Line().at(esp["GPIO1 (ADC)"]).right(1.2)
    d += elm.Dot(open=True).label("MOTION", "right")
    d += elm.Label().label("Motion-sense divider", fontsize=12, halign="left").at((4.5, -1.6))
    d += elm.Dot(open=True).at((4.5, -2.6)).label("+7.6V", "left")
    d += elm.Resistor().right().label("47 kΩ")
    d += (sj := elm.Dot())
    d += elm.Line().right(1.5)
    d += elm.Dot(open=True).label("MOTION (→ GPIO1)\n~2.4V idle, dips when the elbow moves", "right")
    d += elm.Resistor().at(sj.center).down().label("22 kΩ", "bottom")
    d += elm.Ground()

# 4. Sniffer
with drawing("elbow_sniffer.svg") as d:
    d += elm.Label().label("Sniff mode (stock board in control)", fontsize=14, halign="left").at((0, 3.6))
    d += elm.Dot(open=True).at((0, 2)).label("Elbow signal\n(row 5 hot pin, 3.45–3.6V idle)", "left")
    d += elm.Resistor().right().label("1 kΩ")
    d += elm.Line().right(1)
    d += elm.Dot(open=True).label("ESP32 GPIO17 (RX only)\nfreddy_sniffer.ino", "right")
    d += elm.Dot(open=True).at((0, 0)).label("Tap board GND\n(Freddy ground)", "left")
    d += elm.Line().right(3.5)
    d += elm.Dot(open=True).label("ESP32 GND", "right")

# 5. Stock arm wiring (inferred)
with drawing("arm_wiring_inferred.svg") as d:
    d += elm.Label().label("One arm, as wired by the stock board (inferred from logs)", fontsize=14, halign="left").at((0, 9.2))
    d += elm.Label().label("Cable 1 · shoulder motor", fontsize=12, halign="left").at((0, 8))
    d += elm.Dot(open=True).at((0, 7)).label("H-bridge A\n(row 1, black)", "left")
    d += elm.Motor().right().label("~4 Ω")
    d += elm.Dot(open=True).label("H-bridge B\n(row 2, red)", "right")
    d += elm.Label().label("Cable 3 · shoulder sensor", fontsize=12, halign="left").at((0, 5.4))
    d += elm.Dot(open=True).at((0, 4.4)).label("3.3V (red)", "left")
    d += elm.Switch().right().label("closed once raised", "bottom")
    d += elm.Line().right(1.2)
    d += (sw := elm.Dot())
    d += elm.Line().right(1)
    d += elm.Dot(open=True).label("yellow → board input\n0V home, 3.3V raised", "right")
    d += elm.Resistor().at(sw.center).down(1.4)
    d += elm.Ground()
    d += elm.Label().label("pull-down (assumed)\nto black = GND", fontsize=11, halign="left").at((sw.center[0] + 0.4, sw.center[1] - 0.9))
    d += elm.Label().label("Cable 2 · elbow servo module", fontsize=12, halign="left").at((0, 1.4))
    d += elm.Dot(open=True).at((0, 0.6)).label("row 4: ~7V", "left")
    d += elm.Line().right(1.5)
    d += elm.Dot(open=True).at((0, -0.4)).label("row 5: serial 9600 8N1", "left")
    d += elm.Line().right(1.5)
    d += elm.Dot(open=True).at((0, -1.4)).label("row 3: GND", "left")
    d += elm.Line().right(1.5)
    d += elm.Ic(pins=[elm.IcPin(name="7V", side="left", slot="3/3"), elm.IcPin(name="SIG", side="left", slot="2/3"),
                      elm.IcPin(name="GND", side="left", slot="1/3")],
                pinspacing=1, edgepadW=1, label="servo\nmodule").anchor("SIG").at((1.5, -0.4))

# 6. Stock head wiring (inferred)
with drawing("head_wiring_inferred.svg") as d:
    d += elm.Label().label("Head, as wired by the stock board (inferred from logs)", fontsize=14, halign="left").at((0, 7.2))
    d += elm.Label().label("Cable 3 · eye sweep motor (one direction, cam)", fontsize=12, halign="left").at((0, 6))
    d += elm.Dot(open=True).at((0, 5)).label("yellow: ~7V", "left")
    d += elm.Motor().right()
    d += elm.Dot(open=True).label("orange: switched to GND\nfor the whole show", "right")
    d += elm.Label().label("Cable 2 · eye LEDs", fontsize=12, halign="left").at((0, 3.6))
    d += elm.Dot(open=True).at((0, 2.6)).label("yellow", "left")
    d += elm.LED().right().label("eyes")
    d += elm.Dot(open=True).label("white: pulled low = lit\n(flickers near show end)", "right")
    d += elm.Label().label("Cable 1 · eye home switch", fontsize=12, halign="left").at((0, 1.2))
    d += elm.Dot(open=True).at((0, 0.2)).label("yellow: pulled up to 4.8V", "left")
    d += elm.Switch().right().label("closes once per sweep\nand at straight-ahead", "bottom")
    d += elm.Dot(open=True).label("white: GND", "right")

print("wrote", sorted(os.listdir(OUT)))
