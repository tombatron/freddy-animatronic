#!/usr/bin/env python3
"""Wiring for the Phase 2 breadboard power test (docs/phase2-power-test.md).

    pip install schemdraw
    python3 tools/make_power_test_schematic.py

Net labels: two points with the same name are wired together.
"""
import os
import schemdraw
import schemdraw.elements as elm

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "schematics", "power_test_rig.svg")


def net(d, start, direction, name, length=1.2):
    line = getattr(elm.Line().at(start), direction)(length)
    d += line
    loc = {"left": "left", "right": "right", "up": "top", "down": "bottom"}[direction]
    d += elm.Dot(open=True).label(name, loc, fontsize=10)


def module(d, label, left, right, at, width=3.2):
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


def divider(d, x, y, top, mid, head):
    """top net - 47k - mid (to GPIO) - 22k - GND, drawn downward from (x, y)."""
    d += elm.Label().label(head, fontsize=11, halign="left").at((x - 1, y + 1.0))
    d += elm.Dot(open=True).at((x, y)).label(top, "top", fontsize=10)
    d += elm.Resistor().down().label("47k", "bottom")
    j = d.here
    d += elm.Dot()
    d += elm.Resistor().down().label("22k", "bottom")
    d += elm.Ground()
    d += elm.Line().at(j).left(1.2)
    d += elm.Dot(open=True).label(mid, "left", fontsize=10)


def series_in(d, x, y, src, dst, head, pull=None, pull_to="GND"):
    """src - 1k - node - dst, optional pull resistor from node."""
    d += elm.Label().label(head, fontsize=11, halign="left").at((x, y + 1.0))
    d += elm.Dot(open=True).at((x, y)).label(src, "left", fontsize=10)
    d += elm.Resistor().right().label("1k", "top")
    n = d.here
    if pull:
        d += elm.Dot()
    d += elm.Line().right(1.2)
    d += elm.Dot(open=True).label(dst, "right", fontsize=10)
    if pull:
        d += elm.Resistor().at(n).down().label(pull, "bottom")
        if pull_to == "GND":
            d += elm.Ground()


with schemdraw.Drawing(file=OUT, show=False) as d:
    d.config(fontsize=11, bgcolor="white", unit=2.2)
    d += elm.Label().label("Phase 2 breadboard power test", fontsize=15, halign="left").at((0, 13.4))
    d += elm.Label().label("Net labels connect: every point named VMOT is one wire, and so on. ALL grounds go to one GND rail.",
                           fontsize=10, halign="left", color="#555").at((0, 12.8))

    # ---- power in
    d += elm.Label().label("Power in", fontsize=12, halign="left").at((0, 11.6))
    d += elm.Dot(open=True).at((0, 10.6)).label("Adapter +\n7.2V", "left", fontsize=10)
    d += elm.Fuse().right().label("F1 5A", "top")
    vm = d.here
    d += elm.Dot()
    d += elm.Line().right(1.4)
    d += elm.Dot(open=True).label("VMOT", "right", fontsize=10)
    d += elm.Capacitor(polar=True).at(vm).down().label("C1 1000µF\n16V (+ up)", "bottom", fontsize=10)
    d += elm.Ground()
    d += elm.Label().label("Adapter − → GND rail", fontsize=10, halign="left", color="#555").at((0, 7.4))

    # ---- Pololu
    u1 = module(d, "U1 Pololu S7V7F5", [("VIN", "VMOT"), ("GND", "GND")], [("VOUT", None)], at=(10.5, 9.2), width=3.0)
    d += elm.Line().at(u1["VOUT"]).right(0.8)
    j5 = d.here
    d += elm.Dot()
    d += elm.Diode().right().label("D2 1N5819\n(stripe →)", "top", fontsize=10)
    d += elm.Dot(open=True).label("ESP32 5V pin", "right", fontsize=10)
    d += elm.Line().at(j5).down(1.6)
    d += elm.Dot(open=True).label("5V_POL", "bottom", fontsize=10)

    # ---- motor driver
    module(d, "U3 DRV8876 module",
           [("IN1", "GPIO11"), ("IN2", "GPIO12"), ("nSLEEP", "+3V3"), ("PMODE", "+3V3"), ("IPROPI", "CS")],
           [("VM", "VMOT"), ("GND", "GND"), ("OUT1", "cable 1 black"), ("OUT2", "cable 1 red")],
           at=(3, 0), width=3.4)
    d += elm.Label().label("Shoulder motor = arm cable 1. OUT1 → black lifts. Pin names vary by module: check yours.",
                           fontsize=10, halign="left", color="#555").at((0, -1.4))

    # ---- dividers
    divider(d, 23.5, 9.6, "VMOT", "GPIO1", "Main rail monitor")
    divider(d, 30, 9.6, "5V_POL", "GPIO10", "5V rail monitor")

    # ---- inputs
    series_in(d, 19.5, 1.6, "CS", "GPIO2", "Motor current (IPROPI)")
    d += elm.Label().label("If the module has no IPROPI resistor, add 1.5k from CS to GND.",
                           fontsize=10, halign="left", color="#555").at((19.5, 0.0))
    series_in(d, 19.5, -2.4, "cable 3 yellow", "GPIO5", "Shoulder switch (arm cable 3)", pull="100k")
    d += elm.Label().label("cable 3 red → +3V3, black → GND, white unused",
                           fontsize=10, halign="left", color="#555").at((19.5, -5.8))

    # ---- optional
    d += elm.Label().label("Optional (worst-case runs)", fontsize=12, halign="left").at((0, -4.2))
    series_in(d, 0.5, -6.0, "GPIO17", "cable 2 signal", "Elbow (arm cable 2)")
    d += elm.Label().label("cable 2: GND pin → GND, ~7V pin → VMOT", fontsize=10, halign="left", color="#555").at((0.5, -7.0))
    series_in(d, 0.5, -9.0, "PIR orange", "GPIO9", "PIR", pull="100k")
    d += elm.Label().label("PIR yellow → VMOT, black → GND", fontsize=10, halign="left", color="#555").at((0.5, -12.6))

print("wrote", OUT)
