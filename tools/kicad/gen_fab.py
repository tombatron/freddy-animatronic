"""Fabrication outputs for JLCPCB, plus BOM/netlist CSVs and renders, from the routed board.

Writes hardware/controller/fab/: gerbers + drill zip, JLC BOM and CPL (top-side SMD parts only),
the full BOM and netlist CSVs, schematic PDF/SVG, and PNG renders.
"""
import csv
import os
import shutil
import subprocess
import zipfile
from collections import defaultdict
import design
from gen_schematic import KDIR, PROJECT

ROOT = os.path.normpath(os.path.join(KDIR, "..", "..", ".."))
FAB = os.path.join(KDIR, "..", "fab")
PCB = os.path.join(KDIR, PROJECT + ".kicad_pcb")
SCH = os.path.join(KDIR, PROJECT + ".kicad_sch")


def run(*args):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def is_smd(p):
    return any(k in p["fp"] for k in ("_SMD:", "SOT-23", "SOIC", "D_SMA", "D_SMB", "SOD-523", "JST_SH", "PJ-320D"))


def gerbers():
    g = os.path.join(FAB, "gerbers")
    shutil.rmtree(g, ignore_errors=True)
    os.makedirs(g)
    run("kicad-cli", "pcb", "export", "gerbers", "--output", g + "/",
        "--layers", "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts",
        "--subtract-soldermask", "--no-x2", PCB)
    run("kicad-cli", "pcb", "export", "drill", "--output", g + "/", "--format", "excellon",
        "--excellon-separate-th", "--generate-map", "--map-format", "gerberx2", PCB)
    zpath = os.path.join(FAB, "freddy-controller-revA-gerbers.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(g)):
            z.write(os.path.join(g, f), f)
    return zpath


def jlc_bom_cpl():
    parts = [p for p in design.PARTS if is_smd(p) and not p.get("dnp")]
    groups = defaultdict(list)
    for p in parts:
        groups[(p["value"], p["fp"].split(":")[1], p.get("mpn", ""))].append(p["ref"])
    with open(os.path.join(FAB, "jlc_bom.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (val, fp, mpn), refs in sorted(groups.items()):
            w.writerow([mpn or val, ",".join(sorted(refs, key=lambda r: (r.rstrip("0123456789"), int("0" + r.lstrip("ABCDEFGHIJKLMNOPQRSTUVWXYZ"))))), fp, ""])
    pos = os.path.join(FAB, "pos_raw.csv")
    run("kicad-cli", "pcb", "export", "pos", "--output", pos, "--format", "csv", "--units", "mm", "--side", "front", PCB)
    keep = {p["ref"] for p in parts}
    with open(pos) as fi, open(os.path.join(FAB, "jlc_cpl.csv"), "w", newline="") as fo:
        r = csv.DictReader(fi)
        w = csv.writer(fo)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for row in r:
            if row["Ref"] in keep:
                w.writerow([row["Ref"], row["PosX"] + "mm", row["PosY"] + "mm", "Top", row["Rot"]])
    os.remove(pos)


def full_bom():
    groups = defaultdict(list)
    for p in design.PARTS:
        if p["ref"].startswith(("H", "TP")):
            continue
        how = "DNP" if p.get("dnp") else ("JLC assembly (SMD)" if is_smd(p) else "hand-solder")
        groups[(p["value"], p["fp"], p.get("mpn", ""), how)].append(p["ref"])
    with open(os.path.join(KDIR, "..", "bom.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Refs", "Qty", "Value", "Part / MPN", "Footprint", "Fitting"])
        order = lambda r: (r.rstrip("0123456789"), int("0" + "".join(ch for ch in r if ch.isdigit())))
        for (val, fp, mpn, how), refs in sorted(groups.items(), key=lambda kv: order(sorted(kv[1], key=order)[0])):
            refs = sorted(refs, key=order)
            w.writerow([" ".join(refs), len(refs), val, mpn, fp, how])
        for row in [
            ("sockets", 2, "1x22 female header 2.54mm", "A1 ESP32 (rows 25.4mm apart)", "", "hand-solder"),
            ("sockets", 4, "1x7 female header 2.54mm", "U3/U4 DRV8874, round-pin preferred (~3A pins)", "", "hand-solder"),
            ("sockets", 2, "1x8 female header 2.54mm", "U5 TB6612", "", "hand-solder"),
            ("sockets", 1, "1x7 female header 2.54mm", "U6 amp", "", "hand-solder"),
            ("sockets", 1, "1x3 female header 2.54mm", "U1 S7V7F5 (upright)", "", "hand-solder"),
            ("sockets", 6, "single-pin machined socket", "U2 MP1584 corners (4) or solder flat; U6 speaker holes (2)", "", "hand-solder"),
            ("-", 1, "male header pins", "for each module (Pololu boards ship with loose headers)", "", "hand-solder"),
            ("-", 1, "jumper cap 2.54mm", "JP1 (optional: open = 9dB)", "", "hand-solder"),
            ("enclosure", 4, "M3x8 self-tapping screw", "PCB to enclosure standoffs", "", "assembly"),
        ]:
            w.writerow(row)


def netlist():
    nets = defaultdict(list)
    for p in design.PARTS:
        for num, name, net in p["pins"]:
            if net:
                nets[net].append(f"{p['ref']}.{num}" + ("" if name in (num, "~") else f" ({name})"))
    with open(os.path.join(KDIR, "..", "netlist.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Net", "Connections"])
        for n in sorted(nets):
            w.writerow([n, ", ".join(nets[n])])


def docs_exports():
    sch_dir = os.path.join(ROOT, "schematics")
    run("kicad-cli", "sch", "export", "pdf", "--output", os.path.join(sch_dir, "controller_schematic.pdf"), SCH)
    run("kicad-cli", "sch", "export", "svg", "--output", sch_dir, "--exclude-drawing-sheet", SCH)
    src = os.path.join(sch_dir, PROJECT + ".svg")
    if os.path.exists(src):
        os.replace(src, os.path.join(sch_dir, "controller_schematic.svg"))
    r = os.path.join(KDIR, "..", "renders")
    os.makedirs(r, exist_ok=True)
    for side, name in (("top", "pcb_top.png"), ("bottom", "pcb_bottom.png")):
        run("kicad-cli", "pcb", "render", "--output", os.path.join(r, name), "--side", side, "--width", "1600",
            "--height", "1600", "--quality", "high", "--background", "opaque", PCB)
    run("kicad-cli", "pcb", "render", "--output", os.path.join(r, "pcb_3d.png"), "--width", "1800", "--height", "1300",
        "--quality", "high", "--background", "opaque", "--rotate", "-40,0,-25", "--zoom", "0.9", PCB)
    run("kicad-cli", "pcb", "export", "step", "--output", os.path.join(r, "freddy-controller.step"), "--subst-models",
        "--force", PCB)


def pin_header():
    """firmware/freddy_pcb_pins.h: the rev A GPIO map, generated so firmware and board can't drift."""
    lines = ["// Freddy controller rev A: GPIO map. Generated by tools/kicad/gen_fab.py from design.py.",
             "// Copy next to a sketch (Arduino only includes files from the sketch folder).", "#pragma once", ""]
    for io, net in sorted(design.ESP_NETS.items(), key=lambda kv: (len(kv[0]), kv[0])):
        if io.startswith("IO"):
            name = "PIN_" + net.replace("+", "").replace("-", "_")
            lines.append(f"const int {name:<16} = {io[2:]};")
    lines += [f"const int {'PIN_RGB_LED':<16} = 48;   // onboard WS2812 status LED", ""]
    path = os.path.join(ROOT, "firmware", "freddy_pcb_pins.h")
    with open(path, "w") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    os.makedirs(FAB, exist_ok=True)
    print("gerbers:", gerbers())
    jlc_bom_cpl()
    full_bom()
    netlist()
    pin_header()
    docs_exports()
    print("fab outputs in", os.path.normpath(FAB))
