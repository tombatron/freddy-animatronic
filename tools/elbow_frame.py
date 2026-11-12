#!/usr/bin/env python3
"""Build, decode and verify elbow servo frames.

Frame (9 bytes): AB 01 09 41 [pos lo] [pos hi] [time lo] [time hi] [checksum]
  pos  = signed 16-bit target position (-700 = home)
  time = move time in ms
  checksum = (0xAF + 9*b0 + 8*b1 + 7*b2 + 6*b3 + 5*b4 + 4*b5 + 3*b6 + 2*b7) & 0xFF

Usage:
  python3 tools/elbow_frame.py build -400 1500        # -> AB 01 09 41 70 FE DC 05 45
  python3 tools/elbow_frame.py decode AB 01 09 41 06 FF DC 05 37
  python3 tools/elbow_frame.py test                   # check every captured frame
"""
import sys

WEIGHTS = [9, 8, 7, 6, 5, 4, 3, 2]


def checksum(first8):
    return (0xAF + sum(w * b for w, b in zip(WEIGHTS, first8))) & 0xFF


def build(pos, time_ms, device_id=0x01):
    p, t = pos & 0xFFFF, time_ms & 0xFFFF
    body = [0xAB, device_id, 0x09, 0x41, p & 0xFF, p >> 8, t & 0xFF, t >> 8]
    return body + [checksum(body)]


def decode(frame):
    if len(frame) != 9 or frame[0] != 0xAB:
        raise ValueError("not a 9-byte AB frame")
    pos = frame[4] | frame[5] << 8
    pos = pos - 0x10000 if pos & 0x8000 else pos
    return {"id": frame[1], "cmd": frame[3], "pos": pos,
            "time_ms": frame[6] | frame[7] << 8, "checksum_ok": checksum(frame[:8]) == frame[8]}


# Every frame captured from the stock board or found by the checksum hunt
KNOWN = [
    (200, 1000, 0x25), (200, 1500, 0x05), (-700, 1500, 0x65), (-250, 1500, 0x37),
    (-249, 1500, 0x3C), (-252, 1500, 0x2D), (-254, 1500, 0x23), (-242, 1500, 0x5F),
    (-234, 1500, 0x87), (-218, 1500, 0xD7), (-186, 1500, 0x77), (-122, 1500, 0xB7),
    (-250, 1501, 0x3A), (-250, 1502, 0x3D), (-250, 1496, 0x2B), (-250, 1492, 0x1F),
    (-250, 1484, 0x07), (-250, 1532, 0x97), (-250, 1372, 0xB7),
]


def hexs(frame):
    return " ".join(f"{b:02X}" for b in frame)


def main(argv):
    if len(argv) >= 3 and argv[0] == "build":
        print(hexs(build(int(argv[1]), int(argv[2]))))
    elif len(argv) == 10 and argv[0] == "decode":
        print(decode([int(x, 16) for x in argv[1:]]))
    elif argv[:1] == ["test"]:
        bad = [(p, t, c) for p, t, c in KNOWN if build(p, t)[8] != c]
        print(f"{len(KNOWN) - len(bad)}/{len(KNOWN)} known frames match")
        for p, t, c in bad:
            print("MISMATCH", p, t, f"{c:02X}", hexs(build(p, t)))
        return 1 if bad else 0
    else:
        print(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
