"""Write the KiCad schematic (net-label style) and the custom symbol library from design.py."""
import os
import uuid
import design
from sexpr import Sym as S, dump, parse, find, first

HERE = os.path.dirname(os.path.abspath(__file__))
KDIR = os.path.normpath(os.path.join(HERE, "..", "..", "hardware", "controller", "kicad"))
PROJECT = "freddy-controller"
STOCK = "/usr/share/kicad/symbols"

ROOT_UUID = str(uuid.uuid5(uuid.NAMESPACE_URL, "freddy-controller-root"))


def uid(*parts):
    return str(uuid.uuid5(uuid.NAMESPACE_URL, "freddy/" + "/".join(map(str, parts))))


def eff(sz=1.27, hide=False, justify=None):
    e = [S("effects"), [S("font"), [S("size"), sz, sz]]]
    if justify:
        e.append([S("justify")] + [S(j) for j in justify.split()])
    if hide:
        e.append([S("hide"), S("yes")])
    return e


# ---------------------------------------------------------------- custom symbols
GROUPS = {  # symbol name -> (ref prefix, left pin numbers, right pin numbers) ; None = automatic split
    "ESP32-S3-DevKitC-1": ("A", [str(i) for i in range(1, 23)], [str(i) for i in range(23, 45)]),
    "Pololu_DRV8874": ("U", [str(i) for i in range(8, 15)], [str(i) for i in range(1, 8)]),
    "TB6612FNG_Breakout": ("U", [str(i) for i in range(1, 9)], [str(i) for i in range(9, 17)]),
    "MAX98357A_Breakout": ("U", ["1", "2", "3", "4", "5", "6", "7"], ["8", "9"]),
    "MP1584_Module": ("U", ["1", "2"], ["3", "4"]),
    "Pololu_S7V7F5": ("U", ["1", "2"], ["3"]),
    "Jumper3": ("JP", ["1", "2", "3"], []),
    "BarrelJack": ("J", [], ["1", "2", "3"]),
    "PMOS_SSSGDDDD": ("Q", ["5", "6", "7", "8", "4"], ["1", "2", "3"]),
    "AudioJack": ("J", [], ["T", "R1", "R2", "S"]),
    "Qwiic": ("J", [], ["1", "2", "3", "4", "MP"]),
    "Mic6": ("J", [], ["1", "2", "3", "4", "5", "6"]),
    "Pigtail_2": ("J", [], ["1", "2"]),
    "Pigtail_3": ("J", [], ["1", "2", "3"]),
    "Pigtail_4": ("J", [], ["1", "2", "3", "4"]),
    "TestPoint": ("TP", ["1"], []),
    "MountingHole": ("H", [], []),
}


def custom_symbol(name, pinnames, lib_prefix=True):
    prefix, left, right = GROUPS[name]
    rows = max(len(left), len(right), 1)
    h = rows * 2.54 + 2.54
    maxname = max([len(pinnames.get(n, n)) for n in left + right] + [4])
    w = max(10.16, round((maxname * 1.27 * 2 + 5.08) / 2.54) * 2.54) if (left and right) else max(7.62, round((maxname * 1.27 + 5.08) / 2.54) * 2.54)
    top = (rows - 1) * 2.54 / 2
    full = f"Freddy:{name}" if lib_prefix else name
    body = [S("symbol"), f"{name}_0_1",
            [S("rectangle"), [S("start"), -w / 2, h / 2], [S("end"), w / 2, -h / 2],
             [S("stroke"), [S("width"), 0.254], [S("type"), S("default")]], [S("fill"), [S("type"), S("background")]]]]
    pins = [S("symbol"), f"{name}_1_1"]
    for side, nums in ((-1, left), (1, right)):
        for i, n in enumerate(nums):
            x = side * (w / 2 + 2.54)
            y = top - i * 2.54
            ang = 0 if side < 0 else 180
            pins.append([S("pin"), S("passive"), S("line"), [S("at"), x, y, ang], [S("length"), 2.54],
                         [S("name"), pinnames.get(n, n), eff()], [S("number"), n, eff()]])
    sym = [S("symbol"), full, [S("pin_names"), [S("offset"), 1.016]],
           [S("exclude_from_sim"), S("no")], [S("in_bom"), S("yes")], [S("on_board"), S("yes")],
           [S("property"), "Reference", prefix, [S("at"), 0, h / 2 + 1.27, 0], eff()],
           [S("property"), "Value", name, [S("at"), 0, -h / 2 - 1.27, 0], eff()],
           [S("property"), "Footprint", "", [S("at"), 0, 0, 0], eff(hide=True)],
           [S("property"), "Datasheet", "", [S("at"), 0, 0, 0], eff(hide=True)],
           [S("property"), "Description", "", [S("at"), 0, 0, 0], eff(hide=True)],
           body, pins]
    if not left and not right:  # mounting hole: just a circle
        sym[-2] = [S("symbol"), f"{name}_0_1", [S("circle"), [S("center"), 0, 0], [S("radius"), 1.778],
                                                 [S("stroke"), [S("width"), 0.254], [S("type"), S("default")]],
                                                 [S("fill"), [S("type"), S("none")]]]]
    return sym, w, h


def stock_symbol(lib_id):
    lib, name = lib_id.split(":")
    t = parse(open(f"{STOCK}/{lib}.kicad_sym").read())
    for s in find(t, "symbol"):
        if s[1] == name:
            s = list(s)
            s[1] = lib_id
            return s
    raise KeyError(lib_id)


def pin_points(sym):
    """{number: (x, y, angle)} in symbol coordinates (y up)."""
    out = {}

    def walk(n):
        for e in n:
            if isinstance(e, list) and e:
                if e[0] == "pin":
                    at = first(e, "at")
                    out[first(e, "number")[1]] = (float(at[1]), float(at[2]), float(at[3]))
                else:
                    walk(e)
    walk(sym)
    return out


def sym_bbox(sym):
    pts = pin_points(sym)
    xs = [p[0] for p in pts.values()] or [0]
    ys = [p[1] for p in pts.values()] or [0]
    return min(xs), max(xs), min(ys), max(ys)


# ---------------------------------------------------------------- layout
ORDER = [
    ("Power entry", ["J1", "F1", "Q1", "R26", "D1", "C1", "TP1", "TP4"]),
    ("Logic 5V", ["U1", "C3", "D2", "C2", "C7", "TP2"]),
    ("ESP32-S3", ["A1", "C11", "TP3"]),
    ("Shoulder drivers", ["U3", "R4", "C12", "R19", "R25", "C10", "U4", "R5", "C13", "R20", "R24", "C9"]),
    ("Supply monitor", ["R1", "R2", "C14"]),
    ("Arms", ["J3", "J4", "J5", "R6", "R8", "R13", "R23", "J6", "J7", "J8", "R7", "R9", "R14", "R22"]),
    ("Head", ["U5", "C6", "R3", "J9", "R10", "R15", "J10", "J11"]),
    ("Audio", ["U2", "C4", "C5", "U6", "C8", "JP1", "J15", "J16"]),
    ("Inputs", ["J12", "D3", "R11", "R16", "J13", "R12", "R21", "D4", "J14", "R17", "R18"]),
    ("Mechanical", ["H1", "H2", "H3", "H4"]),
]


def build():
    parts = {p["ref"]: p for p in design.PARTS}
    placed = [r for _, refs in ORDER for r in refs]
    missing = set(parts) - set(placed)
    assert not missing, f"not in schematic ORDER: {missing}"

    libsyms, symdefs, custom_lib = {}, {}, []
    for p in design.PARTS:
        lid = p["sym"]
        if lid in symdefs:
            continue
        if lid.startswith("Freddy:"):
            name = lid.split(":")[1]
            names = {n: nm for n, nm, _ in p["pins"]}
            sym, _, _ = custom_symbol(name, names)
            plain, _, _ = custom_symbol(name, names, lib_prefix=False)
            custom_lib.append(plain)
        else:
            sym = stock_symbol(lid)
        symdefs[lid] = sym

    items = []
    X0, Y0, COLW, YMAX = 25.4, 30.48, 111.76, 390.0
    x, y = X0, Y0

    def small(r):
        return parts[r]["sym"].startswith("Device:") and len(parts[r]["pins"]) == 2

    for title, refs in ORDER:
        # rows: big symbols alone, small 2-pin parts four to a row
        rows, cur = [], []
        for r in refs:
            if small(r):
                cur.append(r)
                if len(cur) == 4:
                    rows.append(cur); cur = []
            else:
                if cur:
                    rows.append(cur); cur = []
                rows.append([r])
        if cur:
            rows.append(cur)

        def row_h(row):
            hs = []
            for r in row:
                xmin, xmax, ymin, ymax = sym_bbox(symdefs[parts[r]["sym"]])
                hs.append(max(ymax - ymin, 5.08))
            return max(hs) + 12.7
        gh = sum(row_h(r) for r in rows) + 7.62
        if y + min(gh, 250) > YMAX:
            x += COLW
            y = Y0
        items.append([S("text"), title, [S("exclude_from_sim"), S("no")], [S("at"), x - 5.08, y, 0],
                      eff(2.5, justify="left bottom"), [S("uuid"), uid("title", title)]])
        y += 7.62
        for row in rows:
            h = row_h(row)
            if y + h > YMAX:
                x += COLW
                y = Y0 + 7.62
            for k, r in enumerate(row):
                p = parts[r]
                sym = symdefs[p["sym"]]
                xmin, xmax, ymin, ymax = sym_bbox(sym)
                sx = round((x + 25.4 + k * 20.32) / 2.54) * 2.54
                sy = round((y + ymax + 2.54) / 2.54) * 2.54
                items += place(p, sym, sx, sy)
            y += h
    return libsyms, symdefs, custom_lib, items


def place(p, sym, sx, sy):
    out = []
    pts = pin_points(sym)
    netof = {n: net for n, _, net in p["pins"]}
    su = uid("sym", p["ref"])
    xmin, xmax, ymin, ymax = sym_bbox(sym)
    in_bom = not (p["ref"].startswith("H") or p["ref"].startswith("TP"))
    node = [S("symbol"), [S("lib_id"), p["sym"]], [S("at"), sx, sy, 0], [S("unit"), 1],
            [S("exclude_from_sim"), S("no")], [S("in_bom"), S("yes" if in_bom else "no")],
            [S("on_board"), S("yes")], [S("dnp"), S("yes" if p.get("dnp") else "no")],
            [S("uuid"), su],
            [S("property"), "Reference", p["ref"], [S("at"), sx + xmax + 1.27, sy - ymax - 1.27, 0], eff(justify="left")],
            [S("property"), "Value", p["value"], [S("at"), sx + xmax + 1.27, sy - ymax + 1.27, 0], eff(justify="left")],
            [S("property"), "Footprint", p["fp"], [S("at"), sx, sy, 0], eff(hide=True)],
            [S("property"), "Datasheet", "~", [S("at"), sx, sy, 0], eff(hide=True)],
            [S("property"), "Description", "", [S("at"), sx, sy, 0], eff(hide=True)]]
    if p.get("mpn"):
        node.append([S("property"), "MPN", p["mpn"], [S("at"), sx, sy, 0], eff(hide=True)])
    for n in pts:
        node.append([S("pin"), n, [S("uuid"), uid("pin", p["ref"], n)]])
    node.append([S("instances"), [S("project"), PROJECT,
                                  [S("path"), "/" + ROOT_UUID, [S("reference"), p["ref"]], [S("unit"), 1]]]])
    out.append(node)
    for n, (px, py, ang) in pts.items():
        X, Y = sx + px, sy - py
        out_dir = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}[int(ang) % 360]  # schematic coords
        net = netof.get(n)
        if net is None:
            out.append([S("no_connect"), [S("at"), X, Y], [S("uuid"), uid("nc", p["ref"], n)]])
            continue
        X2, Y2 = X + out_dir[0] * 2.54, Y + out_dir[1] * 2.54
        out.append([S("wire"), [S("pts"), [S("xy"), X, Y], [S("xy"), X2, Y2]],
                    [S("stroke"), [S("width"), 0], [S("type"), S("default")]], [S("uuid"), uid("w", p["ref"], n)]])
        if out_dir == (1, 0):
            lab_ang, just = 0, "left bottom"
        elif out_dir == (-1, 0):
            lab_ang, just = 180, "right bottom"
        elif out_dir == (0, -1):
            lab_ang, just = 90, "left bottom"
        else:
            lab_ang, just = 270, "right bottom"
        out.append([S("global_label"), net, [S("shape"), S("passive")], [S("at"), X2, Y2, lab_ang],
                    [S("fields_autoplaced"), S("yes")], eff(justify=just.replace(" bottom", "")),
                    [S("uuid"), uid("lab", p["ref"], n)],
                    [S("property"), "Intersheetrefs", "${INTERSHEET_REFS}", [S("at"), X2, Y2, 0], eff(hide=True)]])
    return out


def main():
    _, symdefs, custom_lib, items = build()
    sch = [S("kicad_sch"), [S("version"), 20250114], [S("generator"), "eeschema"], [S("generator_version"), "9.0"],
           [S("uuid"), ROOT_UUID], [S("paper"), "A2"],
           [S("title_block"), [S("title"), "Freddy controller"], [S("rev"), "A"],
            [S("company"), "github.com/tombatron/freddy-animatronic"],
            [S("comment"), 1, "Generated by tools/kicad/gen_schematic.py from tools/kicad/design.py. Edit design.py, not this file."],
            [S("comment"), 2, "Net-label style: pins connect by label name."]],
           [S("lib_symbols")] + list(symdefs.values())]
    sch += items
    sch += [[S("sheet_instances"), [S("path"), "/", [S("page"), "1"]]], [S("embedded_fonts"), S("no")]]
    os.makedirs(KDIR, exist_ok=True)
    with open(os.path.join(KDIR, PROJECT + ".kicad_sch"), "w") as f:
        f.write(dump(sch) + "\n")
    lib = [S("kicad_symbol_lib"), [S("version"), 20241209], [S("generator"), "freddy"], [S("generator_version"), "9.0"]] + custom_lib
    with open(os.path.join(KDIR, "freddy.kicad_sym"), "w") as f:
        f.write(dump(lib) + "\n")
    with open(os.path.join(KDIR, "sym-lib-table"), "w") as f:
        f.write('(sym_lib_table\n  (version 7)\n  (lib (name "Freddy")(type "KiCad")(uri "${KIPRJMOD}/freddy.kicad_sym")(options "")(descr "Freddy controller custom symbols"))\n)\n')
    with open(os.path.join(KDIR, "fp-lib-table"), "w") as f:
        f.write('(fp_lib_table\n  (version 7)\n  (lib (name "Freddy")(type "KiCad")(uri "${KIPRJMOD}/freddy.pretty")(options "")(descr "Freddy controller custom footprints"))\n)\n')
    print("schematic written:", len(items), "items")


if __name__ == "__main__":
    main()
