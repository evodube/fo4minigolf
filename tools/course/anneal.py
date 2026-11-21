#!/usr/bin/env python3
"""Finds the course layout by simulated annealing (see layout.py for the data it reads):

    python tools/course/anneal.py <holes_local.json> <holes_tris.json> [seed] [iterations] [out.png]

Each hole has a tee position and a heading. The cost: overlaps between holes (with room for a path between),
anything outside the course's square, long walks from a cup to the next tee, and walks that cross another hole.
Holes are nudged and turned one at a time, keeping changes that lower the cost (and, early on, some that don't).
"""

import math
import random
import sys

from shapely import affinity
from shapely.geometry import LineString, Polygon

import layout as L

HALF = L.HALF
GAP = L.GAP
WALK = 70.0   # a walk this long is free; longer costs


class State:
    def __init__(self, holes):
        self.holes = holes
        self.base = {n: h["shape"].buffer(GAP / 2).simplify(2.0) for n, h in holes.items()}
        self.raw = {n: h["shape"].simplify(2.0) for n, h in holes.items()}
        self.bounds = Polygon([(-HALF, -HALF), (HALF, -HALF), (HALF, HALF), (-HALF, HALF)])
        self.pl = {}
        self.geo = {}
        self.rawgeo = {}

    def set(self, n, pl):
        self.pl[n] = pl
        h = self.holes[n]
        tx, ty, deg = pl
        g = affinity.rotate(self.base[n], deg, origin=h["tee"])
        self.geo[n] = affinity.translate(g, tx - h["tee"][0], ty - h["tee"][1])
        g = affinity.rotate(self.raw[n], deg, origin=h["tee"])
        self.rawgeo[n] = affinity.translate(g, tx - h["tee"][0], ty - h["tee"][1])

    def cup(self, n):
        return L.point(self.holes[n], self.pl[n], self.holes[n]["cup"])

    def cost_of(self, n):
        """everything that involves hole n"""
        g = self.geo[n]
        c = (g.area - g.intersection(self.bounds).area) * 5.0
        for m, gm in self.geo.items():
            if m != n and g.intersects(gm):
                c += g.intersection(gm).area * 5.0
        for a in (n - 1, n):
            if 1 <= a <= 17 and a in self.pl and a + 1 in self.pl:
                c += self.walk_cost(a)
        return c

    def walk_cost(self, a):
        p, q = self.cup(a), self.pl[a + 1][:2]
        d = math.dist(p, q)
        c = max(0.0, d - WALK) ** 2 * 0.5
        line = LineString([p, q])
        for m, gm in self.rawgeo.items():
            if m in (a, a + 1):
                continue
            if line.intersects(gm):
                c += line.intersection(gm).length * 60.0
        return c

    def total(self):
        c = 0.0
        ns = sorted(self.pl)
        for n in ns:
            g = self.geo[n]
            c += (g.area - g.intersection(self.bounds).area) * 5.0
            for m in ns:
                if m > n and g.intersects(self.geo[m]):
                    c += g.intersection(self.geo[m]).area * 5.0
        for a in range(1, 18):
            c += self.walk_cost(a)
        return c


def initial(state, rng):
    """every hole somewhere on the square, at any angle: the annealing sorts them out"""
    for n in range(1, 19):
        state.set(n, (rng.uniform(-HALF + 150, HALF - 150), rng.uniform(-HALF + 150, HALF - 150),
                      rng.choice(range(0, 360, 15))))


def main():
    holes = L.load(sys.argv[1], sys.argv[2])
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    iters = int(sys.argv[4]) if len(sys.argv) > 4 else 6000
    out = sys.argv[5] if len(sys.argv) > 5 else "anneal.png"
    rng = random.Random(seed)
    st = State(holes)
    initial(st, rng)
    cost = st.total()
    t0 = 2000.0
    for i in range(iters):
        temp = t0 * (1.0 - i / iters) ** 3 + 1.0
        n = rng.randint(1, 18)
        old = st.pl[n]
        before = st.cost_of(n)
        step = 80.0 * (1.0 - i / iters) + 6.0
        r = rng.random()
        if r < 0.25:
            new = (old[0], old[1], (old[2] + rng.choice([-30, -15, 15, 30])) % 360)
        elif r < 0.35:
            # swing the hole about its tee so it points elsewhere, keeping its walk
            new = (old[0], old[1], (old[2] + rng.choice([-90, 90, 180])) % 360)
        elif r < 0.45 and n > 1:
            # move its tee next to the last hole's cup, at any angle
            cx, cy = st.cup(n - 1)
            b = rng.uniform(0, math.tau)
            new = (cx + 60 * math.cos(b), cy + 60 * math.sin(b), rng.choice(range(0, 360, 15)))
        else:
            new = (old[0] + rng.gauss(0, step), old[1] + rng.gauss(0, step), old[2])
        st.set(n, new)
        after = st.cost_of(n)
        delta = after - before
        if delta < 0 or rng.random() < math.exp(-delta / temp):
            cost += delta
        else:
            st.set(n, old)
        if i % 1000 == 0:
            print(i, round(cost), round(temp), flush=True)
    cost = st.total()
    print("# final cost", round(cost))
    print("PLACEMENTS = {")
    for n in range(1, 19):
        x, y, deg = st.pl[n]
        print(f"    {n}: ({x:.1f}, {y:.1f}, {deg}),")
    print("}")
    walks = [round(math.dist(st.cup(a), st.pl[a + 1][:2])) for a in range(1, 18)]
    print("# walks", walks)
    L.draw(holes, st.pl, st.rawgeo, out)


if __name__ == "__main__":
    main()
