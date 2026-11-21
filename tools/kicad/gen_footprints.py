"""Write the custom footprint library hardware/controller/kicad/freddy.pretty."""
import os
from sexpr import Sym as S, dump

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "hardware", "controller", "kicad", "freddy.pretty")
P = 2.54
M3D = "${KICAD9_3DMODEL_DIR}"


def font(sz=1.0, th=0.15):
    return [S("effects"), [S("font"), [S("size"), sz, sz], [S("thickness"), th]]]


def prop(name, val, x, y, layer, sz=1.0, hide=False):
    p = [S("property"), name, val, [S("at"), x, y, 0], [S("layer"), layer]]
    if hide:
        p.append([S("hide"), S("yes")])
    p.append(font(sz))
    return p


def rect(x0, y0, x1, y1, layer, w=0.12):
    return [S("fp_rect"), [S("start"), x0, y0], [S("end"), x1, y1],
            [S("stroke"), [S("width"), w], [S("type"), S("solid")]], [S("fill"), S("no")], [S("layer"), layer]]


def line(x0, y0, x1, y1, layer, w=0.12):
    return [S("fp_line"), [S("start"), x0, y0], [S("end"), x1, y1],
            [S("stroke"), [S("width"), w], [S("type"), S("solid")]], [S("layer"), layer]]


def text(t, x, y, layer, sz=0.8, justify=None):
    eff = font(sz, max(0.15, sz * 0.15))
    if justify:
        eff.append([S("justify"), S(justify)])
    return [S("fp_text"), S("user"), t, [S("at"), x, y, 0], [S("layer"), layer], eff]


def pin_labels(names, x, y0, side, sz=0.8):
    """Silkscreen pin names for a column of pads at x, written on the inside (side=+1: labels to the right)."""
    # centred (not justified) so the text stays put when KiCad flips it upright on a 180-degree part;
    # the stroke font advances about one text-height per character
    return [text(n, x + side * (1.15 + 0.5 * sz * len(n)), y0 + i * P, "F.SilkS", sz) for i, n in enumerate(names)]


def tht(num, x, y, dia=1.7, drill=1.0, square=False):
    return [S("pad"), str(num), S("thru_hole"), S("rect" if square else "circle"), [S("at"), x, y],
            [S("size"), dia, dia], [S("drill"), drill], [S("layers"), "*.Cu", "*.Mask"],
            [S("remove_unused_layers"), S("no")]]


def npth_slot(x, y, w, h):
    return [S("pad"), "", S("np_thru_hole"), S("oval"), [S("at"), x, y], [S("size"), w, h],
            [S("drill"), S("oval"), w, h], [S("layers"), "*.Cu", "*.Mask"]]


def model(path, x, y, z=0.0, rz=0.0):
    return [S("model"), path, [S("offset"), [S("xyz"), x, -y, z]], [S("scale"), [S("xyz"), 1, 1, 1]],
            [S("rotate"), [S("xyz"), 0, 0, rz]]]


def strip(x, y0, n, layer="F.SilkS", m=1.27):
    """Outline around a vertical strip of n pins starting at (x, y0)."""
    return rect(x - m, y0 - m, x + m, y0 + (n - 1) * P + m, layer)


def hstrip(x0, y, n, layer="F.SilkS", m=1.27):
    return rect(x0 - m, y - m, x0 + (n - 1) * P + m, y + m, layer)


def footprint(name, descr, items, ref_xy=(0, -3), val_xy=(0, 3), attr="through_hole"):
    fp = [S("footprint"), name, [S("version"), 20241229], [S("generator"), "freddy"], [S("layer"), "F.Cu"],
          [S("descr"), descr],
          prop("Reference", "REF**", ref_xy[0], ref_xy[1], "F.SilkS"),
          prop("Value", name, val_xy[0], val_xy[1], "F.Fab"),
          prop("Datasheet", "", 0, 0, "F.Fab", hide=True),
          prop("Description", descr, 0, 0, "F.Fab", hide=True),
          [S("attr"), S(attr)]]
    fp += items
    return fp


def write(fp):
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, fp[1] + ".kicad_mod"), "w") as f:
        f.write(dump(fp) + "\n")


def esp32():
    it = []
    y0 = -26.67
    for i in range(22):
        it.append(tht(i + 1, -12.7, y0 + i * P, square=(i == 0)))
        it.append(tht(i + 23, 12.7, y0 + i * P, square=(i == 0)))
    it += [strip(-12.7, y0, 22), strip(12.7, y0, 22),
           strip(-12.7, y0, 22, "F.CrtYd", 1.52), strip(12.7, y0, 22, "F.CrtYd", 1.52),
           rect(-14.05, -34.17, 14.05, 29.43, "F.Fab", 0.1),
           rect(-9.0, -34.17, 9.0, -27.5, "F.Fab", 0.1),
           text("ESP32-S3 (antenna this end)", 0, -32.6, "F.SilkS", 0.9),
           text("USB-C at this end", 0, 31.0, "F.SilkS", 0.8),
           text("${REFERENCE}", 0, 0, "F.Fab", 1.0),
           model(f"{M3D}/Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x22_P2.54mm_Vertical.step", -12.7, y0),
           model(f"{M3D}/Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x22_P2.54mm_Vertical.step", 12.7, y0)]
    write(footprint("ESP32-S3-DevKitC-1_Socket",
                    "ESP32-S3-DevKitC-1 style dev board (2x22, rows 25.4mm) on female headers. Board 28.1 x 63.6mm",
                    it, ref_xy=(-12.7, -30.0)))


def drv8874():
    it = []
    for i in range(7):
        it.append(tht(i + 1, 6.35, -7.62 + i * P, square=(i == 0)))     # VIN column
        it.append(tht(i + 8, -6.35, -7.62 + i * P, square=(i == 0)))    # VM column
    labels = ["VIN", "GND", "OUT1", "OUT2", "IMODE", "FLT", "CS"]
    labels2 = ["VM", "GND", "IN1", "IN2", "PMODE", "SLP", "VREF"]
    it += pin_labels(labels, 6.35, -7.62, -1) + pin_labels(labels2, -6.35, -7.62, +1)
    it += [strip(6.35, -7.62, 7), strip(-6.35, -7.62, 7),
           strip(6.35, -7.62, 7, "F.CrtYd", 1.52), strip(-6.35, -7.62, 7, "F.CrtYd", 1.52),
           rect(-7.62, -8.89, 7.62, 8.89, "F.Fab", 0.1),
           text("DRV8874 CHIP UP", 0, 10.2, "F.SilkS", 0.8),
           text("${REFERENCE}", 0, 0, "F.Fab", 1.0),
           model(f"{M3D}/Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x07_P2.54mm_Vertical.step", 6.35, -7.62),
           model(f"{M3D}/Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x07_P2.54mm_Vertical.step", -6.35, -7.62)]
    write(footprint("Pololu_DRV8874_Carrier_Socket",
                    "Pololu #4035 DRV8874 carrier on female headers, mounted parts-up (its labels are on the bottom). Parts-side view: VIN column (1-7) right, VM column (8-14) left. 0.6x0.7in, rows 0.5in",
                    it, ref_xy=(0, -10.2 - 1.4)))


def tb6612():
    """Red TB6612 clone (labels printed on the bottom) mounted chip-up: seen from above, the logic
    column (1-8) is on the right and VM/outputs (9-16) on the left. Rev A had these mirrored."""
    it = []
    y0 = -8.89
    logic = ["PWMA", "AIN2", "AIN1", "STBY", "BIN1", "BIN2", "PWMB", "GND"]
    power = ["VM", "VCC", "GND", "AO1", "AO2", "BO2", "BO1", "GND"]
    for i in range(8):
        it.append(tht(i + 1, 7.62, y0 + i * P, square=(i == 0)))
        it.append(tht(i + 9, -7.62, y0 + i * P, square=(i == 0)))
    it += pin_labels(logic, 7.62, y0, -1) + pin_labels(power, -7.62, y0, +1)
    it += [strip(-7.62, y0, 8), strip(7.62, y0, 8),
           strip(-7.62, y0, 8, "F.CrtYd", 1.52), strip(7.62, y0, 8, "F.CrtYd", 1.52),
           rect(-10.15, -10.4, 10.15, 10.4, "F.Fab", 0.1),
           text("TB6612 CHIP UP", 0, -11.0, "F.SilkS", 0.8),
           text("${REFERENCE}", 0, 0, "F.Fab", 1.0),
           model(f"{M3D}/Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x08_P2.54mm_Vertical.step", -7.62, y0),
           model(f"{M3D}/Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x08_P2.54mm_Vertical.step", 7.62, y0)]
    write(footprint("TB6612FNG_Clone_ChipUp_Socket",
                    "Red TB6612FNG clone (2x8, rows 0.6in, labels on the bottom) mounted chip-up on female headers",
                    it, ref_xy=(0, -12.3)))


def amp():
    it = [tht(i + 1, -7.62 + i * P, 0, square=(i == 0)) for i in range(7)]
    it += [tht(8, -1.75, -13.0, dia=1.8, drill=1.0, square=True), tht(9, 1.75, -13.0, dia=1.8, drill=1.0)]
    it += [hstrip(-7.62, 0, 7), hstrip(-7.62, 0, 7, "F.CrtYd", 1.52),
           rect(-3.4, -14.6, 3.4, -11.4, "F.CrtYd", 0.05),
           rect(-8.9, -17.0, 8.9, 2.3, "F.Fab", 0.1),
           text("SPK+", -1.75, -15.3, "F.SilkS", 0.7), text("SPK-", 1.75, -10.8, "F.SilkS", 0.7),
           *[text(n, -7.62 + i * P, -2.2 if i % 2 == 0 else -3.3, "F.SilkS", 0.8)
             for i, n in enumerate(["LRC", "BCLK", "DIN", "GAIN", "SD", "GND", "VIN"])],
           text("${REFERENCE}", 0, -6, "F.Fab", 1.0),
           model(f"{M3D}/Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x07_P2.54mm_Vertical.step", -7.62, 0, rz=-90),
           model(f"{M3D}/Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x01_P2.54mm_Vertical.step", -1.75, -13.0),
           model(f"{M3D}/Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x01_P2.54mm_Vertical.step", 1.75, -13.0)]
    write(footprint("MAX98357A_Breakout_Socket",
                    "Adafruit-style MAX98357A breakout: 1x7 row on a female header plus two single-pin sockets under the speaker holes (3.5mm apart, 13mm from the row, centred on pin 4)",
                    it, ref_xy=(0, -18.0)))


def mp1584():
    pads = [("1", 4.15, 6.6), ("2", -4.15, 6.6), ("3", 4.15, -6.6), ("4", -4.15, -6.6)]
    it = [tht(n, x, y, dia=1.8, drill=1.0, square=(n == "1")) for n, x, y in pads]
    it += [rect(-5.6, -8.2, 5.6, 8.2, "F.SilkS"), rect(-5.85, -8.45, 5.85, 8.45, "F.CrtYd", 0.05),
           text("IN+", 3.2, 4.6, "F.SilkS", 0.7), text("IN-", -3.2, 4.6, "F.SilkS", 0.7),
           text("OUT+", 3.0, -4.6, "F.SilkS", 0.7), text("OUT-", -3.0, -4.6, "F.SilkS", 0.7),
           text("${REFERENCE}", 0, 0, "F.Fab", 1.0)]
    for _, x, y in pads:
        it.append(model(f"{M3D}/Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x01_P2.54mm_Vertical.step", x, y))
    write(footprint("MP1584_Module",
                    "MP1584 fixed-5V buck module: four corner pads on an 8.3 x 13.2mm grid (single-pin sockets or solder flat)",
                    it, ref_xy=(0, -9.4)))


def s7v7f5():
    it = [tht(i + 1, -2.54 + i * P, 0, square=(i == 0)) for i in range(3)]
    it += [rect(-4.45, -1.6, 4.45, 1.6, "F.SilkS"), rect(-4.7, -1.85, 4.7, 1.85, "F.CrtYd", 0.05),
           text("VIN", -2.54, 2.4, "F.SilkS", 0.8), text("GND", 0, 3.5, "F.SilkS", 0.8), text("VOUT", 2.54, 2.4, "F.SilkS", 0.8),
           model(f"{M3D}/Connector_PinSocket_2.54mm.3dshapes/PinSocket_1x03_P2.54mm_Vertical.step", -2.54, 0, rz=-90)]
    write(footprint("Pololu_S7V7F5_Upright",
                    "Pololu S7V7F5 (reg09a, 3-pin) standing upright on a 1x3 female header",
                    it, ref_xy=(0, -2.8)))


def pigtail(n):
    """Pads for a JST SM pigtail soldered in. Wires leave toward -x; zip-tie slots straddle the bundle."""
    it = [tht(i + 1, 0, i * P, dia=1.9, drill=1.2, square=(i == 0)) for i in range(n)]
    ylo, yhi = -2.9, (n - 1) * P + 2.9
    it += [npth_slot(-4.4, ylo, 3.0, 1.5), npth_slot(-4.4, yhi, 3.0, 1.5),
           rect(-6.15, ylo - 1.0, 1.2, yhi + 1.0, "F.CrtYd", 0.05),
           line(-1.6, -1.3, -1.6, (n - 1) * P + 1.3, "F.SilkS"),
           line(-3.0, ylo, -5.8, ylo, "F.SilkS"), line(-3.0, yhi, -5.8, yhi, "F.SilkS")]
    write(footprint(f"Pigtail_1x{n}_P2.54mm_ZipTie",
                    f"Solder pads for a {n}-pin JST SM pigtail (1.2mm drill, 20-22AWG) with two zip-tie slots on the wire side",
                    it, ref_xy=(2.6, -1.0)))


def main():
    esp32(); drv8874(); tb6612(); amp(); mp1584(); s7v7f5()
    for n in (2, 3, 4):
        pigtail(n)
    print("footprints written to", os.path.normpath(OUT))


if __name__ == "__main__":
    main()
