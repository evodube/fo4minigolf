#!/usr/bin/env python3
"""Lays the 18 holes out like a real mini golf park: each hole at its own angle, its tee a short walk from the last
hole's cup, nothing overlapping, the whole thing as compact as possible. Plain Python + shapely (no Blender):

    python tools/course/layout.py <holes_local.json> <holes_tris.json> [seed] [tries] [out.png]

The two JSON files come from Blender (build_course.build with no placements gives the first; the second is every
hole's triangles, so props count too). Prints PLACEMENTS for build_course.py and draws the layout.

A random search with backing up: the holes are placed in order, the next tee somewhere near the last cup at some
angle, keeping the course compact; the best of many tries wins.
"""

import json
import math
import random
import sys

from shapely import affinity
from shapely.geometry import LineString, Polygon
from shapely.ops import unary_union

HALF = 580.0   # the course fits in +-HALF (about 16.5 m square)
GAP = 22.0     # between two holes: room for a path


def load(local_path, tris_path):
    local = json.load(open(local_path))["holes"]
    tris = json.load(open(tris_path))
    holes = {}
    for k, v in local.items():
        n = int(k)
        shape = unary_union([Polygon(t) for t in tris[k] if Polygon(t).area > 1e-3]).buffer(0.5).simplify(1.0)
        holes[n] = {"tee": tuple(v["tee"]), "cup": tuple(v["cup"]), "shape": shape}
    return holes


def place(h, pl):
    """the hole's shape moved so its tee is at (tx, ty), turned by deg"""
    tx, ty, deg = pl
    g = affinity.rotate(h["shape"], deg, origin=h["tee"])
    return affinity.translate(g, tx - h["tee"][0], ty - h["tee"][1])


def point(h, pl, p):
    tx, ty, deg = pl
    a = math.radians(deg)
    dx, dy = p[0] - h["tee"][0], p[1] - h["tee"][1]
    return (tx + dx * math.cos(a) - dy * math.sin(a), ty + dx * math.sin(a) + dy * math.cos(a))


def try_layout(holes, rng, half=HALF):
    bounds = Polygon([(-half, -half), (half, -half), (half, half), (-half, half)])
    placed = {}
    shapes = {}
    corner = rng.choice([(-1, -1), (1, -1), (-1, 1), (1, 1)])
    for _ in range(200):
        deg = rng.choice(range(0, 360, 15))
        pl = (corner[0] * (half - rng.uniform(40, 160)), corner[1] * (half - rng.uniform(40, 160)), deg)
        s = place(holes[1], pl)
        if bounds.contains(s.buffer(GAP / 2)):
            placed[1], shapes[1] = pl, s
            break
    if 1 not in placed:
        return None
    n = 2
    tried = {}
    steps = 0
    while n <= 18:
        steps += 1
        if steps > 700:
            return None
        prev = n - 1
        cup = point(holes[prev], placed[prev], holes[prev]["cup"])
        union = unary_union([shapes[m] for m in placed])
        grown = union.buffer(GAP - 0.01)
        others = unary_union([shapes[m] for m in placed if m != prev]) if len(placed) > 1 else None
        cands = []
        for _ in range(220):
            deg = rng.choice(range(0, 360, 15))
            b = rng.uniform(0, math.tau)
            d = rng.uniform(40, 140)
            t = (cup[0] + d * math.cos(b), cup[1] + d * math.sin(b))
            pl = (t[0], t[1], deg)
            s = place(holes[n], pl)
            if not bounds.contains(s.buffer(GAP / 2)):
                continue
            if s.intersects(grown):
                continue
            if others is not None and LineString([cup, t]).intersects(others):
                continue
            hull = unary_union([union, s]).convex_hull.area
            score = hull / 1000.0 + d * 0.4 + rng.random() * 30
            cands.append((score, pl, s))
        if not cands or tried.get(n, 0) > 3:
            tried[n] = 0
            for _ in range(2):  # back up two holes
                if n - 1 > 1:
                    n -= 1
                    del placed[n], shapes[n]
            continue
        tried[n] = tried.get(n, 0) + 1
        cands.sort(key=lambda c: c[0])
        _, pl, s = rng.choice(cands[:3])
        placed[n], shapes[n] = pl, s
        n += 1
    return placed, shapes


def score(holes, placed, shapes):
    union = unary_union(list(shapes.values()))
    walk = sum(math.dist(point(holes[n], placed[n], holes[n]["cup"]), (placed[n + 1][0], placed[n + 1][1]))
               for n in range(1, 18))
    angles = len({round(pl[2]) % 180 for pl in placed.values()})
    return union.convex_hull.area / 1000.0 + walk * 0.5 - angles * 10


def draw(holes, placed, shapes, path, half=HALF):
    from PIL import Image, ImageDraw
    S = 900
    k = S / (2 * half + 80)

    def P(p):
        return ((p[0] + half + 40) * k, S - (p[1] + half + 40) * k)

    img = Image.new("RGB", (S, S), (150, 140, 100))
    d = ImageDraw.Draw(img)
    d.rectangle([P((-half, half)), P((half, -half))], outline=(80, 70, 50), width=2)
    for n in range(1, 18):
        d.line([P(point(holes[n], placed[n], holes[n]["cup"])), P(placed[n + 1][:2])], fill=(210, 200, 180), width=7)
    for n, s in shapes.items():
        for g in getattr(s, "geoms", [s]):
            d.polygon([P(c) for c in g.exterior.coords], fill=(70, 130, 60), outline=(240, 240, 240))
        t = P(placed[n][:2])
        d.ellipse([t[0] - 4, t[1] - 4, t[0] + 4, t[1] + 4], fill=(220, 40, 40))
        c = P(point(holes[n], placed[n], holes[n]["cup"]))
        d.ellipse([c[0] - 4, c[1] - 4, c[0] + 4, c[1] + 4], fill=(255, 255, 255))
        d.text(P(s.centroid.coords[0]), str(n), fill=(255, 255, 0))
    img.save(path)


def main():
    holes = load(sys.argv[1], sys.argv[2])
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    tries = int(sys.argv[4]) if len(sys.argv) > 4 else 50
    out = sys.argv[5] if len(sys.argv) > 5 else "layout.png"
    rng = random.Random(seed)
    best = None
    ok = 0
    for _ in range(tries):
        r = try_layout(holes, rng)
        if r is None:
            continue
        ok += 1
        s = score(holes, *r)
        if best is None or s < best[0]:
            best = (s, r)
    print(f"# {ok}/{tries} tries worked")
    if best is None:
        return 1
    s, (placed, shapes) = best
    print(f"# score {s:.1f}")
    print("PLACEMENTS = {")
    for n in range(1, 19):
        x, y, deg = placed[n]
        print(f"    {n}: ({x:.1f}, {y:.1f}, {deg}),")
    print("}")
    draw(holes, placed, shapes, out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
