"""Freddy controller (see REV): the single source of truth for parts, nets and placement.

Everything else (footprint library, symbols, schematic, PCB, enclosure) is generated
from this file. Coordinates are board millimetres, origin at the board's top-left
corner, viewed from the component side with the head end up. Rotations are KiCad
degrees (counter-clockwise).
"""

REV = "B"                      # rev A (as ordered, Nov 2026) is git tag controller-revA
BOARD_W = BOARD_H = 100.0
OFFSET = (50.0, 50.0)          # where the board's top-left sits on the KiCad page

# ---------------------------------------------------------------- pin tables
ESP_LEFT = ["3V3", "3V3", "RST", "IO4", "IO5", "IO6", "IO7", "IO15", "IO16", "IO17", "IO18",
            "IO8", "IO3", "IO46", "IO9", "IO10", "IO11", "IO12", "IO13", "IO14", "5V", "GND"]
ESP_RIGHT = ["GND", "TX", "RX", "IO1", "IO2", "IO42", "IO41", "IO40", "IO39", "IO38", "IO37",
             "IO36", "IO35", "IO0", "IO45", "IO48", "IO47", "IO21", "IO20", "IO19", "GND", "GND"]
ESP_NETS = {
    "3V3": "+3V3", "5V": "+5V_LOGIC", "GND": "GND",
    "IO1": "VMON", "IO2": "CS_R", "IO4": "CS_L",
    "IO5": "SW_SH_R", "IO6": "SW_SH_L", "IO7": "SW_EYE", "IO8": "TRIG", "IO9": "PIR",
    "IO10": "I2S_DIN", "IO11": "EYE_M1", "IO12": "EYE_M2", "IO13": "SH_L_IN1", "IO14": "SH_L_IN2",
    "IO15": "EYE_L1", "IO16": "EYE_L2", "IO17": "ELBOW_R_TX", "IO18": "ELBOW_L_TX",
    "IO21": "I2S_WS", "IO38": "SH_R_IN1", "IO39": "SH_R_IN2", "IO40": "I2S_BCLK",
    "IO41": "I2C_SDA", "IO42": "I2C_SCL", "IO47": "I2S_DOUT",
}
# Pololu DRV8874 carrier, top view: pads 1-7 = VIN column, 8-14 = VM column
DRV_PINS = ["VIN", "GND", "OUT1", "OUT2", "IMODE", "FLT", "CS", "VM", "GND", "IN1", "IN2", "PMODE", "SLP", "VREF"]
# TB6612 breakout pin numbering (1-8 logic column, 9-16 VM/output column). Rev B footprint is for the
# common red clone with its labels on the BOTTOM, mounted chip-up; rev A assumed labels-up.
TB_PINS = ["PWMA", "AIN2", "AIN1", "STBY", "BIN1", "BIN2", "PWMB", "GND",
           "VM", "VCC", "GND", "AO1", "AO2", "BO2", "BO1", "GND"]
AMP_PINS = ["LRC", "BCLK", "DIN", "GAIN", "SD", "GND", "VIN", "SPK+", "SPK-"]
MP_PINS = ["IN+", "IN-", "OUT+", "OUT-"]
POLOLU_REG_PINS = ["VIN", "GND", "VOUT"]

# ---------------------------------------------------------------- parts
# Each part: ref, value, footprint, symbol, pins [(number, name, net or None)],
#            x, y, rot, extra fields
PARTS = []


def part(ref, value, fp, sym, pins, x, y, rot=0, **kw):
    PARTS.append(dict(ref=ref, value=value, fp=fp, sym=sym, pins=pins, x=x, y=y, rot=rot, **kw))


def two(net1, net2, n1="1", n2="2"):
    return [(n1, "~", net1), (n2, "~", net2)]


R0805 = "Resistor_SMD:R_0805_2012Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"


def R(ref, val, a, b, x, y, rot=0, **kw):
    part(ref, val, R0805, "Device:R", two(a, b), x, y, rot, **kw)


def C(ref, val, a, b, x, y, rot=0, **kw):
    part(ref, val, C0805, "Device:C", two(a, b), x, y, rot, **kw)


def CP(ref, val, plus, fp, x, y, rot=0, **kw):
    part(ref, val, fp, "Device:C_Polarized", two(plus, "GND"), x, y, rot, **kw)


# --- ESP32-S3 dev board (socketed)
esp_pins = []
for i, n in enumerate(ESP_LEFT):
    esp_pins.append((str(i + 1), n, ESP_NETS.get(n)))
for i, n in enumerate(ESP_RIGHT):
    esp_pins.append((str(i + 23), n, ESP_NETS.get(n)))
ESP_ORIGIN = (50.05, 68.17)
part("A1", "ESP32-S3-DevKitC-1 N16R8", "Freddy:ESP32-S3-DevKitC-1_Socket", "Freddy:ESP32-S3-DevKitC-1",
     esp_pins, *ESP_ORIGIN, 0, mpn="YD-ESP32-S3 / DevKitC-1 (in hand)", socketed=True)


def esp_pin_xy(name, nth=0):
    """Board position of an ESP32 pin by name (for placing nearby parts)."""
    hits = []
    for i, n in enumerate(ESP_LEFT):
        if n == name:
            hits.append((ESP_ORIGIN[0] - 12.7, ESP_ORIGIN[1] - 26.67 + i * 2.54))
    for i, n in enumerate(ESP_RIGHT):
        if n == name:
            hits.append((ESP_ORIGIN[0] + 12.7, ESP_ORIGIN[1] - 26.67 + i * 2.54))
    return hits[nth]


# --- shoulder drivers (Pololu DRV8874 carriers, socketed)
def drv(ref, side, x, y, rot):
    s = side
    nets = {"VIN": "VMOT", "GND": "GND", "OUT1": f"SH_{s}_OUT1", "OUT2": f"SH_{s}_OUT2", "IMODE": "GND",
            "FLT": f"FAULT_{s}", "CS": f"CS_{s}_RAW", "VM": None, "IN1": f"SH_{s}_IN1", "IN2": f"SH_{s}_IN2",
            "PMODE": "+3V3", "SLP": "+3V3", "VREF": f"VREF_{s}"}
    pins = [(str(i + 1), n, nets[n]) for i, n in enumerate(DRV_PINS)]
    part(ref, "Pololu DRV8874 carrier", "Freddy:Pololu_DRV8874_Carrier_Socket", "Freddy:Pololu_DRV8874",
         pins, x, y, rot, mpn="Pololu #4035 (in hand)", socketed=True)


drv("U3", "R", 75.15, 63.62, 0)      # VIN/OUT column faces the right arm
drv("U4", "L", 24.85, 48.62, 180)    # rotated: VIN/OUT column faces the left arm

# --- eye driver (TB6612, socketed). VM and the outputs face the left.
tb_nets = {"PWMA": "+3V3", "AIN2": "EYE_M2", "AIN1": "EYE_M1", "STBY": "+3V3", "BIN1": "EYE_L1",
           "BIN2": "EYE_L2", "PWMB": "+3V3", "GND": "GND", "VM": "VMOT", "VCC": "+3V3",
           "AO1": "EYE_MOT_A", "AO2": "EYE_MOT_B", "BO2": "EYE_LED_B", "BO1": "EYE_LED_RAW"}
part("U5", "TB6612FNG breakout", "Freddy:TB6612FNG_Clone_ChipUp_Socket", "Freddy:TB6612FNG_Breakout",
     [(str(i + 1), n, tb_nets[n]) for i, n in enumerate(TB_PINS)], 22.02, 23.29, 0,
     mpn="red TB6612FNG clone, labels on bottom, chip up (in hand)", socketed=True)

# --- audio
amp_nets = {"LRC": "I2S_WS", "BCLK": "I2S_BCLK", "DIN": "I2S_DOUT", "GAIN": "AMP_GAIN", "SD": None,
            "GND": "GND", "VIN": "+5V_AUDIO", "SPK+": "SPK+", "SPK-": "SPK-"}
part("U6", "MAX98357A breakout", "Freddy:MAX98357A_Breakout_Socket", "Freddy:MAX98357A_Breakout",
     [(str(i + 1), n, amp_nets[n]) for i, n in enumerate(AMP_PINS)], 76.0, 31.0, 0,
     mpn="Adafruit-style MAX98357A (in hand)", socketed=True)
part("U2", "MP1584 5V", "Freddy:MP1584_Module", "Freddy:MP1584_Module",
     [("1", "IN+", "VMOT"), ("2", "IN-", "GND"), ("3", "OUT+", "+5V_AUDIO"), ("4", "OUT-", "GND")],
     74.0, 44.6, 0, mpn="MP1584EN fixed 5V module (in hand)", socketed=True)
part("JP1", "GAIN", "Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical", "Freddy:Jumper3",
     [("1", "6dB", "+5V_AUDIO"), ("2", "GAIN", "AMP_GAIN"), ("3", "12dB", "GND")], 89.5, 16.0, 90,
     mpn="1x3 pin header + jumper")
C("C8", "100nF", "+5V_AUDIO", "GND", 86.6, 27.4, 90)

# --- logic power
part("U1", "Pololu S7V7F5", "Freddy:Pololu_S7V7F5_Upright", "Freddy:Pololu_S7V7F5",
     [("1", "VIN", "VMOT"), ("2", "GND", "GND"), ("3", "VOUT", "+5V_REG")], 27.0, 61.5, 0,
     mpn="Pololu #2119 (in hand)", socketed=True)
part("D2", "SS34", "Diode_SMD:D_SMA", "Device:D_Schottky", two("+5V_LOGIC", "+5V_REG"), 34.0, 66.6, 90,
     mpn="SS34")

# --- power entry
part("J1", "Barrel 5.5x2.1", "Connector_BarrelJack:BarrelJack_Horizontal", "Freddy:BarrelJack",
     [("1", "+", "VIN_RAW"), ("2", "-", "GND"), ("3", "SW", "GND")], 12.4, 86.5, 90,
     mpn="5.5x2.1mm PCB barrel jack, 5A+, CUI PJ-002A-style pinout (check against the footprint)")
part("F1", "5A mini blade", "Fuse:Fuseholder_Blade_Mini_Keystone_3568", "Device:Fuse",
     two("VIN_RAW", "VIN_FUSED"), 24.9, 87.08, 270, mpn="Keystone 3568 + 5A mini blade fuse")
part("Q1", "AO4407A", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", "Freddy:PMOS_SSSGDDDD",
     [("1", "S", "VMOT"), ("2", "S", "VMOT"), ("3", "S", "VMOT"), ("4", "G", "Q1_G"),
      ("5", "D", "VIN_FUSED"), ("6", "D", "VIN_FUSED"), ("7", "D", "VIN_FUSED"), ("8", "D", "VIN_FUSED")],
     31.5, 95.0, 270, mpn="AO4407A")
part("D1", "SMBJ8.5A", "Diode_SMD:D_SMB", "Device:D_Zener", two("VMOT", "GND"), 31.2, 87.4, 90,
     mpn="SMBJ8.5A (unidirectional TVS)")
R("R26", "10k", "Q1_G", "GND", 34.7, 88.0, 90)
CP("C1", "1000µF 16V", "VMOT", "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm", 18.5, 78.4, 0, mpn="1000µF 16V low-ESR, 10mm")
CP("C3", "470µF 16V", "VMOT", "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm", 16.6, 68.5, 0, mpn="470µF 16V, 8mm")
CP("C2", "470µF 10V", "+5V_LOGIC", "Capacitor_THT:CP_Radial_D6.3mm_P2.50mm", 27.6, 68.2, 0, mpn="470µF 10V, 6.3mm")
CP("C4", "470µF 16V", "VMOT", "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm", 84.3, 49.6, 90, mpn="470µF 16V, 8mm")
CP("C5", "100µF 10V", "+5V_AUDIO", "Capacitor_THT:CP_Radial_D5.0mm_P2.00mm", 82.0, 35.4, 0, mpn="100µF 10V, 5mm")

# --- harness pad groups (JST SM pigtails soldered in)
PIG = "Freddy:Pigtail_1x{n}_P2.54mm_ZipTie"


def pig(ref, label, nets, x, y, rot, names):
    n = len(nets)
    part(ref, label, PIG.format(n=n), f"Freddy:Pigtail_{n}",
         [(str(i + 1), names[i], nets[i]) for i in range(n)], x, y, rot,
         mpn=f"JST SM {n}-pin pigtail (in hand)", pigtail=True)


# left arm (exit left), right arm (exit right, pin 1 at the bottom of each group)
pig("J6", "L SHOULDER", ["SH_L_OUT1", "SH_L_OUT2"], 8.0, 38.0, 0, ["OUT1", "OUT2"])
pig("J7", "L ELBOW", ["GND", "VMOT", "ELBOW_L_SIG"], 8.0, 49.5, 0, ["GND", "7V", "SIG"])
pig("J8", "L SWITCH", ["SW_L_PWR", "GND", "SW_SH_L_EXT", None], 8.0, 62.5, 0, ["3V3", "GND", "SW", "NC"])
pig("J3", "R SHOULDER", ["SH_R_OUT1", "SH_R_OUT2"], 92.0, 40.54, 180, ["OUT1", "OUT2"])
pig("J4", "R ELBOW", ["GND", "VMOT", "ELBOW_R_SIG"], 92.0, 54.58, 180, ["GND", "7V", "SIG"])
pig("J5", "R SWITCH", ["SW_R_PWR", "GND", "SW_SH_R_EXT", None], 92.0, 70.12, 180, ["3V3", "GND", "SW", "NC"])
# head (exit up, pin 1 at the right of each pair)
pig("J9", "EYE SW", ["SW_EYE_EXT", "GND"], 31.27, 9.0, 270, ["SW", "GND"])
pig("J10", "EYE LED", ["EYE_LED_A", "EYE_LED_B"], 42.27, 9.0, 270, ["+", "-"])
pig("J11", "EYE MTR", ["EYE_MOT_A", "EYE_MOT_B"], 53.27, 9.0, 270, ["A", "B"])
pig("J15", "SPEAKER", ["SPK+", "SPK-"], 76.27, 9.0, 270, ["+", "-"])
PARTS[-1]["mpn"] = "solder pads: speaker wires straight in"
# PIR (exit down)
pig("J13", "PIR", ["VMOT", "PIR_EXT", "GND"], 79.0, 92.0, 90, ["7V", "SIG", "GND"])

# --- other connectors
part("J12", "TRIGGER", "Connector_Audio:Jack_3.5mm_KoreanHropartsElec_PJ-320D-4A_Horizontal", "Freddy:AudioJack",
     [("T", "T", "TRIG_EXT"), ("R1", "R1", None), ("R2", "R2", None), ("S", "S", "GND")], 69.6, 91.6, 90,
     mpn="PJ-320D 3.5mm jack")
part("J14", "Qwiic", "Connector_JST:JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal", "Freddy:Qwiic",
     [("1", "GND", "GND"), ("2", "3V3", "+3V3"), ("3", "SDA", "I2C_SDA"), ("4", "SCL", "I2C_SCL"), ("MP", "MP", None)],
     87.6, 9.6, 180, mpn="JST SM04B-SRSS-TB")
part("J16", "MIC", "Connector_PinSocket_2.54mm:PinSocket_1x06_P2.54mm_Vertical", "Freddy:Mic6",
     [("1", "VDD", "+3V3"), ("2", "GND", "GND"), ("3", "L/R", "GND"), ("4", "WS", "I2S_WS"),
      ("5", "SCK", "I2S_BCLK"), ("6", "SD", "I2S_DIN")], 67.6, 81.6, 90, mpn="1x6 female header (INMP441, later)")

# --- inputs and protection (most sit under the ESP32, between its header rows)
ex, ey = esp_pin_xy("IO5")
R("R8", "1k", "SW_SH_R_EXT", "SW_SH_R", 42.0, ey, 0)
R("R13", "100k", "SW_SH_R", "GND", 46.5, ey, 0)
ex, ey = esp_pin_xy("IO6")
R("R9", "1k", "SW_SH_L_EXT", "SW_SH_L", 42.0, ey, 0)
R("R14", "100k", "SW_SH_L", "GND", 46.5, ey, 0)
ex, ey = esp_pin_xy("IO7")
R("R10", "1k", "SW_EYE_EXT", "SW_EYE", 42.0, ey, 0)
R("R15", "10k", "SW_EYE", "+3V3", 46.5, ey, 0)
ex, ey = esp_pin_xy("IO41")
R("R17", "4.7k", "I2C_SDA", "+3V3", 58.0, ey, 0)
ex, ey = esp_pin_xy("IO42")
R("R18", "4.7k", "I2C_SCL", "+3V3", 58.0, ey, 0)
ex, ey = esp_pin_xy("IO17")
R("R6", "1k", "ELBOW_R_TX", "ELBOW_R_SIG", 42.0, ey, 0)
ex, ey = esp_pin_xy("IO18")
R("R7", "1k", "ELBOW_L_TX", "ELBOW_L_SIG", 42.0, ey, 0)
ex, ey = esp_pin_xy("IO8")
R("R11", "1k", "TRIG_EXT", "TRIG", 42.0, ey, 0)
R("R16", "10k", "TRIG", "+3V3", 46.5, ey, 0)
ex, ey = esp_pin_xy("IO9")
R("R12", "1k", "PIR_EXT", "PIR", 42.0, ey, 0)
R("R21", "100k", "PIR", "GND", 46.5, ey, 0)
part("D4", "BAT54S", "Package_TO_SOT_SMD:SOT-23", "Diode:BAT54S",
     [("1", "A", "GND"), ("2", "K", "+3V3"), ("3", "COM", "PIR")], 51.5, ey, 0, mpn="BAT54S")
ex, ey = esp_pin_xy("IO1")
R("R1", "47k", "VMOT", "VMON", 58.0, ey, 0)
R("R2", "22k", "VMON", "GND", 53.5, ey, 0)
C("C14", "100nF", "VMON", "GND", 53.5, ey + 2.6, 0)
ex, ey = esp_pin_xy("IO2")
R("R4", "1k", "CS_R_RAW", "CS_R", 58.0, ey, 0)
C("C12", "100nF", "CS_R", "GND", 53.5, ey + 2.6, 0)
ex, ey = esp_pin_xy("IO4")
R("R5", "1k", "CS_L_RAW", "CS_L", 42.0, ey, 0)
C("C13", "100nF", "CS_L", "GND", 46.5, ey, 0)
ex, ey = esp_pin_xy("5V")
C("C7", "100nF", "+5V_LOGIC", "GND", 42.0, ey, 0)
ex, ey = esp_pin_xy("3V3")
C("C11", "100nF", "+3V3", "GND", 42.0, ey + 1.3, 0)
part("D3", "ESD5Z3V3", "Diode_SMD:D_SOD-523", "Device:D_Zener", two("TRIG_EXT", "GND"), 77.0, 85.0, 0,
     mpn="ESD5Z3V3")

# --- under the shoulder drivers
R("R19", "10k", "FAULT_R", "+3V3", 75.0, 57.5, 0)
R("R25", "DNP", "VREF_R", "GND", 75.0, 60.5, 0, dnp=True)
C("C10", "100nF", "VMOT", "GND", 75.0, 70.0, 0)
R("R20", "10k", "FAULT_L", "+3V3", 25.0, 54.0, 0)
R("R24", "DNP", "VREF_L", "GND", 25.0, 51.0, 0, dnp=True)
C("C9", "100nF", "VMOT", "GND", 25.0, 42.0, 0)
# switch supplies (3V3 through 220R, near each arm)
R("R22", "220", "+3V3", "SW_L_PWR", 12.6, 75.6, 90)
R("R23", "220", "+3V3", "SW_R_PWR", 87.6, 76.0, 90)

# --- under the TB6612
part("R3", "220", "Resistor_SMD:R_1206_3216Metric", "Device:R", two("EYE_LED_RAW", "EYE_LED_A"), 22.0, 27.5, 0)
C("C6", "100nF", "VMOT", "GND", 22.0, 20.0, 0)

# --- test points (top band, above the antenna keep-out)
TP = "TestPoint:TestPoint_Pad_D1.5mm"
for i, (ref, net) in enumerate([("TP1", "VMOT"), ("TP2", "+5V_LOGIC"), ("TP3", "+3V3"), ("TP4", "GND")]):
    part(ref, net, TP, "Freddy:TestPoint", [("1", "TP", net)], 34.8, 25.5 + i * 3.4, 0)

# --- mounting holes
for i, (x, y) in enumerate([(4, 4), (96, 4), (4, 96), (96, 96)]):
    part(f"H{i + 1}", "M3", "MountingHole:MountingHole_3.2mm_M3", "Freddy:MountingHole", [], x, y, 0)

# ---------------------------------------------------------------- silkscreen + keep-outs
KEEPOUT_ANTENNA = (38.0, 24.0, 62.0, 38.0)   # x0, y0, x1, y1
MOUNT_HOLES = [(4, 4), (96, 4), (4, 96), (96, 96)]

# local power pours on the top layer (net, polygon), filled above the GND pour
POWER_ZONES = [
    ("VIN_RAW", [(10.0, 84.2), (26.6, 84.2), (26.6, 89.0), (10.0, 89.0)]),
    ("VIN_FUSED", [(19.6, 95.4), (35.4, 95.4), (35.4, 99.4), (19.6, 99.4)]),
    ("VMOT", [(27.6, 83.6), (35.4, 83.6), (35.4, 93.6), (27.6, 93.6)]),
]

# net classes: (name, track mm, clearance mm, via dia, via drill, nets)
NETCLASSES = [
    # routed at 1.0mm, then widened to WIDEN_TO wherever clearance allows (route.py)
    ("Motor", 1.0, 0.3, 1.2, 0.6, ["VMOT", "VIN_RAW", "VIN_FUSED", "SH_R_OUT1", "SH_R_OUT2", "SH_L_OUT1", "SH_L_OUT2"]),
    ("Power", 0.5, 0.25, 0.9, 0.45, ["GND", "+5V_LOGIC", "+5V_REG", "+5V_AUDIO", "+3V3", "SPK+", "SPK-"]),
]
WIDEN_TO = {"Motor": 2.0, "Power": 0.8}


def nets():
    out = set()
    for p in PARTS:
        for _, _, n in p["pins"]:
            if n:
                out.add(n)
    return sorted(out)


if __name__ == "__main__":
    from collections import Counter
    c = Counter()
    for p in PARTS:
        for _, _, n in p["pins"]:
            if n:
                c[n] += 1
    for n, k in sorted(c.items()):
        flag = "  <-- single" if k < 2 else ""
        print(f"{n:14s} {k}{flag}")
    print(len(PARTS), "parts,", len(c), "nets")
