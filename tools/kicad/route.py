"""Autoroute the board with Freerouting, then fill the zones.

Usage: python3 route.py /path/to/freerouting.jar [passes]
"""
import os
import subprocess
import sys
import pcbnew
from gen_schematic import KDIR, PROJECT

PCB = os.path.join(KDIR, PROJECT + ".kicad_pcb")


def _seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0 if L == 0 else max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / L))
    cx, cy = ax + t * dx, ay + t * dy
    return ((px - cx) ** 2 + (py - cy) ** 2) ** 0.5


def stitch(board, pitch=6.0, via=0.7, drill=0.35, clear=0.35):
    """GND stitching vias on a grid wherever both layers are free."""
    import design
    MM, T = pcbnew.FromMM, pcbnew.ToMM
    ox, oy = design.OFFSET
    gnd = board.FindNet("GND")
    segs = []
    for t in board.GetTracks():
        if t.GetClass() == "PCB_VIA":
            w = T(t.GetWidth(pcbnew.F_Cu)) / 2
            p = t.GetPosition(); segs.append((T(p.x), T(p.y), T(p.x), T(p.y), w))
        else:
            w = T(t.GetWidth()) / 2
            a, b = t.GetStart(), t.GetEnd(); segs.append((T(a.x), T(a.y), T(b.x), T(b.y), w))
    boxes = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            bb = pad.GetBoundingBox()
            boxes.append((T(bb.GetLeft()), T(bb.GetTop()), T(bb.GetRight()), T(bb.GetBottom())))
    ko = []
    for z in board.Zones():
        if z.GetIsRuleArea():
            bb = z.GetBoundingBox()
            ko.append((T(bb.GetLeft()), T(bb.GetTop()), T(bb.GetRight()), T(bb.GetBottom())))
    for x, y in design.MOUNT_HOLES:
        ko.append((ox + x - 4, oy + y - 4, ox + x + 4, oy + y + 4))
    r = via / 2 + clear
    n = 0
    y = 3.0
    while y <= design.BOARD_H - 3.0:
        x = 3.0
        while x <= design.BOARD_W - 3.0:
            px, py = ox + x, oy + y
            ok = all(not (a - r < px < c + r and b - r < py < d + r) for a, b, c, d in boxes + ko)
            ok = ok and all(_seg_dist(px, py, a, b, c, d) > w + r for a, b, c, d, w in segs)
            if ok:
                v = pcbnew.PCB_VIA(board)
                v.SetPosition(pcbnew.VECTOR2I(MM(px), MM(py)))
                v.SetWidth(MM(via)); v.SetDrill(MM(drill)); v.SetNet(gnd)
                board.Add(v); n += 1
                segs.append((px, py, px, py, via / 2))
            x += pitch
        y += pitch
    print("stitching vias:", n)


def fix_endpoints(board):
    """Freerouting sometimes ends a wide track on a pad's edge instead of its centre, which KiCad
    doesn't count as connected. Join such ends to the pad centre with a short segment."""
    T, MM = pcbnew.ToMM, pcbnew.FromMM
    tracks = [t for t in board.GetTracks() if t.GetClass() == "PCB_TRACK"]
    vias = [t for t in board.GetTracks() if t.GetClass() == "PCB_VIA"]
    pads = [p for fp in board.GetFootprints() for p in fp.Pads()]
    added = 0
    for t in tracks:
        for e in (t.GetStart(), t.GetEnd()):
            net = t.GetNetCode()
            joined = False
            for o in tracks:
                if o is t or o.GetNetCode() != net or o.GetLayer() != t.GetLayer():
                    continue
                if (o.GetStart() - e).EuclideanNorm() < 20 or (o.GetEnd() - e).EuclideanNorm() < 20:
                    joined = True; break
            if not joined:
                joined = any(v.GetNetCode() == net and (v.GetPosition() - e).EuclideanNorm() < 20 for v in vias)
            if not joined:
                joined = any(p.GetNetCode() == net and p.IsOnLayer(t.GetLayer()) and p.HitTest(e) for p in pads)
            if joined:
                continue
            best = None
            for p in pads:
                if p.GetNetCode() != net or not p.IsOnLayer(t.GetLayer()):
                    continue
                d = (p.GetPosition() - e).EuclideanNorm()
                if d < MM(3.0) and (best is None or d < best[0]):
                    best = (d, p)
            if best:
                p = best[1]
                s = pcbnew.PCB_TRACK(board)
                s.SetStart(e); s.SetEnd(p.GetPosition()); s.SetLayer(t.GetLayer()); s.SetNet(t.GetNet())
                sz = p.GetSize(t.GetLayer()) if hasattr(p, "GetSize") else p.GetSize()
                s.SetWidth(min(t.GetWidth(), min(sz.x, sz.y)))
                board.Add(s); added += 1
    print("joined track ends to pads:", added)


def _segseg(a, b, c, d):
    """Minimum distance between segments ab and cd (tuples)."""
    def cross(o, p, q):
        return (p[0] - o[0]) * (q[1] - o[1]) - (p[1] - o[1]) * (q[0] - o[0])
    d1, d2, d3, d4 = cross(c, d, a), cross(c, d, b), cross(a, b, c), cross(a, b, d)
    if ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0)):
        return 0.0
    return min(_seg_dist(a[0], a[1], c[0], c[1], d[0], d[1]), _seg_dist(b[0], b[1], c[0], c[1], d[0], d[1]),
               _seg_dist(c[0], c[1], a[0], a[1], b[0], b[1]), _seg_dist(d[0], d[1], a[0], a[1], b[0], b[1]))


def _segbox(a, b, box):
    x0, y0, x1, y1 = box
    if x0 <= a[0] <= x1 and y0 <= a[1] <= y1:
        return 0.0
    edges = [((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))]
    return min(_segseg(a, b, e0, e1) for e0, e1 in edges)


def widen(board):
    """Widen power tracks where there is room (Freerouting routes them at the narrower class width)."""
    import design
    T, MM = pcbnew.ToMM, pcbnew.FromMM
    cls_of = {}
    for name, tw, cl, vd, vdr, nets in design.NETCLASSES:
        for n in nets:
            cls_of[n] = (name, cl)
    tracks = [t for t in board.GetTracks() if t.GetClass() == "PCB_TRACK"]
    vias = [t for t in board.GetTracks() if t.GetClass() == "PCB_VIA"]
    pads = []
    for fp in board.GetFootprints():
        for p in fp.Pads():
            bb = p.GetBoundingBox()
            pads.append((p.GetNetCode(), p.GetLayerSet(), cls_of.get(p.GetNetname(), ("", 0.2))[1], (T(bb.GetLeft()), T(bb.GetTop()), T(bb.GetRight()), T(bb.GetBottom()))))
    ko = []
    for z in board.Zones():
        if z.GetIsRuleArea():
            bb = z.GetBoundingBox()
            ko.append((T(bb.GetLeft()), T(bb.GetTop()), T(bb.GetRight()), T(bb.GetBottom())))
    ox, oy = design.OFFSET
    edge = (ox + 0.3, oy + 0.3, ox + design.BOARD_W - 0.3, oy + design.BOARD_H - 0.3)
    geo = lambda t: ((T(t.GetStart().x), T(t.GetStart().y)), (T(t.GetEnd().x), T(t.GetEnd().y)))
    changed = 0
    for t in tracks:
        n = t.GetNetname()
        if n not in cls_of:
            continue
        cname, cl = cls_of[n]
        cur = T(t.GetWidth())
        a, b = geo(t)
        lay = t.GetLayer()
        for w in (design.WIDEN_TO[cname], (design.WIDEN_TO[cname] + cur) / 2):
            if w <= cur + 0.01:
                break
            h = w / 2
            if not all(edge[0] + h <= p[0] <= edge[2] - h and edge[1] + h <= p[1] <= edge[3] - h for p in (a, b)):
                continue
            ok = True
            for o in tracks:
                if o is t or o.GetNetCode() == t.GetNetCode() or o.GetLayer() != lay:
                    continue
                c, d = geo(o)
                clr = max(cl, cls_of.get(o.GetNetname(), ("", 0.2))[1])
                if _segseg(a, b, c, d) < h + T(o.GetWidth()) / 2 + clr:
                    ok = False; break
            if ok:
                for v in vias:
                    if v.GetNetCode() == t.GetNetCode():
                        continue
                    p = v.GetPosition()
                    clr = max(cl, cls_of.get(v.GetNetname(), ("", 0.2))[1])
                    if _seg_dist(T(p.x), T(p.y), a[0], a[1], b[0], b[1]) < h + T(v.GetWidth(lay)) / 2 + clr:
                        ok = False; break
            if ok:
                for net, ls, pcl, box in pads:
                    if net == t.GetNetCode() and net != 0:
                        continue
                    if not ls.Contains(lay) and net != 0:
                        continue
                    if _segbox(a, b, box) < h + max(cl, pcl):
                        ok = False; break
            if ok:
                for box in ko:
                    if _segbox(a, b, box) < h:
                        ok = False; break
            if ok:
                t.SetWidth(MM(w)); changed += 1
                break
    print("widened tracks:", changed)


def remove_dangling(board):
    """Drop track stubs with a free end (Freerouting occasionally leaves one). One pass:
    the SWIG wrapper misbehaves if tracks are listed again after the connectivity query."""
    tracks = [t for t in board.GetTracks() if t.GetClass() == "PCB_TRACK"]
    board.BuildConnectivity()
    conn = board.GetConnectivity()
    dead = [t for t in tracks if conn.TestTrackEndpointDangling(t, False)]
    for t in dead:
        board.Remove(t)
    print("removed dangling stubs:", len(dead))


def main():
    jar = sys.argv[1]
    passes = sys.argv[2] if len(sys.argv) > 2 else "25"
    work = os.path.join(os.path.dirname(jar), "route")
    os.makedirs(work, exist_ok=True)
    dsn, ses = os.path.join(work, "board.dsn"), os.path.join(work, "board.ses")
    board = pcbnew.LoadBoard(PCB)
    for z in list(board.Zones()):
        if not z.GetIsRuleArea():
            board.Remove(z)
    # remove any previous routing (keep locked tracks)
    for t in list(board.GetTracks()):
        if not t.IsLocked():
            board.Remove(t)
    if not pcbnew.ExportSpecctraDSN(board, dsn):
        sys.exit("DSN export failed")
    # Freerouting is randomised: route from scratch up to 8 times, keep the first fully connected result
    for attempt in range(8):
        if os.path.exists(ses):
            os.remove(ses)
        cmd = ["java", "-jar", jar, "-de", dsn, "-do", ses, f"--router.max_passes={passes}", "--gui.enabled=false"]
        try:
            subprocess.run(cmd, check=False, timeout=300, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            print(f"attempt {attempt}: timed out")
            continue
        if not os.path.exists(ses):
            print(f"attempt {attempt}: no SES")
            continue
        trial = pcbnew.LoadBoard(PCB)
        for z in list(trial.Zones()):
            if not z.GetIsRuleArea():
                trial.Remove(z)
        for t in list(trial.GetTracks()):
            trial.Remove(t)
        pcbnew.ImportSpecctraSES(trial, ses)
        fix_endpoints(trial)
        trial.BuildConnectivity()
        left = trial.GetConnectivity().GetUnconnectedCount(False)
        print(f"attempt {attempt}: {left} unconnected")
        if left == 0:
            board = trial
            break
    else:
        sys.exit("could not get a fully connected route")
    remove_dangling(board)
    widen(board)
    import gen_pcb
    for z in list(board.Zones()):
        if not z.GetIsRuleArea():
            board.Remove(z)
    gen_pcb.add_zones(board)
    stitch(board)
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())
    # drop stitching vias that didn't land in filled GND on both layers, then refill
    gz = [z for z in board.Zones() if not z.GetIsRuleArea() and z.GetNetname() == "GND"]
    dropped = 0
    for t in list(board.GetTracks()):
        if t.GetClass() == "PCB_VIA" and t.GetNetname() == "GND":
            p = t.GetPosition()
            layers_hit = {z.GetLayer() for z in gz if z.HitTestFilledArea(z.GetLayer(), p)}
            if len(layers_hit) < 2:
                board.Remove(t); dropped += 1
    print("dropped vias:", dropped)
    filler.Fill(board.Zones())
    board.Save(PCB)
    print("routed:", len(list(board.GetTracks())), "track items")


if __name__ == "__main__":
    main()
