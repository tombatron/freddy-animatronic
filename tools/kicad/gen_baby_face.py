"""Line-art portrait traced from Tom's GitHub avatar, for the enclosure lid.

Landmarks were measured on the 189px avatar (scaled to 756px). Strokes are buffered into
filled outlines so OpenSCAD can import the SVG and recess it into the lid.
Writes hardware/controller/enclosure/baby_face.svg (fill-only, for OpenSCAD and cutters)
and baby_face_preview.svg (same shapes, for viewing).
"""
import math
import os
from shapely.geometry import LineString, Point, Polygon
from shapely.ops import unary_union
from shapely import affinity

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "hardware", "controller", "enclosure"))
S = 1 / 7.56          # 756px reference -> 100 units
W = 2.4               # stroke width in units (about 1mm at the printed size)


def P(pts):
    return [(x * S, y * S) for x, y in pts]


def smooth(pts, closed=False, n=12):
    """Catmull-Rom through the points."""
    pts = list(pts)
    if closed:
        pts = [pts[-1]] + pts + pts[:2]
    else:
        pts = [pts[0]] + pts + [pts[-1]]
    out = []
    for i in range(1, len(pts) - 2):
        p0, p1, p2, p3 = pts[i - 1], pts[i], pts[i + 1], pts[i + 2]
        for k in range(n):
            t = k / n
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * (2 * p1[j] + (-p0[j] + p2[j]) * t + (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2
                                    + (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3) for j in range(2)))
    if not closed:
        out.append(pts[-2])
    return out


def stroke(pts, w=W, closed=False):
    c = smooth(P(pts), closed)
    if closed:
        c = c + [c[0]]
    return LineString(c).buffer(w / 2, cap_style=1, join_style=1)


def ellipse(cx, cy, rx, ry, rot=0):
    e = affinity.scale(Point(cx * S, cy * S).buffer(1, 64), rx * S, ry * S)
    return affinity.rotate(e, rot) if rot else e


def build():
    shapes = []
    # round baby head: wide, chubby cheeks low down, broad soft chin
    shapes.append(stroke([(370, 26), (500, 52), (590, 140), (632, 270), (642, 400), (636, 520), (606, 630),
                          (540, 712), (440, 758), (340, 760), (240, 728), (160, 660), (108, 560), (92, 430),
                          (100, 300), (140, 172), (220, 78), (290, 38)], closed=True))
    # right ear
    shapes.append(stroke([(640, 345), (668, 352), (676, 398), (660, 440), (637, 448)]))
    shapes.append(stroke([(652, 376), (658, 402), (648, 424)], w=W * 0.6))
    # a few short hair wisps on top
    for wisp in ([(330, 92), (346, 122), (346, 150)], [(388, 78), (398, 110), (392, 140)],
                 [(444, 96), (440, 124), (428, 148)]):
        shapes.append(stroke(wisp, w=W * 0.6))
    # raised, faint eyebrows (the surprised look)
    shapes.append(stroke([(170, 318), (218, 300), (270, 310)], w=W * 0.6))
    shapes.append(stroke([(410, 296), (468, 276), (528, 290)], w=W * 0.6))
    # big eyes: lid crease, outline, large dark iris with a highlight
    for cx, cy, rx, ry, crease in ((215, 425, 62, 34, [(155, 384), (215, 360), (280, 386)]),
                                    (465, 402, 66, 36, [(398, 360), (465, 334), (535, 360)])):
        shapes.append(stroke(crease, w=W * 0.55))
        outline = ellipse(cx, cy, rx, ry)
        shapes.append(outline.exterior.buffer(W * 0.45))
        iris = ellipse(cx + 4, cy + 2, 33, 32).intersection(outline)
        hl = Point((cx + 15) * S, (cy - 10) * S).buffer(1.4)
        shapes.append(iris.difference(hl))
    # small button nose
    shapes.append(stroke([(318, 520), (340, 540), (370, 545), (398, 538), (418, 518)], w=W * 0.7))
    shapes.append(ellipse(342, 528, 11, 7, -15))
    shapes.append(ellipse(396, 526, 11, 7, 15))
    # the "O": open mouth with a lip ring around it
    shapes.append(ellipse(370, 650, 52, 44))
    shapes.append(ellipse(370, 652, 70, 62).exterior.buffer(W * 0.4))
    return unary_union(shapes)


def to_svg(geom, fill="#000"):
    paths = []
    polys = geom.geoms if geom.geom_type == "MultiPolygon" else [geom]
    for pg in polys:
        rings = [pg.exterior] + list(pg.interiors)
        d = " ".join("M " + " L ".join(f"{x:.2f},{y:.2f}" for x, y in r.coords) + " Z" for r in rings)
        paths.append(f'<path d="{d}" fill="{fill}" fill-rule="evenodd"/>')
    minx, miny, maxx, maxy = geom.bounds
    pad = 2
    vb = f"{minx - pad:.2f} {miny - pad:.2f} {maxx - minx + 2 * pad:.2f} {maxy - miny + 2 * pad:.2f}"
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}" width="{(maxx - minx + 2 * pad) * 4:.0f}" '
            f'height="{(maxy - miny + 2 * pad) * 4:.0f}">\n' + "\n".join(paths) + "\n</svg>\n"), geom.bounds


if __name__ == "__main__":
    g = build()
    os.makedirs(OUT, exist_ok=True)
    svg, b = to_svg(g)
    open(os.path.join(OUT, "baby_face.svg"), "w").write(svg)
    print("bounds (units):", [round(v, 1) for v in b])
