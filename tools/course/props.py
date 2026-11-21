"""Detailed models for the course: a small modelling kit (lathe, sweep, bevelled boxes, extruded outlines, text) and
the holes' set pieces, built from real mini golf parks' and 1950s roadside attractions' shapes: a Dutch smock windmill,
a streamline diner, a stone castle, giant Nuka-Cola bottles, a crane with a wrecking ball, a Mr. Handy...

A Model gathers parts by material and makes one object per material. Everything here is visual: the physics stays in
build_course.py's plain walls and greens (hidden when a model stands in for them), and the detailed meshes are left out
of the game's collision (mg_nocol) - each set piece gets a simple box or cylinder for that instead (collision_box).

Units: game units, about 70 to the metre (the ball is 1.5 in radius, a rail 8 high).
"""

import math
import os

import bmesh
import bpy
from mathutils import Matrix, Vector

import build_course as B

FONTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts")

# ---------------------------------------------------------------- transforms


def T(x=0.0, y=0.0, z=0.0):
    return Matrix.Translation((x, y, z))


def RX(deg):
    return Matrix.Rotation(math.radians(deg), 4, "X")


def RY(deg):
    return Matrix.Rotation(math.radians(deg), 4, "Y")


def RZ(deg):
    return Matrix.Rotation(math.radians(deg), 4, "Z")


def S(x, y=None, z=None):
    y = x if y is None else y
    z = x if z is None else z
    return Matrix.Diagonal((x, y, z, 1.0))


# ---------------------------------------------------------------- parts: each a new bmesh, built round the origin


def box(sx, sy, sz, bevel=0.0, segs=1):
    """a box centred on the origin, its edges rounded off by bevel"""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=(sx, sy, sz), verts=bm.verts)
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=bm.edges[:], offset=min(bevel, sx * 0.45, sy * 0.45, sz * 0.45), offset_type="OFFSET",
                        profile_type="SUPERELLIPSE", segments=segs, profile=0.5, affect="EDGES", clamp_overlap=True)
    return bm


def lathe(profile, seg=24, start=0.0, sweep=360.0):
    """turned round z: profile = [(radius, z)] from bottom to top; a radius of 0 closes it to a point"""
    bm = bmesh.new()
    full = abs(sweep - 360.0) < 1e-6
    n = seg if full else seg + 1
    rings = []
    for r, z in profile:
        if r <= 1e-6:
            rings.append([bm.verts.new((0.0, 0.0, z))])
            continue
        ring = []
        for k in range(n):
            a = math.radians(start + sweep * k / seg)
            ring.append(bm.verts.new((r * math.cos(a), r * math.sin(a), z)))
        rings.append(ring)
    for a, b in zip(rings, rings[1:]):
        if len(a) == 1 and len(b) == 1:
            continue
        for k in range(seg):
            k2 = (k + 1) % n if full else k + 1
            if len(a) == 1:
                bm.faces.new((a[0], b[k], b[k2]))
            elif len(b) == 1:
                bm.faces.new((a[k], b[0], a[k2]))
            else:
                bm.faces.new((a[k], a[k2], b[k2], b[k]))
    bm.normal_update()
    return bm


def cyl(r, h, seg=16, r2=None, cap=True, bevel=0.0):
    """a cylinder (or cone, r2) from z=0 up to h, its rims rounded by bevel"""
    r2 = r if r2 is None else r2
    if bevel > 0:
        b = min(bevel, r * 0.4, h * 0.4)
        prof = [(0.0, 0.0)] if cap else []
        prof += [(r - b, 0.0), (r - b * 0.3, b * 0.3), (r, b), (r2, h - b), (r2 - b * 0.3, h - b * 0.3), (r2 - b, h)]
        prof += [(0.0, h)] if cap else []
        return lathe(prof, seg)
    prof = ([(0.0, 0.0)] if cap else []) + [(r, 0.0), (r2, h)] + ([(0.0, h)] if cap else [])
    return lathe(prof, seg)


def tube(r, h, thick, seg=16):
    """an open pipe: r outside, thick wall, from 0 to h"""
    ri = r - thick
    return lathe([(ri, h), (ri, 0.0), (r, 0.0), (r, h), (ri, h)], seg)


def torus(R, r, seg=24, rseg=10, squash=1.0):
    prof = [(R + r * math.cos(a), r * math.sin(a) * squash) for a in [math.tau * k / rseg - math.pi / 2 for k in range(rseg + 1)]]
    return lathe(prof, seg)


def sphere(r, u=16, v=10):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=u, v_segments=v, radius=r)
    return bm


def prism(poly, z0, z1, bevel=0.0):
    """a polygon (xy, ccw) pulled up from z0 to z1, its top rim rounded by bevel"""
    bm = bmesh.new()
    poly = B.ccw(list(poly))
    bot = [bm.verts.new((x, y, z0)) for x, y in poly]
    top = [bm.verts.new((x, y, z1)) for x, y in poly]
    bm.faces.new(list(reversed(bot)))
    bm.faces.new(top)
    n = len(poly)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((bot[i], bot[j], top[j], top[i]))
    bm.normal_update()
    if bevel > 0:
        rim = [e for e in bm.edges if all(abs(v.co.z - z1) < 1e-6 for v in e.verts)]
        bmesh.ops.bevel(bm, geom=rim, offset=bevel, offset_type="OFFSET", profile_type="SUPERELLIPSE", segments=2,
                        profile=0.5, affect="EDGES", clamp_overlap=True)
    return bm


def sweep(path, profile, closed=False, caps=True, up=(0.0, 0.0, 1.0)):
    """profile [(side, height)] carried along path [(x, y, z)]; side is to the path's right, height up"""
    bm = bmesh.new()
    pts = [Vector(p) for p in path]
    n = len(pts)
    upv = Vector(up)
    rings = []
    for i in range(n):
        a = pts[i - 1] if (closed or i > 0) else pts[i]
        c = pts[(i + 1) % n] if (closed or i < n - 1) else pts[i]
        d1 = (pts[i] - a)
        d2 = (c - pts[i])
        d1 = d1.normalized() if d1.length > 1e-6 else d2.normalized()
        d2 = d2.normalized() if d2.length > 1e-6 else d1
        t = (d1 + d2)
        t = t.normalized() if t.length > 1e-6 else d1
        right = t.cross(upv)
        right = right.normalized() if right.length > 1e-6 else Vector((1, 0, 0))
        k = 1.0 / max(abs(right.dot(d1.cross(upv).normalized())) if d1.cross(upv).length > 1e-6 else 1.0, 0.35)
        nup = right.cross(t).normalized()
        rings.append([bm.verts.new(pts[i] + right * s * k + nup * h) for s, h in profile])
    m = len(profile)
    for i in range(n if closed else n - 1):
        a, b = rings[i], rings[(i + 1) % n]
        for j in range(m - 1):
            bm.faces.new((a[j], b[j], b[j + 1], a[j + 1]))
    if caps and not closed and m > 2:
        bm.faces.new(list(reversed(rings[0])))
        bm.faces.new(rings[-1])
    bm.normal_update()
    return bm


def text(body, size, depth, font="Anton-Regular.ttf", bevel=0.0, align="CENTER"):
    """flat lettering standing up: xz plane, reading along +x, depth towards -y, its base at z=0"""
    cu = bpy.data.curves.new("tmp_text", "FONT")
    cu.body = body
    cu.size = size
    cu.extrude = depth / 2
    cu.bevel_depth = bevel
    cu.align_x = align
    cu.font = bpy.data.fonts.load(os.path.join(FONTS, font), check_existing=True)
    ob = bpy.data.objects.new("tmp_text", cu)
    bpy.context.scene.collection.objects.link(ob)
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    bpy.data.objects.remove(ob)
    bpy.data.curves.remove(cu)
    bm = bmesh.new()
    bm.from_mesh(me)
    bpy.data.meshes.remove(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=0.001)
    bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=Matrix.Rotation(math.pi / 2, 3, "X"), verts=bm.verts)
    zmin = min(v.co.z for v in bm.verts)
    bmesh.ops.translate(bm, vec=(0, 0, -zmin), verts=bm.verts)
    return bm


def chain(length, link=2.4, wire=0.4, seg=8):
    """a hanging chain from z=0 down to -length: links turned alternately"""
    bm = bmesh.new()
    pitch = link * 1.4
    count = max(1, int(length / pitch))
    for k in range(count):
        part = torus(link * 0.5, wire, seg=seg, rseg=6)
        bmesh.ops.scale(part, vec=(1.0, 1.0, 1.0), verts=part.verts)
        m = T(0, 0, -k * pitch - link * 0.5) @ RZ(90 * (k % 2)) @ RX(90) @ S(0.75, 1.3, 1.0)
        _merge(bm, part, m)
    return bm


def _merge(into, part, matrix=None):
    me = bpy.data.meshes.new("tmp_part")
    part.to_mesh(me)
    part.free()
    if matrix is not None:
        me.transform(matrix)
    into.from_mesh(me)
    bpy.data.meshes.remove(me)


# ---------------------------------------------------------------- a model: parts by material


class Model:
    def __init__(self, name, coll, hole):
        self.name, self.coll, self.hole = name, coll, hole
        self.parts = {}   # material -> bmesh (box-mapped when built)
        self.keep = {}    # material -> bmesh with its own UVs

    def add(self, mat, part, matrix=None, keep_uv=False):
        store = self.keep if keep_uv else self.parts
        if mat not in store:
            store[mat] = bmesh.new()
        _merge(store[mat], part, matrix)
        return self

    def build(self, role="visual", sharp=38.0, collide=False, matrix=None, **props):
        """one object per material, named <name>_<material>; returns them"""
        out = []
        for store, keep in ((self.parts, False), (self.keep, True)):
            for mat, bm in store.items():
                if matrix is not None:
                    bm.transform(matrix)
                bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=0.002)
                # every face outwards (a lathe or a sweep can wind its faces inwards: the game culls those, Blender
                # draws them anyway). Open sheets (the canvas: two-sided by its two copies) left as they are
                if mat != "Canvas":
                    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
                bm.normal_update()
                if not keep:
                    B.box_uv(bm, B.MATERIALS[mat][1] if mat in B.MATERIALS else 96.0)
                smooth(bm, sharp)
                ob = B.new_object(f"{self.name}_{mat}", bm, self.coll, mat, mg_role=role, mg_hole=self.hole, **props)
                if not collide:
                    ob["mg_nocol"] = 1
                out.append(ob)
        self.parts, self.keep = {}, {}
        return out


def smooth(bm, sharp_deg):
    """smooth shading, with the edges sharper than sharp_deg kept hard"""
    lim = math.radians(sharp_deg)
    for f in bm.faces:
        f.smooth = True
    for e in bm.edges:
        if len(e.link_faces) != 2:
            e.smooth = False
            continue
        a, b = e.link_faces
        e.smooth = a.normal.angle(b.normal, 0.0) < lim


def collision_box(name, coll, hole, center, size, rot=0.0):
    """an invisible box the game's collision gets in place of a detailed model"""
    ob = B.box(name, coll, center, size, "Concrete", "visual", hole, rot=rot)
    hide(ob)
    ob["mg_colproxy"] = 1
    return ob


def collision_cyl(name, coll, hole, center, r, h, seg=10):
    ob = B.cylinder(name, coll, center, r, h, "Concrete", "visual", hole, segments=seg)
    hide(ob)
    ob["mg_colproxy"] = 1
    return ob


def hide(ob):
    """physics only: kept for the walls and the game's collision, not drawn"""
    ob["mg_hidden"] = 1
    ob.display_type = "WIRE"
    ob.hide_render = True
    return ob


def attach(parts, obstacle):
    """the parts of a moving obstacle beside its main mesh: parented to it, moved with it in the game too"""
    for p in parts:
        p["mg_role"] = "opart"
        p["mg_attach"] = obstacle.name
        p.pop("mg_nocol", None)
        p.parent = obstacle
        p.matrix_parent_inverse = Matrix.Identity(4)
    return parts


# ---------------------------------------------------------------- small pieces used all over


def rivets(m, mat, pts, r=0.45, matrix=None):
    for p in pts:
        m.add(mat, sphere(r, 6, 4), (matrix or Matrix.Identity(4)) @ T(*p) @ S(1, 1, 0.5))


def stripes_box(m, sx, sy, sz, matrix):
    """a hazard-striped band"""
    m.add("Striped", box(sx, sy, sz, 0.15), matrix)


# ---------------------------------------------------------------- the set pieces


_VANILLA = {}


def vanilla(H, name, rel, at, height=None, width=None, lie=False, deg=0.0, skip=()):
    """one of the game's own meshes in the hole, scaled to a height (or a width across) - its shapes keep the
    game's materials; visual only (no collision of its own). lie: turned onto its side (a tyre lying flat)."""
    import dressing
    parts = dressing.library_one(B._root, rel)
    parts = [(o, m) for o, m in parts if not any(k in o.name or k in o.data.name for k in skip)]
    if not parts:
        return []
    rot = RX(90.0) if lie else Matrix.Identity(4)
    xs, ys, zs = [], [], []
    for ob, m in parts:
        for c in ob.bound_box:
            q = rot @ m @ Vector(c)
            xs.append(q.x)
            ys.append(q.y)
            zs.append(q.z)
    if height:
        s = height / max(max(zs) - min(zs), 1e-3)
    else:
        s = width / max(max(xs) - min(xs), max(ys) - min(ys), 1e-3)
    cx, cy = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    place = T(*at) @ RZ(deg) @ S(s) @ T(-cx, -cy, -min(zs)) @ rot  # centred on at, standing on its z
    out = []
    for i, (src, m) in enumerate(parts):
        ob = bpy.data.objects.new(H.tag(f"{name}_{i}"), src.data)
        H.coll.objects.link(ob)
        ob.matrix_basis = place @ m
        ob["mg_role"] = "visual"
        ob["mg_hole"] = H.n
        ob["mg_nocol"] = 1
        ob["mg_vanilla"] = 1
        out.append(ob)
    return out


def windmill(H, z, floor):
    """Hole 2: a Dutch smock mill: brick base with an arched tunnel through it, a tapering eight-sided clapboard tower,
    a boat-shaped cap, a gallery round its waist. In the hole's straight frame: the mill's front face at x=0 (the sails
    turn just in front of it, at x=-4), the tunnel along the lane, 18 wide."""
    m = Model(H.tag("Mill"), H.coll, H.n)
    # the base: brick, 50 deep x 110 wide, from the ground up to 26 over the lane, a stone plinth and cap course
    base_h = 26.0
    tunnel_w, tunnel_h = 18.0, 20.0
    for side in (-1, 1):
        w = (110 - tunnel_w) / 2
        yc = side * (tunnel_w / 2 + w / 2)
        hb = z + base_h - 2 - floor
        m.add("Brick", box(50, w, hb, 0.4), T(25, yc, floor + hb / 2))
    # under the tunnel's floor (the lane is up on its rise): brick down to the ground
    if z - floor > 2:
        for x in (0.3, 49.7):
            m.add("Brick", box(1.0, tunnel_w, z - floor - 1.0), T(x, 0, floor + (z - floor - 1.0) / 2))
    # over the tunnel: brick, with an arch cut in its underside (a half lathe as the arch's soffit)
    m.add("Brick", box(50, tunnel_w + 2, base_h - tunnel_h - 2), T(25, 0, z + tunnel_h + 2 + (base_h - tunnel_h - 2) / 2))
    arch = lathe([(tunnel_w / 2, -25.0), (tunnel_w / 2, 25.0)], 12, 0, 180)
    m.add("Brick", arch, T(25, 0, z + tunnel_h - tunnel_w / 2 + 1) @ RY(90) @ RZ(90))
    # keystone arch trim round the tunnel mouth, front and back
    for x in (0.0, 50.0):
        ring = []
        for k in range(13):
            a = math.pi * k / 12
            ring.append((x, -math.cos(a) * (tunnel_w / 2 + 1.5), z + tunnel_h - tunnel_w / 2 + 1 + math.sin(a) * (tunnel_w / 2 + 1.5)))
        m.add("Stone", sweep(ring, [(-1.6, -1.6), (1.6, -1.6), (1.6, 1.6), (-1.6, 1.6), (-1.6, -1.6)], up=(1, 0, 0)))
        for side in (-1, 1):
            m.add("Stone", box(3.2, 3.2, tunnel_h - tunnel_w / 2 + 1, 0.3), T(x, side * (tunnel_w / 2 + 1.5), z + (tunnel_h - tunnel_w / 2 + 1) / 2))
    m.add("Stone", box(54, 114, 3, 0.6), T(25, 0, floor + 1.5))       # plinth
    m.add("Stone", box(54, 114, 2.5, 0.8), T(25, 0, z + base_h + 0.25))  # cap course
    # the tower: eight-sided, tapering, white clapboard, with corner posts
    t0, t1 = z + base_h + 1.5, z + 104.0
    r0, r1 = 28.0, 18.0
    oct_ = lambda r, zz: [(25 + r * math.cos(math.radians(22.5 + 45 * k)), r * math.sin(math.radians(22.5 + 45 * k)), zz) for k in range(8)]
    tw = bmesh.new()
    lo = [tw.verts.new(p) for p in oct_(r0, t0)]
    hi = [tw.verts.new(p) for p in oct_(r1, t1)]
    for k in range(8):
        tw.faces.new((lo[k], lo[(k + 1) % 8], hi[(k + 1) % 8], hi[k]))
    m.add("Siding", tw)
    for k in range(8):  # corner posts
        a = math.radians(22.5 + 45 * k)
        m.add("WhiteWood", sweep([(25 + (r0 + 0.6) * math.cos(a), (r0 + 0.6) * math.sin(a), t0), (25 + (r1 + 0.6) * math.cos(a), (r1 + 0.6) * math.sin(a), t1)],
                                 [(-1, -1), (1, -1), (1, 1), (-1, 1), (-1, -1)], up=(math.cos(a), math.sin(a), 0)))
    # windows: two small framed windows up the tower's sides, a door over the tunnel at the back
    for k, h in ((2, 0.45), (6, 0.45), (3, 0.7), (5, 0.7), (0, 0.6)):
        a = math.radians(45 * k)
        rr = r0 + (r1 - r0) * h + 0.4
        zz = t0 + (t1 - t0) * h
        mw = T(25 + rr * math.cos(a), rr * math.sin(a), zz) @ RZ(math.degrees(a)) @ RY(-math.degrees(math.atan((r0 - r1) / (t1 - t0))))
        m.add("DarkWood", box(1.2, 7.0, 9.0, 0.3), mw)
        m.add("Glass", box(1.4, 5.4, 7.4), mw @ T(0.2, 0, 0))
        m.add("WhiteWood", box(1.6, 0.8, 7.4), mw @ T(0.3, 0, 0))
        m.add("WhiteWood", box(1.6, 5.4, 0.8), mw @ T(0.3, 0, 0))
        m.add("WhiteWood", box(2.4, 9.0, 1.2, 0.3), mw @ T(0.6, 0, -5.0))  # sill
    # the gallery (reefing stage) round the tower, on brackets, with a railing
    gz = z + 46.0 + 12.0
    gr = r0 + (r1 - r0) * ((gz - t0) / (t1 - t0))
    m.add("DarkWood", lathe([(gr - 1, gz - 1.2), (gr + 9, gz - 1.2), (gr + 9, gz), (gr - 1, gz)], 8, 22.5), T(25, 0, 0))
    rail_r = gr + 8.4
    for k in range(16):
        a = math.tau * k / 16 + math.pi / 16
        m.add("DarkWood", box(0.7, 0.7, 7.0), T(25 + rail_r * math.cos(a), rail_r * math.sin(a), gz + 3.5))
    m.add("DarkWood", torus(rail_r, 0.45, 32, 6), T(25, 0, gz + 7.0))
    m.add("DarkWood", torus(rail_r, 0.3, 32, 6), T(25, 0, gz + 3.5))
    for k in range(8):
        a = math.radians(22.5 + 45 * k)
        m.add("DarkWood", sweep([(25 + (gr + 0.5) * math.cos(a), (gr + 0.5) * math.sin(a), gz - 7), (25 + (gr + 7) * math.cos(a), (gr + 7) * math.sin(a), gz - 1.3)],
                                [(-0.6, -0.6), (0.6, -0.6), (0.6, 0.6), (-0.6, 0.6), (-0.6, -0.6)]))
    # the cap: a boat-shaped roof of shingles, a ridge, a cap ring
    m.add("Stone", lathe([(r1 + 1.5, t1), (r1 + 1.5, t1 + 2.0), (0.0, t1 + 2.0)], 16), T(25, 0, 0))
    cap = lathe([(r1 + 4.0, 0.0), (r1 + 3.0, 6.0), (r1 - 2.0, 13.0), (r1 - 8.0, 18.0), (6.0, 21.0), (0.0, 22.0)], 24)
    m.add("Shingles", cap, T(25, 0, t1 + 2.0) @ S(1.25, 1.0, 1.0))
    m.add("RedMetal", sphere(1.8, 10, 6), T(25, 0, t1 + 24.5))
    m.add("RedMetal", cyl(0.4, 6.0, 6), T(25, 0, t1 + 24.0))
    # the windshaft's housing out of the cap's front, down to the hub at (-4, 0, z+46)
    hub = Vector((-4.0, 0.0, z + 46.0))
    m.add("DarkWood", cyl(3.2, 27.0, 12, 3.6, bevel=0.5), T(hub.x + 1.0, 0, hub.z) @ RY(90))
    return m.build()


def windmill_sails(H, sails):
    """the four sails on the hub, each a stock with a lattice frame and a canvas, as parts of the obstacle (in its own
    frame: the hub at the origin, the sails in the yz plane, blade 0 hanging down -z)"""
    m = Model(H.tag("Sails"), H.coll, H.n)
    for k in range(4):
        r = RX(90 * k)
        # the stock (whip), from the hub to the tip
        m.add("DarkWood", box(1.6, 1.6, 46.0, 0.25), r @ T(0, 0, -24.0))
        # the lattice: two long rails, bars across every 3.5
        for y in (2.0, 10.0):
            m.add("WhiteWood", box(0.9, 0.9, 38.0, 0.15), r @ T(0.4, y, -27.0))
        for k2 in range(12):
            zz = -9.0 - k2 * 3.4
            m.add("WhiteWood", box(0.7, 9.2, 0.6), r @ T(0.4, 6.0, zz))
        # the canvas, a hair behind the lattice, a little billow
        cv = bmesh.new()
        rows = []
        for i in range(6):
            zz = -8.5 - i * 7.3
            row = []
            for j in range(4):
                yy = 2.0 + j * 8.0 / 3
                bul = 0.9 * math.sin(math.pi * j / 3) * math.sin(math.pi * i / 5)
                row.append(cv.verts.new((1.1 + bul, yy, zz)))
            rows.append(row)
        for i in range(5):
            for j in range(3):
                cv.faces.new((rows[i][j], rows[i][j + 1], rows[i + 1][j + 1], rows[i + 1][j]))
        back = cv.copy()
        bmesh.ops.reverse_faces(back, faces=back.faces[:])
        m.add("Canvas", cv, r)
        m.add("Canvas", back, r)
    # the hub: a cast boss and a cap
    m.add("DarkWood", cyl(4.2, 3.0, 16, bevel=0.6), RY(90) @ T(0, 0, -1.5))
    m.add("RedMetal", sphere(2.0, 12, 8), T(-2.0, 0, 0) @ S(0.6, 1, 1))
    for k in range(8):
        a = math.tau * k / 8
        m.add("RedMetal", sphere(0.4, 6, 4), T(-1.6, 3.1 * math.cos(a), 3.1 * math.sin(a)))
    parts = m.build(role="opart")
    for p in parts:
        p.matrix_basis = Matrix.Identity(4)
    return attach(parts, sails)


def nuka_bottle(H, i, z, s=1.0):
    """Hole 3: a giant Nuka-Cola bottle, the rocket-shaped kind: 4.5 across at the base (its wall), 36 high"""
    m = Model(H.tag(f"Bottle{i}"), H.coll, H.n)
    prof = [(0.0, 0.0), (4.0, 0.0), (4.5, 0.6), (4.6, 3.0), (4.3, 6.0), (4.6, 10.0), (4.6, 16.0), (4.1, 20.0),
            (3.2, 23.0), (3.6, 25.5), (3.0, 27.5), (2.0, 29.5), (1.8, 32.5)]
    m.add("Glass", lathe(prof, 20), T(0, 0, z) @ S(s))
    # the label band and the shoulder ring
    m.add("RedMetal", lathe([(4.65, 9.0), (4.7, 9.5), (4.7, 15.5), (4.65, 16.0)], 20), T(0, 0, z) @ S(s))
    m.add("WhiteMetal", lathe([(4.72, 11.6), (4.75, 11.8), (4.75, 13.2), (4.72, 13.4)], 20), T(0, 0, z) @ S(s))
    m.add("Chrome", lathe([(1.9, 32.3), (2.25, 32.5), (2.3, 34.0), (2.05, 34.4), (0.0, 34.6)], 16), T(0, 0, z) @ S(s))
    # the fins round the base (it's a rocket)
    for k in range(3):
        fin = prism([(0, 0), (4.5, 0), (0.6, 9.0), (0, 9.0)], -0.4, 0.4, 0.15)
        m.add("RedMetal", fin, T(0, 0, z) @ S(s) @ RZ(120 * k) @ T(4.2, 0, 0) @ RX(90))
    return m.build()


def tire(m, x, y, z, r=9.0, w=6.0, mat="Rubber"):
    """a car tyre lying flat: its centre at (x, y, z) (its bottom on z - w/2)"""
    ri = r * 0.55
    prof = [(ri, -w * 0.32), (ri + 0.6, -w * 0.45), (r - 1.4, -w * 0.5), (r - 0.4, -w * 0.4), (r, -w * 0.2), (r, w * 0.2),
            (r - 0.4, w * 0.4), (r - 1.4, w * 0.5), (ri + 0.6, w * 0.45), (ri, w * 0.32)]
    m.add(mat, lathe(prof + [prof[0]], 28), T(x, y, z))
    # tread blocks
    for k in range(28):
        a = 360.0 * k / 28
        m.add(mat, box(1.0, 1.6, w * 0.32, 0.2), T(x, y, z) @ RZ(a) @ T(r + 0.15, 0, w * (0.18 if k % 2 else -0.18)))


def tire_stack(H, i, z, count=3, r=8.0):
    m = Model(H.tag(f"Tires{i}"), H.coll, H.n)
    w = 5.0
    for k in range(count):
        tire(m, 0.0, 0.0, z + w / 2 + k * (w - 0.3), r, w)
    # painted white wall on the top one: a ring of white
    m.add("WhiteMetal", lathe([(r * 0.62, 0.0), (r * 0.86, 0.0)], 28), T(0, 0, z + count * (w - 0.3) + 0.35))
    return m.build()


def diner(H, z):
    """Hole 7: a 1950s streamline diner: stainless bands, a window band, rounded ends, a neon DINER on the roof.
    In the section's frame: 90 long (x), 40 deep (y), centred at (-20, 10)."""
    m = Model(H.tag("Diner"), H.coll, H.n)
    L, W, Hh = 90.0, 40.0, 34.0
    cx, cy = -20.0, 10.0
    r = W / 2

    def stadium(rr, n=10):
        pts = []
        for k in range(n + 1):
            a = -math.pi / 2 + math.pi * k / n
            pts.append((cx + (L / 2 - r) + rr * math.cos(a), cy + rr * math.sin(a)))
        for k in range(n + 1):
            a = math.pi / 2 + math.pi * k / n
            pts.append((cx - (L / 2 - r) + rr * math.cos(a), cy + rr * math.sin(a)))
        return pts
    # the base: a stone skirt; the walls in bands: steel below, a window band, steel above
    m.add("Stone", prism(stadium(r + 1.0), z, z + 5.0, 0.4))
    m.add("WhiteMetal", prism(stadium(r), z + 5.0, z + 15.0))
    m.add("RedMetal", prism(stadium(r + 0.4), z + 13.0, z + 15.0, 0.3))
    m.add("Glass", prism(stadium(r - 0.5), z + 15.0, z + 27.0))
    m.add("Chrome", prism(stadium(r + 0.2), z + 27.0, z + 28.5, 0.3))
    m.add("WhiteMetal", prism(stadium(r), z + 28.5, z + Hh))
    # window mullions
    pts = stadium(r + 0.25, 12)
    for k in range(0, len(pts), 2):
        x, y = pts[k]
        m.add("Chrome", box(0.9, 0.9, 12.0), T(x, y, z + 21.0))
    # stainless flutes (horizontal ribs) in the steel band
    for zz in (7.0, 9.0, 11.0):
        m.add("Chrome", prism(stadium(r + 0.35), z + zz, z + zz + 0.6))
    # the roof: a low barrel over the top, an overhang
    m.add("RoofMetal", prism(stadium(r + 2.5), z + Hh, z + Hh + 1.2, 0.5))
    roof_l = L - 2 * r
    m.add("RoofMetal", lathe([(r + 1.0, 0.0), (r * 0.9, 3.0), (r * 0.6, 5.0), (0.0, 6.0)], 20), T(cx, cy, z + Hh + 1.2) @ S(1.0 + roof_l / (2 * r), 1.0, 1.0))
    # the door in the middle of the long side facing -y, steps up to it
    m.add("Chrome", box(14.0, 1.6, 22.0, 0.3), T(cx, cy - r - 0.3, z + 16.0))
    m.add("Glass", box(10.0, 1.8, 17.0), T(cx, cy - r - 0.4, z + 15.5))
    m.add("Stone", box(18.0, 6.0, 2.5, 0.3), T(cx, cy - r - 3.0, z + 1.25))
    m.add("Stone", box(16.0, 3.5, 2.5, 0.3), T(cx, cy - r - 2.0, z + 3.75))
    # the sign on the roof: a red frame, DINER in neon-white letters, poles
    sx, sy, sz = cx, cy, z + Hh + 6.0
    m.add("RedMetal", box(46.0, 2.0, 15.0, 0.8), T(sx, sy, sz + 9.0))
    m.add("Chrome", box(47.0, 1.0, 16.0, 0.6), T(sx, sy + 0.6, sz + 9.0))
    lt = text("DINER", 11.0, 1.4, "ArchivoBlack-Regular.ttf")
    m.add("Neon", lt, T(sx, sy - 1.0, sz + 3.0))
    lt2 = text("DINER", 11.0, 1.4, "ArchivoBlack-Regular.ttf")
    m.add("Neon", lt2, T(sx, sy + 1.0, sz + 3.0) @ RZ(180))
    for dx in (-18.0, 18.0):
        m.add("Chrome", cyl(0.8, 6.0, 8), T(sx + dx, sy, z + Hh + 1.0))
    # a little EAT arrow sign on a pole by the corner
    m.add("Chrome", cyl(0.9, 48.0, 8), T(cx + L / 2 + 6, cy - r - 4, z))
    m.add("RedMetal", prism([(0, -1), (14, -1), (19, 4), (14, 9), (0, 9)], -0.8, 0.8, 0.3), T(cx + L / 2 + 6, cy - r - 4, z + 36) @ RX(90) @ RZ(0))
    m.add("Neon", text("EAT", 6.0, 1.8), T(cx + L / 2 + 13, cy - r - 4.4, z + 37.0))
    return m.build()


def castle(H, z, floor):
    """Hole 15: a stone gatehouse: two square towers with battlements either side of the arch, the portcullis
    between, round corner towers with red conical roofs. Section frame: the gate on x=0, the lane through it."""
    m = Model(H.tag("Castle"), H.coll, H.n)
    # gate towers (the walls' physics boxes: 20 x 22 x 52 at y=+-22)
    for side in (-1, 1):
        y = side * 22.0
        hb = z + 50.0 - floor
        m.add("CastleStone", box(22.0, 24.0, hb, 0.6), T(0, y, floor + hb / 2))
        # battlements: a corbelled parapet and merlons
        m.add("CastleStone", box(25.0, 27.0, 3.0, 0.5), T(0, y, z + 51.5))
        for i in range(3):
            for j in range(3):
                if i == 1 and j == 1:
                    continue
                m.add("CastleStone", box(5.0, 5.0, 5.5, 0.4), T(-9.5 + 9.5 * i, y - 10.5 + 10.5 * j, z + 55.7))
        # arrow slits
        for zz in (18.0, 34.0):
            m.add("DarkWood", box(1.4, 1.4, 7.0), T(-11.2, y, z + zz))
    # the arch over the gate, a stone band round it
    m.add("CastleStone", box(22.0, 22.0, 16.0, 0.4), T(0, 0, z + 40.0))
    ring = []
    for k in range(13):
        a = math.pi * k / 12
        ring.append((-11.3, -math.cos(a) * 12.5, z + 30.0 + math.sin(a) * 2.0))
    m.add("Stone", sweep(ring, [(-1.2, -1.5), (1.2, -1.5), (1.2, 1.5), (-1.2, 1.5), (-1.2, -1.5)], up=(1, 0, 0)))
    # a shield over the arch
    m.add("RedMetal", prism([(-4, 4), (4, 4), (4, -1), (0, -5), (-4, -1)], -0.6, 0.6, 0.3), T(-11.6, 0, z + 41.0) @ RZ(90) @ RX(90))
    # the round towers at the corners: stone drums, a corbel ring, a red cone roof with a finial
    for side in (-1, 1):
        y = side * 40.0
        m.add("CastleStone", cyl(11.0, z + 64.0 - floor, 20, bevel=0.6), T(0, y, floor))
        m.add("CastleStone", lathe([(11.0, 0.0), (13.0, 2.5), (13.0, 5.0), (0.0, 5.0)], 20), T(0, y, z + 64.0))
        m.add("Shingles", cyl(14.0, 26.0, 20, 0.3), T(0, y, z + 69.0))
        m.add("RedMetal", cyl(14.4, 1.5, 20, 13.6), T(0, y, z + 68.8))
        m.add("Chrome", sphere(1.2, 8, 6), T(0, y, z + 95.8))
        m.add("Chrome", cyl(0.3, 7.0, 6), T(0, y, z + 94.5))
        for zz in (20.0, 44.0):
            m.add("DarkWood", box(2.0, 1.4, 8.0), T(-10.6, y, z + zz))
    # wooden doors folded back inside the arch
    for side in (-1, 1):
        m.add("DarkWood", box(12.0, 1.4, 24.0, 0.3), T(6.0, side * 11.0, z + 12.0))
        for zz in (4.0, 12.0, 20.0):
            m.add("Chrome", box(11.0, 1.6, 1.0), T(6.0, side * 10.8, z + zz))
    return m.build()


def portcullis(H, gate):
    """the portcullis bars, part of the lifting obstacle (its frame: x=0 on the gate's plane, z from 0 up to 30)"""
    m = Model(H.tag("PortcullisBars"), H.coll, H.n)
    for k in range(7):
        y = -10.5 + 3.5 * k
        m.add("DarkMetal", box(1.6, 1.0, 30.0, 0.2), T(0, y, 15.0))
        m.add("DarkMetal", cyl(0.8, 3.0, 6, 0.0), T(0, y, -1.0))
    for zz in (4.0, 12.0, 20.0, 28.0):
        m.add("DarkMetal", box(2.2, 24.0, 1.4, 0.2), T(0, 0, zz))
    parts = m.build(role="opart")
    return attach(parts, gate)


def wrecking_crane(H, z):
    """Hole 12: a steel gantry of I-beams, rivetted, rusty red, with a winch box; the ball's chain hangs from it.
    Section frame: posts at y=+-34, the beam at z+96 along y."""
    m = Model(H.tag("Gantry"), H.coll, H.n)
    ib = [(-2.5, -3.0), (2.5, -3.0), (2.5, -2.2), (0.5, -2.2), (0.5, 2.2), (2.5, 2.2), (2.5, 3.0), (-2.5, 3.0),
          (-2.5, 2.2), (-0.5, 2.2), (-0.5, -2.2), (-2.5, -2.2), (-2.5, -3.0)]
    for s in (-1, 1):
        # a pair of legs, splayed, braced
        for dx in (-9.0, 9.0):
            m.add("RedMetal", sweep([(dx, s * 36.0, z - 4), (dx * 0.3, s * 34.0, z + 94)], ib, up=(0, 1, 0)))
        for k in range(4):
            zz = z + 10 + 22 * k
            t = (zz - z) / 98
            x0 = 9.0 - 6.3 * t
            x1 = 9.0 - 6.3 * min(1, t + 0.22)
            m.add("RedMetal", sweep([(-x0, s * 35.5, zz), (x1, s * 35.0, zz + 21)], [(-0.6, -0.6), (0.6, -0.6), (0.6, 0.6), (-0.6, 0.6), (-0.6, -0.6)]))
            m.add("RedMetal", sweep([(x0, s * 35.5, zz), (-x1, s * 35.0, zz + 21)], [(-0.6, -0.6), (0.6, -0.6), (0.6, 0.6), (-0.6, 0.6), (-0.6, -0.6)]))
        # a footing
        m.add("Concrete", box(26.0, 10.0, 6.0, 0.5), T(0, s * 36.0, z - 3.0))
        for dx in (-9.0, 9.0):
            m.add("DarkMetal", box(8.0, 8.0, 1.0, 0.2), T(dx, s * 36.0, z + 0.5))
            rivets(m, "DarkMetal", [(dx + a, s * 36.0 + b, z + 1.0) for a in (-3, 3) for b in (-3, 3)], 0.6)
    # the beam: an I-section along y, rivetted gusset plates at the joints
    m.add("RedMetal", sweep([(0, -40.0, z + 97.0), (0, 40.0, z + 97.0)], [(p[1] * 1.4, p[0] * 1.2) for p in ib]))
    for s in (-1, 1):
        for x in (-3.4, 3.4):
            m.add("DarkMetal", prism([(-6, -4), (6, -4), (6, 6), (-6, 6)], -0.3, 0.3), T(x, s * 34.0, z + 92.0) @ RY(90))
    # the winch housing over the middle, a drum, the chain's sheave
    m.add("YellowMetal", box(14.0, 12.0, 9.0, 0.8), T(0, 0, z + 105.5))
    m.add("DarkMetal", box(15.0, 13.0, 1.0, 0.3), T(0, 0, z + 110.5))
    m.add("DarkMetal", cyl(3.0, 10.0, 16, bevel=0.4), T(-5.0, 0, z + 103.0) @ RY(90))
    stripes_box(m, 14.2, 12.2, 2.0, T(0, 0, z + 103.0))
    return m.build()


def wrecking_ball(H, wb):
    """the ball's chain and its lifting eye, part of the pendulum (its frame: the pivot at the origin, hanging down)"""
    m = Model(H.tag("WreckingBallParts"), H.coll, H.n)
    m.add("DarkMetal", chain(74.0, 2.6, 0.45), T(0, 0, -1.0))
    m.add("DarkMetal", torus(2.0, 0.7, 16, 8), T(0, 0, -76.0) @ RX(90))
    m.add("DarkMetal", cyl(2.5, 3.0, 12, 1.8), T(0, 0, -78.0))
    m.add("Rust", sphere(9.0, 24, 14), T(0, 0, -84.0))
    for k in range(8):  # weld seams
        m.add("DarkMetal", torus(9.0, 0.35, 24, 4), T(0, 0, -84.0) @ RZ(45 * k) @ RX(90))
    m.add("DarkMetal", torus(9.05, 0.4, 24, 4), T(0, 0, -84.0))
    parts = m.build(role="opart")
    return attach(parts, wb)


def mr_handy(H, z):
    """Hole 16: Mr. Handy at the middle of the big green: his round body with seams, three eyes on stalks, the jet
    under him; his three arms (the obstacle) sweep round."""
    m = Model(H.tag("Handy"), H.coll, H.n)
    c = Vector((0, 0, z + 16.0))
    m.add("Chrome", sphere(12.0, 28, 18), T(*c))
    m.add("DarkMetal", torus(12.05, 0.45, 32, 6), T(*c))
    for k in range(4):
        m.add("DarkMetal", torus(12.05, 0.3, 32, 4), T(*c) @ RZ(45 * k) @ RX(90))
    # the eyes: stalks from the top, white eyeballs, black irises
    for k in range(3):
        a = math.radians(90 + 120 * k)
        base = c + Vector((math.cos(a) * 6.0, math.sin(a) * 6.0, 10.0))
        tip = base + Vector((math.cos(a) * 3.5, math.sin(a) * 3.5, 9.0))
        m.add("DarkMetal", sweep([tuple(base), tuple(tip)], [(math.cos(t) * 0.7, math.sin(t) * 0.7) for t in [math.tau * i / 8 for i in range(9)]]))
        m.add("DarkMetal", cyl(1.6, 1.5, 12), T(*base) @ T(0, 0, -1.0))
        m.add("WhiteMetal", sphere(2.6, 14, 10), T(*(tip + Vector((0, 0, 1.6)))))
        eye_dir = Vector((math.cos(a), math.sin(a), 0.25)).normalized()
        m.add("DarkMetal", sphere(1.3, 10, 8), T(*(tip + Vector((0, 0, 1.6)) + eye_dir * 1.9)) @ S(1, 1, 1))
    # the jet: a flared nozzle, a blue-white flame
    m.add("DarkMetal", lathe([(5.0, 0.0), (4.0, -3.0), (3.0, -5.0), (4.2, -8.0), (3.6, -8.0), (2.5, -5.0)], 20), T(*c) @ T(0, 0, -10.0))
    m.add("Flame", lathe([(3.2, 0.0), (2.4, -2.0), (0.0, -5.5)], 16), T(*c) @ T(0, 0, -17.5))
    return m.build()


def handy_arms(H, arms):
    """the three arms: jointed, a pincer, a saw and a flamer at their ends (the obstacle's frame: the hub at the origin)"""
    m = Model(H.tag("HandyArmParts"), H.coll, H.n)
    for k in range(3):
        r = RZ(120 * k)
        m.add("DarkMetal", cyl(1.4, 2.0, 10), r @ T(10.0, 0, -1.0))
        m.add("Chrome", sweep([(10.0, 0, 0), (28.0, 0, 0)], [(math.cos(t) * 1.4, math.sin(t) * 1.4) for t in [math.tau * i / 10 for i in range(11)]]), r)
        m.add("DarkMetal", sphere(2.0, 12, 8), r @ T(28.0, 0, 0))
        m.add("Chrome", sweep([(28.0, 0, 0), (44.0, 0, 0)], [(math.cos(t) * 1.1, math.sin(t) * 1.1) for t in [math.tau * i / 10 for i in range(11)]]), r)
        if k == 0:  # pincer
            for s in (-1, 1):
                m.add("DarkMetal", prism([(0, 0), (5, s * 2.5), (7, s * 1.0), (5, s * 0.5)], -0.6, 0.6), r @ T(44.0, 0, 0))
        elif k == 1:  # buzz saw
            ring = []
            for i in range(24):
                a = math.tau * i / 24
                rr = 4.0 if i % 2 else 3.2
                ring.append((rr * math.cos(a), rr * math.sin(a)))
            m.add("Chrome", prism(ring, -0.3, 0.3), r @ T(46.0, 0, 0))
        else:  # flamer nozzle
            m.add("DarkMetal", cyl(1.6, 4.0, 10, 1.0), r @ T(44.0, 0, 0) @ RY(90))
    parts = m.build(role="opart")
    return attach(parts, arms)


def vault_door_frame(H, i, z):
    """Hole 9: a blast door's frame across the lane: two pillars, a lintel with a Vault-Tec plate, hazard stripes;
    the door slides into the pillar on +y"""
    m = Model(H.tag(f"DoorFrame{i}"), H.coll, H.n)
    for s in (-1, 1):
        m.add("VaultPanel", box(14.0, 8.0, 34.0, 0.6), T(0, s * 26.0, z + 13.0))
        stripes_box(m, 14.4, 8.4, 3.0, T(0, s * 26.0, z - 2.0))
        rivets(m, "DarkMetal", [(-7.2, s * 26.0 + dy, z + zz) for dy in (-2.5, 2.5) for zz in (2, 10, 18, 26)], 0.5)
    m.add("VaultPanel", box(14.0, 60.0, 8.0, 0.6), T(0, 0, z + 34.0))
    m.add("YellowMetal", box(14.4, 30.0, 4.0, 0.4), T(0, 0, z + 34.0))
    m.add("Neon", text(f"VAULT {111 + i * 3}", 3.0, 0.6, "ArchivoBlack-Regular.ttf"), T(-7.4, 0, z + 32.4) @ RZ(-90))
    # warning lamps on top
    for s in (-1, 1):
        m.add("DarkMetal", cyl(1.6, 1.5, 12), T(0, s * 20.0, z + 38.0))
        m.add("Lamp", sphere(1.5, 12, 8), T(0, s * 20.0, z + 40.0) @ S(1, 1, 1.3))
    return m.build()


def vault_door(H, door):
    """the door's face: panels, a stripe at its leading edge, rivets (parts of the sliding obstacle; its frame: the
    door's bottom middle at the origin, 8 thick, 34 wide, 24 high)"""
    m = Model(H.tag(f"{door.name.split('_')[-1]}Parts"), H.coll, H.n)
    for s in (-1, 1):
        x = s * 4.2
        for j in range(2):
            m.add("VaultPanel", box(0.8, 14.0, 9.0, 0.3), T(x, -8.0 + 16.0 * j, 7.0))
            m.add("VaultPanel", box(0.8, 14.0, 9.0, 0.3), T(x, -8.0 + 16.0 * j, 17.0))
        stripes_box(m, 8.6, 3.0, 24.2, T(0, -16.0, 12.0))
        rivets(m, "DarkMetal", [(x * 1.03, yy, zz) for yy in (-15, -1, 1, 15) for zz in (1.5, 22.5)], 0.45)
    parts = m.build(role="opart")
    return attach(parts, door)


def volcano_rocks(H, c, z):
    """Hole 8: lava rocks round the volcano's rim, a red glow in the crater"""
    m = Model(H.tag("Volcano"), H.coll, H.n)
    import random
    rnd = random.Random(8)
    for k in range(14):
        a = math.tau * k / 14 + rnd.uniform(-0.1, 0.1)
        r = 60.0 + rnd.uniform(0, 4)
        rock = sphere(rnd.uniform(4.0, 7.0), 8, 6)
        for v in rock.verts:
            v.co *= 1.0 + rnd.uniform(-0.18, 0.18)
        m.add("GlowRock", rock, T(c[0] + r * math.cos(a), c[1] + r * math.sin(a), z + 1.0) @ S(1.2, 1.0, 0.7))
    return m.build()


def rotor_head(H, z):
    """Hole 6: the vertibird's rotor mast: a flanged base tapering up to a collar just under the hub; olive drab"""
    m = Model(H.tag("Mast"), H.coll, H.n)
    m.add("OliveMetal", lathe([(0.0, -4.0), (9.5, -4.0), (9.5, -2.5), (7.5, -1.0), (4.5, 1.5), (3.4, 3.0), (0.0, 3.0)], 24), T(0, 0, z))
    m.add("DarkMetal", lathe([(0.0, -1.2), (8.2, -1.2), (8.2, -0.4), (0.0, -0.4)], 24), T(0, 0, z))  # (a dark band round the flange)
    return m.build()


def rotor_blades(H, rotor):
    """the rotor (parts of the spinning obstacle; its frame: the hub's middle at the origin): one hub - a bevelled
    disc with a dome - and four tapered aerofoil blades running into it through grip sleeves, yellow tips"""
    m = Model(H.tag("RotorParts"), H.coll, H.n)
    m.add("DarkMetal", lathe([(0.0, -1.6), (5.6, -1.6), (6.2, -1.0), (6.2, 1.0), (5.0, 1.6), (3.0, 2.6), (0.0, 3.0)], 24))
    for k in range(4):
        r = RZ(90 * k)
        root = [(-1.8, 0.0), (-1.0, 0.45), (0.8, 0.55), (1.8, 0.0), (0.8, -0.3), (-1.0, -0.3), (-1.8, 0.0)]
        tip = [(x * 1.35, y * 1.1) for x, y in root]
        # the blade: a sweep that widens from the root to its tip
        bm = bmesh.new()
        rings = []
        for i, sx in enumerate((4.0, 20.0, 52.0)):
            prof = root if i == 0 else tip
            rings.append([bm.verts.new((sx, py, pz)) for py, pz in prof[:-1]])
        n = len(rings[0])
        for a, b in zip(rings, rings[1:]):
            for j in range(n):
                bm.faces.new((a[j], a[(j + 1) % n], b[(j + 1) % n], b[j]))
        bm.faces.new(list(reversed(rings[0])))
        bm.faces.new(rings[-1])
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
        m.add("OliveMetal", bm, r)
        m.add("YellowMetal", box(5.0, 4.8, 1.0, 0.3), r @ T(55.0, 0, 0.1))
        m.add("DarkMetal", cyl(1.4, 9.0, 12, bevel=0.3), r @ T(4.0, 0, 0) @ RY(90))
    parts = m.build(role="opart")
    return attach(parts, rotor)


def sign(hole, coll, x, y, z, facing):
    """a hole sign: a board with an arched top, a frame, on a cast iron post with a finial; the atlas number on both
    sides. facing: the direction the face looks (maths angle, radians)."""
    m = Model(f"Hole{hole:02d}_SignPost", coll, hole)
    rot = RZ(math.degrees(facing) - 90.0)
    base = T(x, y, z) @ rot
    # a cast iron post under the board (not across its face), a collar where it meets it
    m.add("DarkMetal", lathe([(2.4, 0.0), (2.4, 1.0), (1.4, 2.0), (1.0, 3.0), (0.8, 12.0), (1.6, 12.6), (1.6, 13.4)], 10), base)
    # the board: 22 x 22 with an arched top, framed, a finial on the arch
    s = 22.0
    arch = [(-s / 2, 0.0), (s / 2, 0.0)] + [(s / 2 * math.cos(a), s + 3.0 * math.sin(a)) for a in [math.pi * k / 10 for k in range(11)]]
    board = prism([(px, pz) for px, pz in arch], -0.9, 0.9, 0.25)
    m.add("DarkWood", board, base @ T(0, 0, 13.0) @ RX(90))
    frame = [(p[0], p[1]) for p in arch] + [arch[0]]
    m.add("Chrome", sweep([(px, 0.0, pz) for px, pz in frame], [(-1.1, -1.1), (1.1, -1.1), (1.1, 1.1), (-1.1, 1.1), (-1.1, -1.1)], up=(0, 1, 0)), base @ T(0, 0, 13.0))
    m.add("DarkMetal", sphere(1.3, 8, 6), base @ T(0, 0, 13.0 + s + 4.3))
    for o in m.build():
        o["mg_nocol"] = 1
    # the face itself: the atlas tile, both sides (UVs by hand)
    i = hole - 1
    u0, v0 = (i % 6) / 6.0, 1.0 - (i // 6 + 1) / 3.0
    bm = bmesh.new()
    uv = bm.loops.layers.uv.verify()
    w = s - 2.0
    for side in (1, -1):
        vs = []
        for cx, cz in ((-w / 2, 0), (w / 2, 0), (w / 2, w), (-w / 2, w)):
            vs.append(bm.verts.new((cx * side, -side * 1.35, cz)))  # (well clear of the board: no flicker)
        f = bm.faces.new(vs)
        for loop, (cu, cv) in zip(f.loops, ((0, 0), (1, 0), (1, 1), (0, 1))):
            loop[uv].uv = (u0 + cu / 6.0, v0 + cv / 3.0)
    bm.transform(base @ T(0, 0, 14.0))
    B.new_object(f"Hole{hole:02d}_Sign", bm, coll, "Signs", mg_role="visual", mg_hole=hole, mg_nocol=1)


def tee_mat(H, x, y, z, a):
    """a rubber tee mat on the carpet"""
    m = Model(H.tag("Tee"), H.coll, H.n)
    mm = T(x, y, z) @ RZ(math.degrees(a))
    m.add("Rubber", box(16.0, 13.0, 0.5, 0.2), mm @ T(0, 0, 0.1))
    return m.build()


def awning(H, length, width, ztop, floor):
    """Hole 11: a corrugated roof over the chute on four steel posts, a sign board on its front"""
    m = Model(H.tag("Chute"), H.coll, H.n)
    m.add("Corrugated", box(length, width, 1.2, 0.3), T(0, 0, ztop) @ RY(-6))
    for x in (-length / 2 + 3, length / 2 - 3):
        for y in (-width / 2 + 2, width / 2 - 2):
            zt = ztop - math.tan(math.radians(6)) * x
            m.add("DarkMetal", box(1.8, 1.8, zt - floor), T(x, y, floor + (zt - floor) / 2))
    m.add("YellowMetal", box(1.2, width * 0.7, 7.0, 0.3), T(-length / 2 - 1, 0, ztop + 5) @ RY(-6))
    m.add("Neon", text("THE DROP", 4.2, 0.6, "ArchivoBlack-Regular.ttf"), T(-length / 2 - 1.8, 0, ztop + 2.6) @ RZ(-90))
    return m.build()
