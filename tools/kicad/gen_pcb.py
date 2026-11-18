"""Build the KiCad PCB (placement, nets, outline, keep-outs, net classes, silkscreen) from design.py.

Routing is a separate step (route.py), so this file always produces an unrouted board.
"""
import os
import sys
import json
import pcbnew
import design
from gen_schematic import uid, PROJECT, KDIR

MM = pcbnew.FromMM
STOCK_FP = "/usr/share/kicad/footprints"
FREDDY_FP = os.path.join(KDIR, "freddy.pretty")
OX, OY = design.OFFSET


def V(x, y):
    return pcbnew.VECTOR2I(MM(OX + x), MM(OY + y))


def load_fp(spec):
    lib, name = spec.split(":")
    path = FREDDY_FP if lib == "Freddy" else os.path.join(STOCK_FP, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise RuntimeError("footprint not found: " + spec)
    fp.SetFPIDAsString(spec)
    return fp


def text(board, s, x, y, size=1.0, layer=pcbnew.F_SilkS, rot=0, bold=False, justify=None):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(V(x, y))
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
    t.SetTextThickness(MM(size * (0.2 if bold else 0.15)))
    if rot:
        t.SetTextAngle(pcbnew.EDA_ANGLE(rot, pcbnew.DEGREES_T))
    if justify == "left":
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
    elif justify == "right":
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_RIGHT)
    board.Add(t)
    return t


def seg(board, x0, y0, x1, y1, layer=pcbnew.Edge_Cuts, w=0.1):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(x0, y0))
    s.SetEnd(V(x1, y1))
    s.SetLayer(layer)
    s.SetWidth(MM(w))
    board.Add(s)


def arc(board, cx, cy, sx, sy, ang, layer=pcbnew.Edge_Cuts, w=0.1):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_ARC)
    s.SetCenter(V(cx, cy))
    s.SetStart(V(sx, sy))
    s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(ang, pcbnew.DEGREES_T))
    s.SetLayer(layer)
    s.SetWidth(MM(w))
    board.Add(s)


def outline(board, w, h, r=3.0):
    seg(board, r, 0, w - r, 0)
    seg(board, w, r, w, h - r)
    seg(board, w - r, h, r, h)
    seg(board, 0, h - r, 0, r)
    arc(board, w - r, r, w - r, 0, 90)
    arc(board, w - r, h - r, w, h - r, 90)
    arc(board, r, h - r, r, h, 90)
    arc(board, r, r, 0, r, 90)


def zone(board, net, layer, pts, name=None, priority=0):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    if net is not None:
        z.SetNet(net)
    ol = z.Outline()
    ol.NewOutline()
    for x, y in pts:
        ol.Append(MM(OX + x), MM(OY + y))
    z.SetLocalClearance(MM(0.3))
    z.SetMinThickness(MM(0.25))
    z.SetThermalReliefGap(MM(0.4))
    z.SetThermalReliefSpokeWidth(MM(0.6))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
    z.SetAssignedPriority(priority)
    if name:
        z.SetZoneName(name)
    board.Add(z)
    return z


def keepout(board, x0, y0, x1, y1, name, pads=False):
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetLayerSet(pcbnew.LSET.AllCuMask())
    z.SetDoNotAllowTracks(True)
    z.SetDoNotAllowVias(True)
    z.SetDoNotAllowCopperPour(True) if hasattr(z, "SetDoNotAllowCopperPour") else z.SetDoNotAllowZoneFills(True)
    z.SetDoNotAllowPads(not pads and name.startswith("ESP32"))
    z.SetDoNotAllowFootprints(False)
    ol = z.Outline()
    ol.NewOutline()
    for x, y in [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]:
        ol.Append(MM(OX + x), MM(OY + y))
    z.SetZoneName(name)
    board.Add(z)


# Freddy's wire colours per pad group (pin 1 first), for the silkscreen
WIRE_LABELS = {
    "J6": ["OUT1 blk", "OUT2 red"], "J3": ["OUT1 blk", "OUT2 red"],
    "J7": ["GND", "7V", "SIG"], "J4": ["GND", "7V", "SIG"],
    "J8": ["red 3V3", "blk GND", "yel SW", "wht NC"], "J5": ["red 3V3", "blk GND", "yel SW", "wht NC"],
    "J9": ["yel", "wht"], "J10": ["yel+", "wht-"], "J11": ["yel", "org"], "J15": ["+", "-"],
    "J13": ["yel 7V", "org SIG", "blk GND"],
}
GROUP_NAMES = {"J6": "J6 SHOULDER", "J7": "J7 ELBOW", "J8": "J8 SWITCH",
               "J3": "J3 SHOULDER", "J4": "J4 ELBOW", "J5": "J5 SWITCH",
               "J9": "J9 EYE SW", "J10": "J10 EYE LED", "J11": "J11 EYE MTR", "J15": "J15 SPEAKER",
               "J13": "J13 PIR"}


HIDE_REFS = {"A1", "U1", "U2", "U3", "U4", "U5", "U6", "J3", "J4", "J5", "J6", "J7", "J8", "J9", "J10",
             "J11", "J13", "J14", "J15"}


def build():
    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(2)
    ds.m_TrackMinWidth = MM(0.2)
    ds.m_MinClearance = MM(0.2)
    ds.m_ViasMinSize = MM(0.6)
    ds.m_MinThroughDrill = MM(0.3)
    ds.m_CopperEdgeClearance = MM(0.3)
    ds.m_HoleToHoleMin = MM(0.5)
    ds.m_SilkClearance = MM(0.0)

    ns = ds.m_NetSettings
    dflt = ns.GetDefaultNetclass()
    dflt.SetTrackWidth(MM(0.25))
    dflt.SetClearance(MM(0.2))
    dflt.SetViaDiameter(MM(0.7))
    dflt.SetViaDrill(MM(0.35))
    for name, tw, cl, vd, vdr, nets in design.NETCLASSES:
        nc = pcbnew.NETCLASS(name)
        nc.SetTrackWidth(MM(tw))
        nc.SetClearance(MM(cl))
        nc.SetViaDiameter(MM(vd))
        nc.SetViaDrill(MM(vdr))
        ns.SetNetclass(name, nc)
        for n in nets:
            ns.SetNetclassPatternAssignment(n, name)

    netobj = {}
    for n in design.nets():
        ni = pcbnew.NETINFO_ITEM(board, n)
        board.Add(ni)
        netobj[n] = ni

    for p in design.PARTS:
        fp = load_fp(p["fp"])
        fp.SetReference(p["ref"])
        fp.SetValue(p["value"])
        fp.SetPosition(V(p["x"], p["y"]))
        fp.SetOrientation(pcbnew.EDA_ANGLE(p["rot"], pcbnew.DEGREES_T))
        fp.SetPath(pcbnew.KIID_PATH("/" + uid("sym", p["ref"])))
        fp.SetSheetname("/")
        fp.SetSheetfile(PROJECT + ".kicad_sch")
        if p.get("dnp"):
            fp.SetDNP(True)
        pinnet = {}
        for num, pname, net in p["pins"]:
            if net is None:
                net = (f"unconnected-({p['ref']}-Pad{num})" if pname in (num, "~")
                       else f"unconnected-({p['ref']}-{pname}-Pad{num})")
                if net not in netobj:
                    ni = pcbnew.NETINFO_ITEM(board, net)
                    board.Add(ni)
                    netobj[net] = ni
            pinnet[num] = net
        for pad in fp.Pads():
            n = pinnet.get(pad.GetNumber())
            if n:
                pad.SetNet(netobj[n])
        # small refs; hide refs of parts that sit under a module
        ref = fp.Reference()
        ref.SetTextSize(pcbnew.VECTOR2I(MM(0.8), MM(0.8)))
        ref.SetTextThickness(MM(0.12))
        r = p["ref"]
        if r.startswith(("H", "TP")) or r in HIDE_REFS:
            ref.SetVisible(False)
        if r[0] in "RCD" and 39 < p["x"] < 61 and 41 < p["y"] < 95:
            ref.SetVisible(False)   # under the ESP32: listed on the back silkscreen instead
        board.Add(fp)

    outline(board, design.BOARD_W, design.BOARD_H)
    keepout(board, *design.KEEPOUT_ANTENNA, "ESP32 antenna keep-out")

    # keep tracks and vias clear of the zip-tie slots
    k = 0
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH and fp.GetReference().startswith("J"):
                bb = pad.GetBoundingBox()
                x0, y0 = pcbnew.ToMM(bb.GetLeft()) - OX - 0.6, pcbnew.ToMM(bb.GetTop()) - OY - 0.6
                x1, y1 = pcbnew.ToMM(bb.GetRight()) - OX + 0.6, pcbnew.ToMM(bb.GetBottom()) - OY + 0.6
                keepout(board, x0, y0, x1, y1, f"slot {fp.GetReference()} {k}")
                k += 1
    silk(board)
    return board


def add_zones(board):
    """Copper pours, added after routing so the autorouter routes every net with tracks."""
    W, H = design.BOARD_W, design.BOARD_H
    gnd = board.FindNet("GND")
    zone(board, gnd, pcbnew.B_Cu, [(0, 0), (W, 0), (W, H), (0, H)], "GND bottom", 0)
    zone(board, gnd, pcbnew.F_Cu, [(0, 0), (W, 0), (W, H), (0, H)], "GND top", 0)
    for net, pts in design.POWER_ZONES:
        zone(board, board.FindNet(net), pcbnew.F_Cu, pts, net + " pour", 5)
    for z in board.Zones():
        if not z.GetIsRuleArea():
            z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)


def silk(board):
    parts = {p["ref"]: p for p in design.PARTS}
    for ref, labels in WIRE_LABELS.items():
        p = parts[ref]
        fp = board.FindFootprintByReference(ref)
        pads = sorted(fp.Pads(), key=lambda q: int(q.GetNumber()) if q.GetNumber().isdigit() else 99)
        pads = [q for q in pads if q.GetNumber().isdigit()]
        rot = p["rot"]
        for q, lab in zip(pads, labels):
            x = pcbnew.ToMM(q.GetPosition().x) - OX
            y = pcbnew.ToMM(q.GetPosition().y) - OY
            if rot == 0:          # left arm: labels to the right of the pads
                text(board, lab, x + 1.6, y, 0.8, justify="left")
            elif rot == 180:      # right arm
                text(board, lab, x - 1.6, y, 0.8, justify="right")
            elif rot == 270:      # head: below the pads, vertical
                text(board, lab, x, y + 1.6, 0.8, rot=90, justify="right")
            else:                 # PIR: above the pads
                text(board, lab, x, y - 1.6, 0.8, rot=90, justify="left")
        xs = [pcbnew.ToMM(q.GetPosition().x) - OX for q in pads]
        ys = [pcbnew.ToMM(q.GetPosition().y) - OY for q in pads]
        name = GROUP_NAMES[ref]
        if rot == 0:
            text(board, name, min(xs) - 1.0, max(ys) + 2.5, 0.85, bold=True, justify="left")
        elif rot == 180:
            text(board, name, max(xs) + 1.0, max(ys) + 2.5, 0.85, bold=True, justify="right")
        elif rot == 270:
            text(board, name, (min(xs) + max(xs)) / 2, 2.0, 0.8, bold=True)
        else:
            text(board, name, max(xs) + 2.2, max(ys), 0.85, bold=True, justify="left")
    # edge legends and title
    text(board, "LEFT ARM", 1.6, 27, 1.2, rot=90, bold=True)
    text(board, "RIGHT ARM", 98.4, 27, 1.2, rot=270, bold=True)
    text(board, "HEAD", 64.5, 2.2, 1.2, bold=True)
    text(board, "FREDDY CONTROLLER rev A", 50, 22.2, 1.3, bold=True)
    t = text(board, "github.com/tombatron/freddy-animatronic", 50, 60.0, 1.2, layer=pcbnew.B_SilkS)
    t.SetMirrored(True)
    t = text(board, "Freddy controller rev A", 50, 56.0, 2.0, layer=pcbnew.B_SilkS, bold=True)
    t.SetMirrored(True)
    under = sorted([p["ref"] for p in design.PARTS if p["ref"][0] in "RCD" and 39 < p["x"] < 61 and 41 < p["y"] < 95],
                   key=lambda r: (r[0], int(r[1:])))
    t = text(board, "Under the ESP32: " + " ".join(under), 50, 64.0, 0.9, layer=pcbnew.B_SilkS)
    t.SetMirrored(True)
    text(board, "7.2V IN", 6.0, 86.0, 1.0, rot=90, bold=True)
    text(board, "5A MINI FUSE", 23.2, 92.0, 0.9, rot=90, bold=True)
    text(board, "TRIG", 69.6, 84.4, 0.9, bold=True)
    for lbl, x, y in [("U5 TB6612", 22.0, 23.6), ("U4 DRV8874", 24.85, 46.8), ("U3 DRV8874", 75.15, 64.6),
                      ("U6 AMP", 76.0, 24.4), ("U2 5V AUDIO", 74.0, 44.6), ("U1 5V LOGIC", 27.0, 59.4)]:
        text(board, lbl, x, y, 0.9, bold=True)
    for i, lbl in enumerate(["VMOT", "5V", "3V3", "GND"]):
        text(board, lbl, 36.4, 25.5 + i * 3.4, 0.8, justify="left")
    text(board, "6dB", 89.5, 13.8, 0.8)
    text(board, "12dB", 94.6, 13.8, 0.8)
    text(board, "GAIN: open = 9dB", 92.0, 19.4, 0.8)
    text(board, "QWIIC I2C", 87.6, 14.6, 0.8, bold=True)
    text(board, "MIC (INMP441, later)", 74.0, 79.0, 0.8)
    text(board, "NO COPPER: antenna", 50, 31.0, 0.9)


def save(board):
    os.makedirs(KDIR, exist_ok=True)
    path = os.path.join(KDIR, PROJECT + ".kicad_pcb")
    board.Save(path) if hasattr(board, "Save") else pcbnew.SaveBoard(path, board)
    write_project()
    return path


def write_project():
    """Project file with the net classes (KiCad keeps them in the .kicad_pro)."""
    classes = [{"name": "Default", "track_width": 0.25, "clearance": 0.2, "via_diameter": 0.7, "via_drill": 0.35,
                "microvia_diameter": 0.3, "microvia_drill": 0.1, "diff_pair_width": 0.2, "diff_pair_gap": 0.25,
                "diff_pair_via_gap": 0.25, "wire_width": 6, "bus_width": 12, "line_style": 0,
                "pcb_color": "rgba(0, 0, 0, 0.000)", "schematic_color": "rgba(0, 0, 0, 0.000)", "priority": 2147483647}]
    patterns = []
    for i, (name, tw, cl, vd, vdr, nets) in enumerate(design.NETCLASSES):
        classes.append({"name": name, "track_width": tw, "clearance": cl, "via_diameter": vd, "via_drill": vdr,
                        "microvia_diameter": 0.3, "microvia_drill": 0.1, "diff_pair_width": 0.2, "diff_pair_gap": 0.25,
                        "diff_pair_via_gap": 0.25, "wire_width": 6, "bus_width": 12, "line_style": 0,
                        "pcb_color": "rgba(0, 0, 0, 0.000)", "schematic_color": "rgba(0, 0, 0, 0.000)", "priority": i})
        patterns += [{"netclass": name, "pattern": n} for n in nets]
    pro = {
        "board": {"design_settings": {"rules": {"min_clearance": 0.2, "min_track_width": 0.2, "min_via_diameter": 0.6,
                                                "min_through_hole_diameter": 0.3, "min_copper_edge_clearance": 0.3,
                                                "min_hole_to_hole": 0.5, "min_silk_clearance": 0.0},
                                    "rule_severities": {"starved_thermal": "warning", "silk_overlap": "warning",
                                                        "silk_over_copper": "warning", "silk_edge_clearance": "warning",
                                                        "text_height": "warning", "text_thickness": "warning"}}},
        "meta": {"filename": PROJECT + ".kicad_pro", "version": 3},
        "net_settings": {"classes": classes, "meta": {"version": 4}, "net_colors": None,
                         "netclass_assignments": None, "netclass_patterns": patterns},
        "schematic": {"legacy_lib_dir": "", "legacy_lib_list": []},
        "text_variables": {},
    }
    with open(os.path.join(KDIR, PROJECT + ".kicad_pro"), "w") as f:
        json.dump(pro, f, indent=2)
    with open(os.path.join(KDIR, PROJECT + ".kicad_dru"), "w") as f:
        f.write('(version 1)\n'
                '(rule "thermals may lose a spoke to a track"\n'
                '  (constraint min_resolved_spokes (min 1)))\n')


if __name__ == "__main__":
    b = build()
    print("saved", save(b), len(list(b.GetFootprints())), "footprints")
