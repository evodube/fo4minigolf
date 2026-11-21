"""Builds the mini golf course in Blender: 18 holes on a 16 x 16 m deck, all from code, in game units (1 Blender unit
= 1 game unit, about 70 per metre), every surface with the game's own textures.

Run inside Blender (Blender 5.2 + PyNifly), with this folder on sys.path:

    import build_course; build_course.build(root)          # makes the scene (wipes the current one)
    import export_course; export_course.export(root)       # writes the meshes and course.txt

root is the working folder: <root>/vanilla holds the game's textures pulled out of its archives by ba2.py (for the
viewport only), <root>/Data/Textures/fo4minigolf our own (signs and flags, tools/course/make_textures.py).

Every object carries custom properties that export_course.py reads:
    mg_hole      1..18 (0 = the deck, shared)
    mg_role      ground   the ball rolls on its upward faces          (mg_surface: green, water, fast, slow)
                 wall     its vertical faces are walls                (mg_rest: restitution)
                 owall    the walls of a moving obstacle at time 0    (mg_obstacle: the obstacle's name)
                 obstacle a moving obstacle's mesh: exported as its own nif, origin at its pivot; mg_motion and the
                          motion's numbers (see Course.h)
                 visual   just to look at
    mg_hidden    1 = physics only, not in any nif
Tee and cup are empties named HoleNN_Tee / HoleNN_Cup.
"""

import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from holes import HOLES  # noqa: E402

# ---------------------------------------------------------------- layout

COURSE = 1120.0          # the deck is COURSE x COURSE, centred on the origin
COLS, ROWS = 3, 6
CELL_X = COURSE / COLS   # 373: a hole runs along x inside its cell
CELL_Y = COURSE / ROWS   # 187
DECK = 10.0              # deck top
SKIRT = 60.0             # the deck's sides reach this far below 0, for uneven ground
TIERS = [0.0, 14.0, 28.0, 28.0, 14.0, 0.0]   # each row's platform height above the deck
GREEN = 4.0              # a green sits this high on its platform
RAIL_H = 8.0
RAIL_T = 6.0
CUP_R = 3.78             # 108 mm
GRID = 8.0               # the greens' triangle size

# ---------------------------------------------------------------- materials: the game's textures

# name: (texture path without _d/_n/_s, units per texture repeat, normal/specular override)
MATERIALS = {
    "Turf": ("interiors/Institute/InstituteTurf01", 96.0, None),
    "Lane": ("interiors/Building/BldWoodFloorBowling01", 128.0, None),
    "TeeMat": ("interiors/Building/BldCarpetDecoRed01", 48.0, "interiors/Building/BldCarpetDeco01"),
    "Deck": ("Architecture/Buildings/WoodFloor01", 192.0, None),
    "Concrete": ("Architecture/Buildings/Concrete02", 160.0, None),
    "Corrugated": ("Architecture/DiamondCity/CorrugatedMetal01", 96.0, None),
    "WhiteWood": ("SetDressing/Fences/PaintedWoodWhiteClean01", 96.0, None),
    "VaultPanel": ("interiors/Vault/VltHallResPaneled01", 96.0, None),
    "BlueMetal": ("Architecture/Warehouse/WrhsMetalCorgYellow01", 96.0, None),
    "Striped": ("interiors/Industrial/MetalSiloPlatesStriped01", 96.0, None),
    "GlowRock": ("Landscape/Rocks/GlowingSeaCliffs", 160.0, None),
    "Muck": ("Landscape/GSMuckPools/GlowingSeaMuckWater02", 128.0, None),
    "Lawn": ("Landscape/Ground/DriedGrass01", 256.0, None),
    "Paving": ("Landscape/Roads/Cobblestone01Landscape", 96.0, None),
    "Signs": ("fo4minigolf/Signs", 0.0, None),     # UVs set by hand
    "Flags": ("fo4minigolf/Flags", 0.0, None),
}

THEME = {  # rails, trim
    "junk": ("Corrugated", "Corrugated"),
    "americana": ("WhiteWood", "Striped"),
    "vault": ("VaultPanel", "Striped"),
    "military": ("BlueMetal", "Corrugated"),
    "glow": ("GlowRock", "GlowRock"),
}

_root = None


def texture_file(base, suffix):
    rel = f"textures/{base}_{suffix}.dds"
    if base.startswith("fo4minigolf/"):
        return os.path.join(_root, "Data", *rel.split("/"))
    return os.path.join(_root, "vanilla", *rel.split("/"))


def material(name):
    if name in bpy.data.materials:
        return bpy.data.materials[name]
    base, _scale, ns = MATERIALS[name]
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    img = nt.nodes.new("ShaderNodeTexImage")
    path = texture_file(base, "d")
    if os.path.exists(path):
        img.image = bpy.data.images.load(path, check_existing=True)
    else:
        img.image = bpy.data.images.new(os.path.basename(path), 4, 4)
        img.image.filepath = path
    nt.links.new(img.outputs["Color"], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.85
    other = ns or base
    mat["BSShaderTextureSet_Normal"] = f"textures\\{other}_n.dds".replace("/", "\\")
    mat["BSShaderTextureSet_Specular"] = f"textures\\{other}_s.dds".replace("/", "\\")
    return mat


# ---------------------------------------------------------------- mesh helpers


def collection(name, parent=None):
    if name in bpy.data.collections:
        return bpy.data.collections[name]
    c = bpy.data.collections.new(name)
    (parent or bpy.context.scene.collection).children.link(c)
    return c


def new_object(name, bm, coll, mat=None, **props):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    if mat:
        me.materials.append(material(mat))
    for k, v in props.items():
        ob[k] = v
    return ob


def box_uv(bm, scale, offset=(0.0, 0.0, 0.0)):
    """World-ish box projection: each face takes the plane its normal points most along."""
    if scale <= 0:
        return
    uv = bm.loops.layers.uv.verify()
    for f in bm.faces:
        n = f.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        for loop in f.loops:
            co = loop.vert.co + Vector(offset)
            if ax == 2:
                u, v = co.x, co.y
            elif ax == 0:
                u, v = co.y, co.z
            else:
                u, v = co.x, co.z
            loop[uv].uv = (u / scale, v / scale)


def finish(bm, mat):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.normal_update()
    box_uv(bm, MATERIALS[mat][1] if mat in MATERIALS else 96.0)


def poly_area(poly):
    return 0.5 * sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1] for i in range(len(poly)))


def ccw(poly):
    return poly if poly_area(poly) > 0 else list(reversed(poly))


def resample(poly, step, closed=True):
    out = []
    n = len(poly)
    for i in range(n if closed else n - 1):
        a, b = Vector(poly[i]), Vector(poly[(i + 1) % n])
        k = max(1, int(math.ceil((b - a).length / step)))
        for j in range(k):
            out.append(tuple(a.lerp(b, j / k)))
    if not closed:
        out.append(tuple(poly[-1]))
    return out


def ground(name, poly, hfunc, z0, coll, mat="Turf", surface="green", hole=0, holes=(), grid=GRID):
    """A surface over the polygon (local xy), height z0 + hfunc(x, y), cut into a grid so slopes are smooth. holes =
    polygons cut out of it."""
    poly = ccw(poly)
    bm = bmesh.new()
    verts = [bm.verts.new((x, y, 0.0)) for x, y in poly]
    bm.faces.new(verts)
    xs = [p[0] for p in poly]
    ys = [p[1] for p in poly]
    geom = bm.verts[:] + bm.edges[:] + bm.faces[:]
    cuts_x = {round(math.floor(min(xs) / grid) * grid + grid * (i + 1), 3) for i in range(int((max(xs) - min(xs)) / grid) + 1)}
    cuts_y = {round(math.floor(min(ys) / grid) * grid + grid * (i + 1), 3) for i in range(int((max(ys) - min(ys)) / grid) + 1)}
    for h in holes:  # cut along the cut-outs' straight sides too, so they come out exact
        if len(h) <= 6:
            cuts_x |= {round(p[0], 3) for p in h}
            cuts_y |= {round(p[1], 3) for p in h}
    for x in sorted(cuts_x):
        if min(xs) < x < max(xs):
            bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=(x, 0, 0), plane_no=(1, 0, 0))
    for y in sorted(cuts_y):
        if min(ys) < y < max(ys):
            bmesh.ops.bisect_plane(bm, geom=bm.verts[:] + bm.edges[:] + bm.faces[:], plane_co=(0, y, 0), plane_no=(0, 1, 0))
    for h in holes:
        # drop the faces whose centre is inside a hole polygon (water cut-outs are whole grid cells: fine, the water
        # surface below fills them)
        doomed = [f for f in bm.faces if inside(f.calc_center_median().xy, h)]
        bmesh.ops.delete(bm, geom=doomed, context="FACES")
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    for v in bm.verts:
        v.co.z = z0 + hfunc(v.co.x, v.co.y)
    bm.normal_update()
    for f in bm.faces:
        if f.normal.z < 0:
            f.normal_flip()
    box_uv(bm, MATERIALS[mat][1])
    return new_object(name, bm, coll, mat, mg_role="ground", mg_surface=surface, mg_hole=hole)


def grown(poly, by):
    """the polygon pushed out by about `by` (from its centre): water and patches are drawn a little bigger than
    the hole cut for them, so no gap is left where the grid's cut doesn't follow a curve"""
    cx = sum(p[0] for p in poly) / len(poly)
    cy = sum(p[1] for p in poly) / len(poly)
    out = []
    for x, y in poly:
        d = Vector((x - cx, y - cy))
        out.append((x + (d.normalized().x * by if d.length else 0), y + (d.normalized().y * by if d.length else 0)))
    return out


def inside(p, poly):
    x, y = p
    c = False
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]
        x2, y2 = poly[(i + 1) % n]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            c = not c
    return c


def rail(name, path, hfunc, z0, zbottom, coll, mat, hole, closed=True, height=RAIL_H, thick=RAIL_T, rest=0.6, outward=True):
    """A rail along a path (local xy): its inner face on the path, thick outwards (to the right of a ccw polygon's
    edges = outside), its top height above the ground's height there."""
    pts = resample(ccw(path) if closed else path, 12.0, closed)
    n = len(pts)
    normals = []
    for i in range(n):
        a = Vector(pts[i - 1] if (closed or i > 0) else pts[i])
        b = Vector(pts[i])
        c = Vector(pts[(i + 1) % n] if (closed or i < n - 1) else pts[i])
        d1 = (b - a).normalized() if (b - a).length > 1e-6 else (c - b).normalized()
        d2 = (c - b).normalized() if (c - b).length > 1e-6 else d1
        n1 = Vector((d1.y, -d1.x))
        n2 = Vector((d2.y, -d2.x))
        m = (n1 + n2)
        m = m.normalized() if m.length > 1e-6 else n1
        k = 1.0 / max(m.dot(n1), 0.3)
        normals.append(m * k * (1 if outward else -1))
    bm = bmesh.new()
    ring = []
    for i, p in enumerate(pts):
        p = Vector(p)
        q = p + normals[i] * thick
        top = z0 + hfunc(p.x, p.y) + height
        ring.append([bm.verts.new((p.x, p.y, zbottom)), bm.verts.new((p.x, p.y, top)),
                     bm.verts.new((q.x, q.y, top)), bm.verts.new((q.x, q.y, zbottom))])
    for i in range(n if closed else n - 1):
        a, b = ring[i], ring[(i + 1) % n]
        for k in range(4):
            k2 = (k + 1) % 4
            if k == 3:
                continue  # no bottom
            bm.faces.new((a[k], b[k], b[k2], a[k2]))
    if not closed:
        bm.faces.new(ring[0][::-1][:4])
        bm.faces.new(ring[-1][:4])
    finish(bm, mat)
    return new_object(name, bm, coll, mat, mg_role="wall", mg_rest=rest, mg_hole=hole)


def box(name, coll, center, size, mat, role="visual", hole=0, rot=0.0, **props):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=size, verts=bm.verts)
    bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=Matrix.Rotation(rot, 3, "Z"), verts=bm.verts)
    bmesh.ops.translate(bm, vec=center, verts=bm.verts)
    finish(bm, mat)
    return new_object(name, bm, coll, mat, mg_role=role, mg_hole=hole, **props)


def cylinder(name, coll, center, radius, height, mat, role="visual", hole=0, segments=16, cap=True, **props):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=cap, cap_tris=False, segments=segments, radius1=radius, radius2=radius, depth=height)
    bmesh.ops.translate(bm, vec=(center[0], center[1], center[2] + height / 2), verts=bm.verts)
    finish(bm, mat)
    return new_object(name, bm, coll, mat, mg_role=role, mg_hole=hole, **props)


def ring_walls(name, coll, center, radius, z0, height, hole, rest=0.6, segments=10, role="wall", **props):
    """physics only: a polygon of walls round a post"""
    pts = [(center[0] + radius * math.cos(i / segments * math.tau), center[1] + radius * math.sin(i / segments * math.tau))
           for i in range(segments)]
    return wall_path(name, coll, pts, z0, height, hole, rest, True, role, **props)


def wall_path(name, coll, pts, z0, height, hole, rest=0.6, closed=True, role="wall", **props):
    """physics only: thin vertical walls along a path"""
    bm = bmesh.new()
    vs = [(bm.verts.new((x, y, z0)), bm.verts.new((x, y, z0 + height))) for x, y in pts]
    n = len(vs)
    for i in range(n if closed else n - 1):
        a, b = vs[i], vs[(i + 1) % n]
        bm.faces.new((a[0], b[0], b[1], a[1]))
    ob = new_object(name, bm, coll, None, mg_role=role, mg_rest=rest, mg_hole=hole, mg_hidden=1, **props)
    ob.display_type = "WIRE"
    ob.hide_render = True
    return ob


def polyline_outline(points, width):
    """the outline (ccw) of a path of the given width, mitred at the bends"""
    left, right = [], []
    n = len(points)
    for i in range(n):
        p = Vector(points[i])
        d_in = (p - Vector(points[i - 1])).normalized() if i > 0 else None
        d_out = (Vector(points[i + 1]) - p).normalized() if i < n - 1 else None
        if d_in is None:
            d_in = d_out
        if d_out is None:
            d_out = d_in
        n1 = Vector((-d_in.y, d_in.x))
        n2 = Vector((-d_out.y, d_out.x))
        m = (n1 + n2).normalized()
        k = (width / 2) / max(m.dot(n1), 0.3)
        left.append(tuple(p + m * k))
        right.append(tuple(p - m * k))
    return right + list(reversed(left))


def empty(name, coll, loc, hole, **props):
    e = bpy.data.objects.new(name, None)
    e.location = loc
    e.empty_display_size = 6
    coll.objects.link(e)
    e["mg_hole"] = hole
    for k, v in props.items():
        e[k] = v
    return e


def cup(hole, coll, x, y, z, rails_mat):
    """the cup: a liner down into the green (the green's cut-out is made by export: see cut_cup), and a flag on a
    short pole just behind it"""
    cylinder(f"Hole{hole:02d}_CupLiner", coll, (x, y, z - 8.0), CUP_R, 8.0, "WhiteWood", segments=24, cap=False)
    bm = bmesh.new()
    bmesh.ops.create_circle(bm, cap_ends=True, segments=24, radius=CUP_R)
    bmesh.ops.translate(bm, vec=(x, y, z - 7.9), verts=bm.verts)
    finish(bm, "WhiteWood")
    new_object(f"Hole{hole:02d}_CupBottom", bm, coll, "WhiteWood", mg_role="visual", mg_hole=hole)
    empty(f"Hole{hole:02d}_Cup", coll, (x, y, z), hole, mg_cup=1)


def flag(hole, coll, x, y, z):
    cylinder(f"Hole{hole:02d}_FlagPole", coll, (x, y, z), 0.5, 46.0, "WhiteWood", segments=8)
    # the flag: a two-sided card, its number from the atlas (6 x 3 cells)
    i = hole - 1
    u0, v0 = (i % 6) / 6.0, 1.0 - (i // 6 + 1) / 3.0
    bm = bmesh.new()
    uv = bm.loops.layers.uv.verify()
    w, h = 16.0, 11.0
    corners = [(0, 0), (w, 0), (w, h), (0, h)]
    for side in (1, -1):
        vs = [bm.verts.new((x + 0.5 + cx, y + 0.05 * side, z + 46.0 - h + cy)) for cx, cy in corners]
        f = bm.faces.new(vs if side == 1 else list(reversed(vs)))
        for loop in f.loops:
            lx = (loop.vert.co.x - x - 0.5) / w
            ly = (loop.vert.co.z - (z + 46.0 - h)) / h
            if side == -1:
                lx = lx  # same number both sides (mirrored on the back, like a real flag)
            loop[uv].uv = (u0 + lx / 6.0, v0 + ly / 3.0)
    new_object(f"Hole{hole:02d}_Flag", bm, coll, "Flags", mg_role="visual", mg_hole=hole)


def sign(hole, coll, x, y, z, facing):
    """a signboard on two posts: the hole's number, name and par from the atlas"""
    cylinder(f"Hole{hole:02d}_SignPost1", coll, (x - 9 * math.sin(facing), y + 9 * math.cos(facing), z), 1.0, 34.0, "Deck", segments=6)
    cylinder(f"Hole{hole:02d}_SignPost2", coll, (x + 9 * math.sin(facing), y - 9 * math.cos(facing), z), 1.0, 34.0, "Deck", segments=6)
    i = hole - 1
    u0, v0 = (i % 6) / 6.0, 1.0 - (i // 6 + 1) / 3.0
    bm = bmesh.new()
    uv = bm.loops.layers.uv.verify()
    s = 22.0
    # facing: the direction the sign's face looks (radians, maths angle)
    fx, fy = math.cos(facing), math.sin(facing)
    rx, ry = -fy, fx  # to the sign's right as you look at it
    for side in (1, -1):
        off = 0.6 * side
        vs = []
        for cx, cz in ((-s / 2, 0), (s / 2, 0), (s / 2, s), (-s / 2, s)):
            vs.append(bm.verts.new((x + rx * cx * side + fx * off, y + ry * cx * side + fy * off, z + 14 + cz)))
        f = bm.faces.new(vs)
        for loop, (cu, cv) in zip(f.loops, ((0, 0), (1, 0), (1, 1), (0, 1))):
            loop[uv].uv = (u0 + cu / 6.0, v0 + cv / 3.0)
    new_object(f"Hole{hole:02d}_Sign", bm, coll, "Signs", mg_role="visual", mg_hole=hole)
    box(f"Hole{hole:02d}_SignBack", coll, (x, y, z + 14 + s / 2), (1.0, s + 2, s + 2), "Deck", rot=facing)


# ---------------------------------------------------------------- the holes
#
# Each hole is built in its own frame: the tee at -x, the cup at +x, inside a cell 373 x 187 (x within +-170,
# y within +-85). Its objects are then moved into place on the deck.


def flat(_x, _y):
    return 0.0


def smoothstep(e0, e1, x):
    t = min(1.0, max(0.0, (x - e0) / (e1 - e0)))
    return t * t * (3 - 2 * t)


# ---------------------------------------------------------------- shapes
#
# A hole's outline is the union of simple pieces: a thick centreline (its "path": straights and arcs, round at both
# ends) and round greens (circles). The union is found from its signed distance field with marching squares, so any
# mix of them comes out as one clean outline, the way a park's concrete holes look from above.


class Path:
    """a centreline from turtle steps: ("L", length) or ("A", radius, degrees; + turns left)"""

    def __init__(self, steps, start=(0.0, 0.0), heading=0.0, step=3.0):
        x, y = start
        a = math.radians(heading)
        pts = [(x, y)]
        heads = [a]
        for st in steps:
            if st[0] == "L":
                n = max(1, int(st[1] / step))
                for _ in range(n):
                    x += math.cos(a) * st[1] / n
                    y += math.sin(a) * st[1] / n
                    pts.append((x, y))
                    heads.append(a)
            else:
                r, deg = st[1], st[2]
                turn = math.radians(deg)
                n = max(2, int(abs(turn) * r / step))
                sgn = 1.0 if turn > 0 else -1.0
                cx, cy = x - math.sin(a) * r * sgn, y + math.cos(a) * r * sgn
                for k in range(1, n + 1):
                    b = a + turn * k / n
                    x = cx + math.sin(b) * r * sgn
                    y = cy - math.cos(b) * r * sgn
                    pts.append((x, y))
                    heads.append(b)
                a += turn
        self.pts = pts
        self.heads = heads
        self.s = [0.0]
        for p, q in zip(pts, pts[1:]):
            self.s.append(self.s[-1] + math.dist(p, q))
        self.length = self.s[-1]

    def at(self, s):
        """(x, y, heading) at distance s along it (past the ends: carried on straight)"""
        if s <= 0:
            a = self.heads[0]
            return (self.pts[0][0] + math.cos(a) * s, self.pts[0][1] + math.sin(a) * s, a)
        if s >= self.length:
            a = self.heads[-1]
            d = s - self.length
            return (self.pts[-1][0] + math.cos(a) * d, self.pts[-1][1] + math.sin(a) * d, a)
        lo, hi = 0, len(self.s) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if self.s[mid] <= s:
                lo = mid
            else:
                hi = mid
        t = (s - self.s[lo]) / max(self.s[hi] - self.s[lo], 1e-9)
        p, q = self.pts[lo], self.pts[hi]
        return (p[0] + (q[0] - p[0]) * t, p[1] + (q[1] - p[1]) * t, self.heads[lo] + (self.heads[hi] - self.heads[lo]) * t)

    def project(self, x, y):
        """(s, d): how far along, and how far to the left of it, the nearest point is"""
        best = (1e18, 0.0, 0.0)
        n = len(self.pts)
        for i in range(n - 1):
            (ax, ay), (bx, by) = self.pts[i], self.pts[i + 1]
            dx, dy = bx - ax, by - ay
            l2 = dx * dx + dy * dy
            t = ((x - ax) * dx + (y - ay) * dy) / l2 if l2 else 0.0
            if i > 0:
                t = max(t, 0.0)
            if i < n - 2:
                t = min(t, 1.0)
            px, py = ax + dx * t, ay + dy * t
            d2 = (x - px) ** 2 + (y - py) ** 2
            if d2 < best[0]:
                side = dx * (y - ay) - dy * (x - ax)
                best = (d2, self.s[i] + t * math.sqrt(l2), math.copysign(math.sqrt(d2), side))
        return best[1], best[2]

    def sdf(self, x, y, half):
        best = 1e18
        for (ax, ay), (bx, by) in zip(self.pts, self.pts[1:]):
            dx, dy = bx - ax, by - ay
            l2 = dx * dx + dy * dy
            t = min(1.0, max(0.0, ((x - ax) * dx + (y - ay) * dy) / l2)) if l2 else 0.0
            best = min(best, (x - ax - dx * t) ** 2 + (y - ay - dy * t) ** 2)
        return math.sqrt(best) - half


def outline(pieces, step=2.0):
    """the outline (ccw) of a union of pieces: (Path, half width) or ("circle", x, y, r)"""

    def sdf(x, y):
        d = 1e18
        for pc in pieces:
            if pc[0] == "circle":
                d = min(d, math.hypot(x - pc[1], y - pc[2]) - pc[3])
            else:
                d = min(d, pc[0].sdf(x, y, pc[1]))
        return d

    xs, ys = [], []
    for pc in pieces:
        if pc[0] == "circle":
            xs += [pc[1] - pc[3], pc[1] + pc[3]]
            ys += [pc[2] - pc[3], pc[2] + pc[3]]
        else:
            xs += [p[0] - pc[1] for p in pc[0].pts] + [p[0] + pc[1] for p in pc[0].pts]
            ys += [p[1] - pc[1] for p in pc[0].pts] + [p[1] + pc[1] for p in pc[0].pts]
    # (an odd offset, so no grid point falls exactly on an edge: a corner exactly on the outline would give two
    # crossings the same position)
    x0, y0 = min(xs) - 3.0137 * step, min(ys) - 3.0291 * step
    nx = int((max(xs) - x0) / step) + 4
    ny = int((max(ys) - y0) / step) + 4
    grid = [[sdf(x0 + i * step, y0 + j * step) or 1e-6 for i in range(nx)] for j in range(ny)]

    def interp(i0, j0, i1, j1):
        a, b = grid[j0][i0], grid[j1][i1]
        t = a / (a - b)
        return (round(x0 + (i0 + (i1 - i0) * t) * step, 4), round(y0 + (j0 + (j1 - j0) * t) * step, 4))

    nxt = {}
    for j in range(ny - 1):
        for i in range(nx - 1):
            c = [grid[j][i] < 0, grid[j][i + 1] < 0, grid[j + 1][i + 1] < 0, grid[j + 1][i] < 0]
            edges = {}
            if c[0] != c[1]:
                edges["b"] = interp(i, j, i + 1, j)
            if c[1] != c[2]:
                edges["r"] = interp(i + 1, j, i + 1, j + 1)
            if c[2] != c[3]:
                edges["t"] = interp(i, j + 1, i + 1, j + 1)
            if c[3] != c[0]:
                edges["l"] = interp(i, j, i, j + 1)
            if not edges:
                continue
            # walk each cell's crossings so the inside stays on the left (ccw outer loop)
            code = c[0] | c[1] << 1 | c[2] << 2 | c[3] << 3
            table = {1: [("l", "b")], 2: [("b", "r")], 3: [("l", "r")], 4: [("r", "t")], 5: [("l", "b"), ("r", "t")],
                     6: [("b", "t")], 7: [("l", "t")], 8: [("t", "l")], 9: [("t", "b")], 10: [("b", "r"), ("t", "l")],
                     11: [("t", "r")], 12: [("r", "l")], 13: [("r", "b")], 14: [("b", "l")]}
            for e0, e1 in table.get(code, []):
                nxt[edges[e1]] = edges[e0]
    loops = []
    seen = set()
    for start in nxt:
        if start in seen:
            continue
        loop = [start]
        seen.add(start)
        p = nxt[start]
        while p != start and p in nxt and p not in seen:
            loop.append(p)
            seen.add(p)
            p = nxt[p]
        if len(loop) > 8:
            loops.append(loop)
    loop = max(loops, key=lambda l: abs(poly_area(l)))
    return ccw(simplify(loop, 0.25))


def simplify(pts, tol):
    """Ramer-Douglas-Peucker on a closed loop: drops points within tol of the line kept, and keeps no edge longer
    than 12 (so the rails and the ground grid still follow it closely)"""

    def rdp(seg):
        if len(seg) < 3:
            return seg
        a, b = seg[0], seg[-1]
        dx, dy = b[0] - a[0], b[1] - a[1]
        l = math.hypot(dx, dy)
        best, k = -1.0, 0
        for i in range(1, len(seg) - 1):
            p = seg[i]
            d = abs(dx * (p[1] - a[1]) - dy * (p[0] - a[0])) / l if l > 1e-9 else math.dist(p, a)
            if d > best:
                best, k = d, i
        if best > tol or l > 12.0:
            return rdp(seg[:k + 1])[:-1] + rdp(seg[k:])
        return [a, b]

    far = max(range(len(pts)), key=lambda i: math.dist(pts[0], pts[i]))
    first = rdp(pts[:far + 1])
    second = rdp(pts[far:] + [pts[0]])
    return first[:-1] + second[:-1]


class Section:
    """objects made inside `with H.section(s, d):` are built in a straight local frame (x along the path, y to the
    left) and moved to the path at s, offset d to the left"""

    def __init__(self, H, s, d=0.0):
        self.H, self.s, self.d = H, s, d

    def __enter__(self):
        self.before = set(bpy.data.objects)
        return self

    def __exit__(self, *exc):
        x, y, a = self.H.path.at(self.s)
        x -= math.sin(a) * self.d
        y += math.cos(a) * self.d
        m = Matrix.Translation((x, y, 0.0)) @ Matrix.Rotation(a, 4, "Z")
        for ob in set(bpy.data.objects) - self.before:
            if ob.type == "MESH" and ob.get("mg_role") != "obstacle":
                ob.data.transform(m)
                ob.data.update()
            else:
                ob.matrix_basis = m @ ob.matrix_basis
                ob["mg_yaw"] = ob.get("mg_yaw", 0.0) + a
        return False

    def point(self, lx, ly):
        x, y, a = self.H.path.at(self.s)
        x -= math.sin(a) * self.d
        y += math.cos(a) * self.d
        return (x + lx * math.cos(a) - ly * math.sin(a), y + lx * math.sin(a) + ly * math.cos(a))


class Hole:
    def __init__(self, number, par, name, theme, coll, z0):
        self.n, self.par, self.name, self.theme = number, par, name, theme
        self.coll = coll
        self.z0 = z0
        self.rails, self.trim = THEME[theme]
        self.path = None
        self.half = 18.0
        self.tee = None
        self.cup = None
        self.h = flat
        self.poly = None
        self.sign_side = -1

    def tag(self, s):
        return f"Hole{self.n:02d}_{s}"

    def shape(self, steps, half, circles=(), extra=()):
        """the hole's centreline and outline: half = the lane's half width; circles = (s, d, r) round greens on the
        path (s along it, d to the left); extra = more (Path, half) pieces"""
        self.path = Path(steps)
        self.half = half
        pieces = [(self.path, half)] + list(extra)
        for s, d, r in circles:
            x, y, a = self.path.at(s)
            pieces.append(("circle", x - math.sin(a) * d, y + math.cos(a) * d, r))
        self.poly = outline(pieces)
        return self.poly

    def along(self, f):
        """a height function of (s, d) as one of (x, y)"""
        def h(x, y):
            s, d = self.path.project(x, y)
            return f(s, d)
        return h

    def lane(self, h=None, mat="Turf", surface="green", holes=(), grid=GRID, rails=True, name="Green"):
        if h is not None:
            self.h = h
        g = ground(self.tag(name), self.poly, self.h, self.z0, self.coll, mat, surface, self.n, holes, grid)
        if rails:
            rail(self.tag(name + "Rail"), self.poly, self.h, self.z0, DECK, self.coll, self.rails, self.n)
        return g

    def section(self, s, d=0.0):
        return Section(self, s, d)

    def p(self, s, d=0.0):
        x, y, a = self.path.at(s)
        return (x - math.sin(a) * d, y + math.cos(a) * d)

    def strip(self, s0, s1, d0, d1):
        """a polygon covering the path from s0 to s1, between d0 and d1 to its left"""
        n = max(2, int(abs(s1 - s0) / 6))
        left = [self.p(s0 + (s1 - s0) * k / n, d1) for k in range(n + 1)]
        right = [self.p(s0 + (s1 - s0) * k / n, d0) for k in range(n + 1)]
        return right + list(reversed(left))

    def finish(self):
        x, y = self.tee or self.p(18.0)
        self.tee = (x, y)
        _, _, ta = self.path.at(0.0)
        z = self.z0 + self.h(x, y)
        bm = bmesh.new()
        bmesh.ops.create_grid(bm, x_segments=1, y_segments=1, size=1.0)
        bmesh.ops.scale(bm, vec=(9.0, 7.0, 1.0), verts=bm.verts)
        bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=Matrix.Rotation(ta, 3, "Z"), verts=bm.verts)
        bmesh.ops.translate(bm, vec=(x, y, z + 0.15), verts=bm.verts)
        finish(bm, "TeeMat")
        new_object(self.tag("TeeMat"), bm, self.coll, "TeeMat", mg_role="visual", mg_hole=self.n)
        empty(self.tag("Tee"), self.coll, (x, y, z), self.n, mg_tee=1, mg_par=self.par, mg_name=self.name, mg_aim=ta)
        cx, cy = self.cup
        cz = self.z0 + self.h(cx, cy)
        cup(self.n, self.coll, cx, cy, cz, self.rails)
        # the flag beside the cup, on the side away from the tee
        dx, dy = cx - x, cy - y
        l = math.hypot(dx, dy) or 1.0
        fx, fy = cx + dx / l * 8.0 - dy / l * 5.0, cy + dy / l * 8.0 + dx / l * 5.0
        if not inside((fx, fy), self.poly):
            fx, fy = cx - dy / l * 8.0, cy + dx / l * 8.0
        flag(self.n, self.coll, fx, fy, self.z0 + self.h(fx, fy))
        ring_walls(self.tag("FlagWall"), self.coll, (fx, fy), 0.8, self.z0 + self.h(fx, fy) - 2, 40.0, self.n, 0.5, segments=6)
        # the sign beside the tee, outside the rail, facing out to the side
        s = self.sign_side
        sx, sy = self.p(10.0, s * (self.half + RAIL_T + 10.0))
        sign(self.n, self.coll, sx, sy, DECK, ta + s * math.pi / 2)

    def obstacle(self, ob, motion, **numbers):
        ob["mg_role"] = "obstacle"
        ob["mg_motion"] = motion
        for k, v in numbers.items():
            ob["mg_" + k] = v
        return ob


def blades(name, coll, count, size, offset, axis, mat, hole):
    """a rotor: count boxes of size, pushed out by offset along x, turned evenly round the axis ("X" or "Z")"""
    bm = bmesh.new()
    for k in range(count):
        v = bmesh.ops.create_cube(bm, size=1.0)["verts"]
        bmesh.ops.scale(bm, vec=size, verts=v)
        bmesh.ops.translate(bm, vec=offset, verts=v)
        bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=Matrix.Rotation(k * math.tau / count, 3, axis), verts=v)
    finish(bm, mat)
    return new_object(name, bm, coll, mat, mg_hole=hole)


# ---------------------------------------------------------------- the holes
#
# Each hole is built in its own frame: its path starts at the origin heading +x. Shapes, after the park in the
# reference photo: keyholes (a lane into a round green), an L, a hairpin, S-bends, a lollipop, a dumbbell, an
# island green...


def hole01(H):  # Welcome Mat: a keyhole, over a little hump into the round green
    H.shape([("L", 200)], 17, circles=[(200, 0, 32)])
    H.lane(H.along(lambda s, d: 3.0 * math.exp(-((s - 100.0) / 30.0) ** 2)))
    H.cup = H.p(200)
    for i, (s, d) in enumerate(((60, 30), (130, -30))):
        with H.section(s, d):
            cylinder(H.tag(f"TireStack{i}"), H.coll, (0, 0, DECK), 8.0, 14.0, "Corrugated", segments=12)


def hole02(H):  # Old Windmill: straight through the mill when the sails let you, to a round green
    H.shape([("L", 260)], 26, circles=[(262, 0, 36)])
    H.lane()
    z = H.z0
    with H.section(150):
        box(H.tag("MillLeft"), H.coll, (25, -32, z + 28), (50, 46, 64), "WhiteWood", "wall", H.n)
        box(H.tag("MillRight"), H.coll, (25, 32, z + 28), (50, 46, 64), "WhiteWood", "wall", H.n)
        box(H.tag("MillTunnelRoof"), H.coll, (25, 0, z + 42), (50, 20, 36), "WhiteWood", "visual", H.n)
        bm = bmesh.new()  # the roof: a pyramid
        bmesh.ops.create_cone(bm, cap_ends=True, segments=4, radius1=62, radius2=0, depth=40)
        bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=Matrix.Rotation(math.pi / 4, 3, "Z"), verts=bm.verts)
        bmesh.ops.translate(bm, vec=(25, 0, z + 80), verts=bm.verts)
        finish(bm, "Striped")
        new_object(H.tag("MillRoof"), bm, H.coll, "Striped", mg_role="visual", mg_hole=H.n)
        # the sails: four blades turning round a level axis along the lane, in front of the mill (origin at the hub)
        bm = bmesh.new()
        for k in range(4):
            v = bmesh.ops.create_cube(bm, size=1.0)["verts"]
            bmesh.ops.scale(bm, vec=(1.5, 9.0, 40.0), verts=v)
            bmesh.ops.translate(bm, vec=(0, 0, -24.0), verts=v)
            bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=Matrix.Rotation(k * math.pi / 2, 3, "X"), verts=v)
        hubv = bmesh.ops.create_cone(bm, cap_ends=True, segments=12, radius1=4, radius2=4, depth=4)["verts"]
        bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=Matrix.Rotation(math.pi / 2, 3, "Y"), verts=hubv)
        finish(bm, "Striped")
        sails = new_object(H.tag("MillSails"), bm, H.coll, "Striped", mg_hole=H.n)
        sails.location = (-4.0, 0.0, z + 46.0)
        # blade 0 hangs straight down at time 0; the walls across the tunnel's mouth are there while a blade is down
        H.obstacle(sails, "wheel", period=8.0, phase=0.0, blades=4, half=11.0, axis=0.0)
        wall_path(H.tag("MillSailsWall"), H.coll, [(-6.0, -11.0), (-6.0, 11.0)], z, 20.0, H.n, 0.4, False, "owall",
                  mg_obstacle=sails.name)
    H.cup = H.p(268, 8)


def hole03(H):  # Nuka Alley: an S-bend through giant bottles
    H.shape([("L", 40), ("A", 110, 40), ("A", 110, -40), ("L", 40)], 30, circles=[(240, 0, 34)])
    H.lane()
    for i, (s, d) in enumerate(((70, -10), (95, 14), (120, -14), (140, 6), (165, -8), (185, 14))):
        with H.section(s, d):
            cylinder(H.tag(f"Bottle{i}"), H.coll, (0, 0, H.z0), 4.5, 26.0, "Striped", segments=12)
            cylinder(H.tag(f"BottleNeck{i}"), H.coll, (0, 0, H.z0 + 26.0), 2.0, 9.0, "Striped", segments=10)
            ring_walls(H.tag(f"BottleWall{i}"), H.coll, (0, 0), 4.6, H.z0 - 2, 30.0, H.n, 0.55, segments=10)
    H.cup = H.p(240)


def hole04(H):  # Rust Ramp: up the ramp, then a left turn on the plateau to the round green
    H.shape([("L", 170), ("A", 45, 90), ("L", 50)], 18, circles=[(H_len := 170 + 45 * math.pi / 2 + 50, 0, 30)])
    H.lane(H.along(lambda s, d: 20.0 * smoothstep(60.0, 130.0, s)))
    with H.section(H_len):
        box(H.tag("BackWall"), H.coll, (38, 0, H.z0 + 36), (6, 60, 32), "Corrugated", "visual", H.n)
    H.cup = H.p(H_len)
    H.sign_side = -1


def hole05(H):  # Toxic Crossing: a dumbbell, round at both ends, with a plank bridge over the muck between
    H.shape([("L", 280)], 30, circles=[(10, 0, 36), (272, 0, 36)])
    water = [H.strip(110, 180, -40, -9), H.strip(110, 180, 9, 40)]
    H.lane(holes=water)
    for i, w in enumerate(water):
        ground(H.tag(f"Muck{i}"), grown(w, 6.0), flat, H.z0 - 3.0, H.coll, "Muck", "water", H.n)
    H.tee = H.p(10)
    H.cup = H.p(272)


def hole06(H):  # Vertibird: a lollipop, the rotor spinning over the big round green
    H.shape([("L", 220)], 18, circles=[(160, 0, 70)])
    H.lane()
    z = H.z0
    with H.section(160):
        cylinder(H.tag("RotorMast"), H.coll, (0, 0, z), 6.0, 12.0, "BlueMetal", "visual", H.n, segments=12)
        ring_walls(H.tag("RotorMastWall"), H.coll, (0, 0), 6.0, z - 2, 14.0, H.n, 0.5, segments=12)
        rotor = blades(H.tag("Rotor"), H.coll, 4, (52.0, 5.0, 2.0), (32.0, 0, 0), "Z", "BlueMetal", H.n)
        rotor.location = (0.0, 0.0, z + 5.0)
        H.obstacle(rotor, "spin", period=-5.0, phase=0.0)
        for k in range(4):
            a = k * math.pi / 2
            c, s = math.cos(a), math.sin(a)
            pts = [(6, -2.5), (58, -2.5), (58, 2.5), (6, 2.5)]
            wall_path(H.tag(f"RotorWall{k}"), H.coll, [(px * c - py * s, py * c + px * s) for px, py in pts], z - 1, 8.0,
                      H.n, 0.5, True, "owall", mg_obstacle=rotor.name)
    H.cup = H.p(160, -0) if False else H.p(212)
    H.sign_side = 1


def hole07(H):  # Diner Dogleg: a 60 degree bend round the diner
    H.shape([("L", 130), ("A", 60, 60), ("L", 110)], 22, circles=[(130 + 60 * math.pi / 3 + 110, 0, 30)])
    H.lane()
    with H.section(100, 60):
        box(H.tag("Diner"), H.coll, (-20, 10, DECK + 20), (90, 40, 40), "WhiteWood", "visual", H.n)
        box(H.tag("DinerRoof"), H.coll, (-20, 10, DECK + 42), (98, 48, 4), "Striped", "visual", H.n)
    H.cup = H.p(130 + 60 * math.pi / 3 + 110)


def hole08(H):  # The Volcano: a lollipop with the cone in the round green; too soft and it rolls back down
    H.shape([("L", 200)], 17, circles=[(200, 0, 56)])
    c = H.p(200)

    def h(x, y):
        r = math.hypot(x - c[0], y - c[1])
        return 18.0 * (1.0 - smoothstep(12.0, 52.0, r)) - (1.5 * (1.0 - (r / 12.0) ** 2) if r < 12 else 0.0)
    H.lane(h, grid=6.0)
    H.cup = c


def hole09(H):  # Vault Doors: a long straight with two blast doors sliding across, to a round green
    H.shape([("L", 280)], 22, circles=[(282, 0, 32)])
    H.lane()
    z = H.z0
    for i, (s, period, phase) in enumerate(((110.0, 4.0, 0.0), (190.0, 5.0, 0.5))):
        with H.section(s):
            door = box(H.tag(f"Door{i}"), H.coll, (0, 0, 0), (8.0, 34.0, 24.0), "VaultPanel", "obstacle", H.n)
            for v in door.data.vertices:
                v.co.z += 12.0
            door.location = (0.0, 0.0, z - 1)
            H.obstacle(door, "slide", period=period, phase=phase, dx=0.0, dy=20.0)
            wall_path(H.tag(f"DoorWall{i}"), H.coll, [(-4, -17), (4, -17), (4, 17), (-4, 17)], z - 1, 24.0, H.n, 0.4,
                      True, "owall", mg_obstacle=door.name)
    H.cup = H.p(284)


def hole10(H):  # Tire Yard: a clover of three round greens full of bouncy tyres
    H.shape([("L", 180)], 20, circles=[(110, 26, 40), (150, -28, 40), (190, 10, 40)])
    H.lane()
    for i, (s, d) in enumerate(((95, 20), (130, -20), (160, 30), (175, -32), (195, -6))):
        with H.section(s, d):
            bm = bmesh.new()
            bmesh.ops.create_cone(bm, cap_ends=False, segments=16, radius1=9, radius2=9, depth=6)
            bmesh.ops.translate(bm, vec=(0, 0, H.z0 + 3), verts=bm.verts)
            finish(bm, "Corrugated")
            new_object(H.tag(f"Tire{i}"), bm, H.coll, "Corrugated", mg_role="visual", mg_hole=H.n)
            ring_walls(H.tag(f"TireWall{i}"), H.coll, (0, 0), 9.0, H.z0 - 2, 8.0, H.n, 0.85, segments=14)
    H.cup = H.p(210, 30)


def hole11(H):  # The Drop: down a narrow chute, round a bend at the bottom, into the round green
    H.shape([("L", 150), ("A", 50, -90), ("L", 40)], 14, circles=[(150 + 50 * math.pi / 2 + 40, 0, 34)])
    H.lane(H.along(lambda s, d: 28.0 * (1.0 - smoothstep(70.0, 130.0, s))))
    with H.section(100):
        box(H.tag("ChuteRoof"), H.coll, (0, 0, H.z0 + 50), (60, 36, 3), "VaultPanel", "visual", H.n)
    H.cup = H.p(150 + 50 * math.pi / 2 + 40)
    H.sign_side = 1


def hole12(H):  # Wrecking Ball: a hairpin, the ball swinging over the first straight
    H.shape([("L", 170), ("A", 50, 180), ("L", 110)], 20, circles=[(170 + 50 * math.pi + 110, 0, 30)])
    H.lane()
    z = H.z0
    with H.section(100):
        for s in (-1, 1):
            box(H.tag(f"GantryPost{s}"), H.coll, (0, s * 34, z + 46), (5, 5, 100), "Corrugated", "visual", H.n)
        box(H.tag("GantryBeam"), H.coll, (0, 0, z + 96), (6, 74, 5), "Corrugated", "visual", H.n)
        bm = bmesh.new()
        ball = bmesh.ops.create_uvsphere(bm, u_segments=16, v_segments=10, radius=9.0)["verts"]
        bmesh.ops.translate(bm, vec=(0, 0, -84.0), verts=ball)
        chain = bmesh.ops.create_cone(bm, cap_ends=True, segments=6, radius1=0.6, radius2=0.6, depth=76)["verts"]
        bmesh.ops.translate(bm, vec=(0, 0, -38.0), verts=chain)
        finish(bm, "Corrugated")
        wb = new_object(H.tag("WreckingBall"), bm, H.coll, "Corrugated", mg_hole=H.n)
        wb.location = (0.0, 0.0, z + 94.0)
        H.obstacle(wb, "pendulum", axis=0.0, length=84.0, swing=28.0, period=3.4, phase=0.0)
        ring_walls(H.tag("WreckingBallWall"), H.coll, (0.0, 0.0), 9.0, z - 1, 18.0, H.n, 0.5, segments=12, role="owall",
                   mg_obstacle=wb.name)
    H.cup = H.p(170 + 50 * math.pi + 110)
    H.sign_side = -1


def hole13(H):  # Fat Man Jump: up the ramp and over the muck to the island green
    H.path = Path([("L", 175)])
    H.half = 18.0
    # the lane: round at the tee, cut square at the ramp's lip, no rail across the lip
    start = [(18 * math.cos(a), 18 * math.sin(a)) for a in [math.pi / 2 + math.pi * k / 12 for k in range(13)]]
    H.poly = ccw([(175, -18), (175, 18)] + start)
    H.h = H.along(lambda s, d: 14.0 * smoothstep(120.0, 170.0, s))
    ground(H.tag("Green"), H.poly, H.h, H.z0, H.coll, "Turf", "green", H.n)
    open_rail = [(175, 18)] + start + [(175, -18)]
    rail(H.tag("GreenRail"), open_rail, H.h, H.z0, DECK, H.coll, H.rails, H.n, closed=False)
    # the island: a round green 40 past the lip, its rail open towards the ramp
    cx, r = 250.0, 36.0
    island = [(cx + r * math.cos(a), r * math.sin(a)) for a in [math.tau * k / 40 for k in range(40)]]
    ground(H.tag("Island"), island, flat, H.z0, H.coll, "Turf", "green", H.n)
    arc = [(cx + r * math.cos(a), r * math.sin(a)) for a in [math.radians(-145 + 290 * k / 30) for k in range(31)]]
    rail(H.tag("IslandRail"), arc, flat, H.z0, DECK, H.coll, H.rails, H.n, closed=False)
    ground(H.tag("Muck"), [(165, -44), (cx - 10, -44), (cx - 10, 44), (165, 44)], flat, H.z0 - 6.0, H.coll, "Muck", "water", H.n)
    with H.section(60, 40):
        box(H.tag("Bomb"), H.coll, (0, 0, DECK + 7), (34, 14, 14), "BlueMetal", "visual", H.n)
    H.cup = (cx + 6.0, 0.0)


def hole14(H):  # Glowing Sea: a wide wavy lane over rolling ground, a patch of slag and a toxic pool
    H.shape([("A", 160, 30), ("A", 160, -60), ("A", 160, 30)], 40, circles=[(250, 0, 44)])
    L = H.path.length
    H.h = H.along(lambda s, d: 5.0 * math.sin(s / 24.0) * math.cos(d / 20.0) * smoothstep(30, 60, s) * (1 - smoothstep(L - 60, L - 20, s)))
    pool = [H.p(150 + 18 * math.cos(a / 12 * math.tau), -18 + 13 * math.sin(a / 12 * math.tau)) for a in range(12)]
    slag = [H.p(90 + 24 * math.cos(a / 12 * math.tau), 16 + 16 * math.sin(a / 12 * math.tau)) for a in range(12)]
    H.lane(holes=[pool, slag], grid=6.0)
    ground(H.tag("Pool"), grown(pool, 8.0), flat, H.z0 - 3.0, H.coll, "Muck", "water", H.n)
    ground(H.tag("Slag"), grown(slag, 7.0), H.h, H.z0 + 0.3, H.coll, "GlowRock", "slow", H.n)
    H.cup = H.p(255)


def hole15(H):  # Castle Gate: through the gate, then a bend right to the round green
    H.shape([("L", 170), ("A", 70, -50), ("L", 70)], 22, circles=[(170 + 70 * math.radians(50) + 70, 0, 30)])
    H.lane()
    z = H.z0
    with H.section(120):
        box(H.tag("CastleLeft"), H.coll, (0, -22, z + 22), (20, 22, 52), "WhiteWood", "wall", H.n)
        box(H.tag("CastleRight"), H.coll, (0, 22, z + 22), (20, 22, 52), "WhiteWood", "wall", H.n)
        box(H.tag("CastleArch"), H.coll, (0, 0, z + 40), (20, 24, 16), "WhiteWood", "visual", H.n)
        for s in (-1, 1):
            cylinder(H.tag(f"Tower{s}"), H.coll, (0, s * 40, DECK), 11.0, 74.0, "Striped", "visual", H.n, segments=12)
        gate = box(H.tag("Portcullis"), H.coll, (0, 0, 0), (3.0, 24.0, 30.0), "Corrugated", "obstacle", H.n)
        for v in gate.data.vertices:
            v.co.z += 15.0
        gate.location = (-11.0, 0.0, z - 1)
        H.obstacle(gate, "lift", height=28.0, period=7.0, phase=0.0)
        wall_path(H.tag("PortcullisWall"), H.coll, [(-12, -12), (-10, -12), (-10, 12), (-12, 12)], z - 1, 30.0, H.n, 0.3,
                  True, "owall", mg_obstacle=gate.name)
    H.cup = H.p(170 + 70 * math.radians(50) + 70)


def hole16(H):  # Mr. Handy: a barbell, his three arms sweeping the big round green in the middle
    H.shape([("L", 270)], 18, circles=[(140, 0, 62), (272, 0, 28)])
    H.lane()
    z = H.z0
    with H.section(140):
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=16, v_segments=10, radius=12.0)
        bmesh.ops.translate(bm, vec=(0, 0, z + 14), verts=bm.verts)
        finish(bm, "VaultPanel")
        new_object(H.tag("HandyBody"), bm, H.coll, "VaultPanel", mg_role="visual", mg_hole=H.n)
        ring_walls(H.tag("HandyBodyWall"), H.coll, (0, 0), 9.0, z - 1, 10.0, H.n, 0.5, segments=12)
        arms = blades(H.tag("HandyArms"), H.coll, 3, (36.0, 3.0, 3.0), (28.0, 0, 0), "Z", "Striped", H.n)
        arms.location = (0.0, 0.0, z + 3.0)
        H.obstacle(arms, "spin", period=4.5, phase=0.0)
        for k in range(3):
            a = k * math.tau / 3
            c, s = math.cos(a), math.sin(a)
            pts = [(10, -1.5), (46, -1.5), (46, 1.5), (10, 1.5)]
            wall_path(H.tag(f"HandyArmWall{k}"), H.coll, [(px * c - py * s, py * c + px * s) for px, py in pts], z - 1, 6.0,
                      H.n, 0.5, True, "owall", mg_obstacle=arms.name)
    H.cup = H.p(272)


def hole17(H):  # Zigzag: three sharp switchbacks
    H.shape([("L", 60), ("A", 25, 70), ("L", 60), ("A", 25, -140), ("L", 60), ("A", 25, 70), ("L", 50)], 17,
            circles=[(None, 0, 28)] if False else [])
    L = H.path.length
    H.poly = outline([(H.path, 17), ("circle", *H.p(L), 28)])
    H.lane()
    H.cup = H.p(L)


def hole18(H):  # Last Stop: a long sweeping curve up into the funnel
    H.shape([("L", 60), ("A", 140, 70), ("L", 60)], 20)
    L = H.path.length
    c = H.p(L)
    H.poly = outline([(H.path, 20), ("circle", c[0], c[1], 44)])

    def h(x, y):
        r = math.hypot(x - c[0], y - c[1])
        if r < 44.0:
            return 12.0 - 8.0 * (1.0 - (r / 44.0) ** 2)
        s, _ = H.path.project(x, y)
        return 12.0 * smoothstep(40.0, L - 50.0, s)
    H.lane(h, grid=6.0)
    H.cup = c


BUILDERS = [hole01, hole02, hole03, hole04, hole05, hole06, hole07, hole08, hole09, hole10, hole11, hole12, hole13,
            hole14, hole15, hole16, hole17, hole18]

# where each hole goes: its tee's position on the course and the way its frame is turned (maths angle, degrees).
# Found by tools/course/anneal.py (nothing overlapping, room for paths between, each tee a short walk from the last
# hole's cup), from the holes' shapes as built here.
PLACEMENTS = {
    1: (258.2, -329.4, 300),
    2: (421.7, -408.7, 120),
    3: (192.4, -184.8, 60),
    4: (337.7, -33.2, 300),
    5: (512.4, -49.5, 135),
    6: (222.9, 129.9, 210),
    7: (-106.8, 39.0, 105),
    8: (-378.4, 335.5, 225),
    9: (-502.7, 77.1, 270),
    10: (-411.1, -215.2, 255),
    11: (-347.1, -411.2, 30),
    12: (-81.1, -321.3, 330),
    13: (-56.1, -182.2, 75),
    14: (-20.6, 158.4, 15),
    15: (112.3, 347.2, 45),
    16: (194.2, 532.0, 195),
    17: (-141.5, 464.1, 225),
    18: (-369.6, 222.8, 255),
}


def footprints():
    """each built hole's extent in its own frame (before it's moved), for layout.py"""
    out = {}
    for num, *_ in HOLES:
        xs, ys = [], []
        for ob in bpy.data.collections[f"Hole{num:02d}"].objects:
            if ob.type != "MESH" or ob.get("mg_hidden"):
                continue
            for v in ob.data.vertices:
                p = ob.matrix_basis @ v.co
                xs.append(p.x)
                ys.append(p.y)
        out[num] = (round(min(xs)), round(max(xs)), round(min(ys)), round(max(ys)))
    return out


def hole_matrix(num, tee_local):
    tx, ty, deg = PLACEMENTS[num]
    a = math.radians(deg)
    tl = Matrix.Rotation(a, 2) @ Vector(tee_local)
    return Matrix.Translation((tx - tl.x, ty - tl.y, 0.0)) @ Matrix.Rotation(a, 4, "Z"), a


def offset_poly(poly, d):
    """a ccw polygon pushed out by d (mitred, as the rails are)"""
    poly = ccw(poly)
    n = len(poly)
    out = []
    for i in range(n):
        a, b, c = Vector(poly[i - 1]), Vector(poly[i]), Vector(poly[(i + 1) % n])
        d1 = (b - a).normalized() if (b - a).length > 1e-6 else (c - b).normalized()
        d2 = (c - b).normalized() if (c - b).length > 1e-6 else d1
        n1, n2 = Vector((d1.y, -d1.x)), Vector((d2.y, -d2.x))
        m = n1 + n2
        m = m.normalized() if m.length > 1e-6 else n1
        k = 1.0 / max(m.dot(n1), 0.3)
        q = b + m * d * k
        out.append((q.x, q.y))
    return out


def convex_hull(pts):
    pts = sorted(set(pts))
    if len(pts) < 3:
        return pts

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]


def slab(name, coll, poly, top, bottom, mat_top, mat_side, role="visual"):
    """a flat-topped block over a polygon: its top in one material, its sides in another"""
    poly = ccw(poly)
    bm = bmesh.new()
    vt = [bm.verts.new((x, y, top)) for x, y in poly]
    bm.faces.new(vt)
    bm.normal_update()
    box_uv(bm, MATERIALS[mat_top][1])
    top_ob = new_object(name, bm, coll, mat_top, mg_role=role, mg_hole=0)
    bm = bmesh.new()
    n = len(poly)
    vt = [bm.verts.new((x, y, top)) for x, y in poly]
    vb = [bm.verts.new((x, y, bottom)) for x, y in poly]
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((vb[i], vb[j], vt[j], vt[i]))
    bm.normal_update()
    box_uv(bm, MATERIALS[mat_side][1])
    new_object(name + "Side", bm, coll, mat_side, mg_role="visual", mg_hole=0)
    return top_ob


def rounded(poly, r, steps=4):
    """a convex polygon with its corners rounded off"""
    out = []
    n = len(poly)
    for i in range(n):
        p, a, b = Vector(poly[i]), Vector(poly[i - 1]), Vector(poly[(i + 1) % n])
        da, db = (a - p).normalized(), (b - p).normalized()
        k = min(r, (a - p).length / 2, (b - p).length / 2)
        p1, p2 = p + da * k, p + db * k
        for s in range(steps + 1):
            t = s / steps
            q = (1 - t) ** 2 * p1 + 2 * (1 - t) * t * p + t * t * p2  # a quadratic curve through the corner
            out.append((q.x, q.y))
    return out


def build(root, placements=None):
    global _root
    _root = root
    if placements:
        PLACEMENTS.update(placements)
    bpy.ops.wm.read_homefile(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "NONE"
    top = collection("MiniGolf")
    z0 = DECK + GREEN

    sys.setrecursionlimit(10000)
    built = {}
    for i, (num, par, name, theme) in enumerate(HOLES):
        coll = collection(f"Hole{num:02d}", top)
        H = Hole(num, par, name, theme, coll, z0)
        BUILDERS[i](H)
        H.finish()
        built[num] = H
    bpy.context.view_layer.update()
    feet = footprints()
    info = {n: {"tee": [round(v, 1) for v in H.tee], "cup": [round(v, 1) for v in H.cup],
                "outline": [(round(x, 1), round(y, 1)) for x, y in offset_poly(H.poly, RAIL_T + 2.0)]} for n, H in built.items()}
    if not PLACEMENTS:
        return {"footprints": feet, "holes": info}

    # move each hole into place
    pads = []
    ends = {}
    for num, H in built.items():
        m, a = hole_matrix(num, H.tee)
        for ob in H.coll.objects:
            if ob.type == "MESH" and ob.get("mg_role") != "obstacle":
                ob.data.transform(m)  # bake: meshes keep identity transforms
                ob.data.update()
            else:
                ob.matrix_basis = m @ ob.matrix_basis  # (not matrix_world: stale until a depsgraph update)
            ob["mg_turn"] = a
        pads.append([tuple((m @ Vector((x, y, 0))).xy) for x, y in offset_poly(H.poly, RAIL_T + 14.0)])
        ends[num] = (tuple((m @ Vector((H.tee[0], H.tee[1], 0))).xy), tuple((m @ Vector((H.cup[0], H.cup[1], 0))).xy))

    ground_coll = collection("Grounds", top)
    hull = convex_hull([p for pad in pads for p in pad])
    # the lawn: the hull pushed out 40 and rounded
    cx = sum(p[0] for p in hull) / len(hull)
    cy = sum(p[1] for p in hull) / len(hull)
    lawn = [(x + (Vector((x - cx, y - cy)).normalized() * 40).x, y + (Vector((x - cx, y - cy)).normalized() * 40).y) for x, y in hull]
    slab("Lawn", ground_coll, rounded(lawn, 120.0, 6), DECK - 0.5, -SKIRT, "Lawn", "Concrete")
    # a paved pad under each hole, and a path from each cup to the next tee
    for i, pad in enumerate(pads):
        slab(f"Pad{i + 1:02d}", ground_coll, pad, DECK + 0.02 * (i % 5), DECK - 1.0, "Paving", "Paving")
    for num in range(1, 18):
        a, b = Vector(ends[num][1]), Vector(ends[num + 1][0])
        d = b - a
        if d.length < 1:
            continue
        side = Vector((-d.y, d.x)).normalized() * 17.0
        quad = [tuple(a - side), tuple(b - side), tuple(b + side), tuple(a + side)]
        slab(f"Path{num:02d}", ground_coll, quad, DECK + 0.15, DECK - 1.0, "Paving", "Paving")
    return {"holes": len(HOLES), "objects": len(bpy.data.objects), "footprints": feet}


def preview(path, eye=(-700.0, -1150.0, 1050.0), target=(0.0, -40.0, 0.0), lens=35.0, size=(1600, 1000)):
    """renders the course (EEVEE, a sun, a pale sky) to a PNG, for checking it by eye"""
    sc = bpy.context.scene
    for o in bpy.data.objects:
        if o.get("mg_hidden"):
            o.hide_render = True
    cam = bpy.data.objects.get("PreviewCam")
    if not cam:
        cam = bpy.data.objects.new("PreviewCam", bpy.data.cameras.new("PreviewCam"))
        sc.collection.objects.link(cam)
    cam.data.clip_end = 20000
    cam.data.lens = lens
    cam.location = eye
    cam.rotation_euler = (Vector(target) - Vector(eye)).to_track_quat("-Z", "Y").to_euler()
    sc.camera = cam
    sun = bpy.data.objects.get("PreviewSun")
    if not sun:
        sun = bpy.data.objects.new("PreviewSun", bpy.data.lights.new("PreviewSun", "SUN"))
        sc.collection.objects.link(sun)
    sun.data.energy = 4.0
    sun.rotation_euler = (math.radians(40), math.radians(15), math.radians(-30))
    w = bpy.data.worlds.get("World") or bpy.data.worlds.new("World")
    sc.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.6, 0.65, 1)
    w.node_tree.nodes["Background"].inputs[1].default_value = 0.8
    sc.render.engine = "BLENDER_EEVEE"
    sc.render.resolution_x, sc.render.resolution_y = size
    sc.render.resolution_percentage = 100
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    return path
