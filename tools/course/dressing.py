"""Dresses the grounds round the holes with the game's own props, as a real park has them: clipped
shrubs, roses and rocks in the open lawn between the holes.

The props' meshes are read from the game's archives (ba2.py, into <root>/vanilla) and imported with PyNifly once, into a
hidden library collection; each placed prop is a copy of those meshes, keeping their materials (the game's .bgsm
material files, so they look in game exactly as they do anywhere else). export_course.py merges them into the
course's mesh like everything else; they have no collision of their own (mg_nocol).

    import dressing; dressing.dress(root, pads, lawn)       # called by build_course.build
"""

import math
import os
import random

import bpy
from mathutils import Matrix, Vector

import ba2
import build_course as B

GAME_DATA = r"G:\Program Files\Steam\steamapps\common\Fallout 4\Data"

PROPS = {
    "boxwood_r": "meshes/landscape/plants/boxwoodrnd01.nif",
    "boxwood_s": "meshes/landscape/plants/boxwoodstr01.nif",
    "boxwood_t": "meshes/landscape/plants/boxwoodtall01.nif",
    "holly": "meshes/landscape/plants/hollyshrubsmall01trimmed.nif",
    "rose": "meshes/landscape/plants/rosebush01.nif",
    "mulch": "meshes/landscape/plants/mulchmoundround01.nif",
    "boulder": "meshes/landscape/rocks/rockboulder01.nif",
    "rock": "meshes/landscape/rocks/riverrock1.nif",
}


def _ctx():
    win = bpy.context.window_manager.windows[0]
    area = next(a for a in win.screen.areas if a.type == "VIEW_3D")
    region = next(r for r in area.regions if r.type == "WINDOW")
    return bpy.context.temp_override(window=win, area=area, region=region)


def library(root):
    """imports each prop once: {key: [(mesh object, its matrix relative to the prop's root)]}"""
    lib = B.collection("DressingLibrary")
    lib.hide_render = True
    lib.hide_viewport = True
    data = GAME_DATA if os.path.isdir(GAME_DATA) else None
    missing = [p for p in PROPS.values() if not os.path.exists(os.path.join(root, "vanilla", *p.split("/")))]
    if missing and data:
        ba2.extract(data, missing, os.path.join(root, "vanilla"))
    out = {}
    for key, rel in PROPS.items():
        path = os.path.join(root, "vanilla", *rel.split("/"))
        if not os.path.exists(path):
            continue
        before = set(bpy.data.objects)
        with _ctx():
            bpy.ops.import_scene.pynifly(filepath=path)
        new = [o for o in bpy.data.objects if o not in before]
        bpy.context.view_layer.update()
        parts = []
        for o in new:
            for c in list(o.users_collection):
                c.objects.unlink(o)
            lib.objects.link(o)
            # the visible shapes only: not the collision, not the markers
            if (o.type == "MESH" and o.data.materials and not o.name.startswith("bhk") and o.data.polygons
                    and "EditorMarker" not in o.name):
                parts.append((o, o.matrix_world.copy()))
        out[key] = parts
    # for the previews only: the game's colour maps on the imported materials (the game itself uses its own .bgsm)
    want = {}
    for parts in out.values():
        for o, _ in parts:
            for m in o.data.materials:
                d = str(m.get("BSShaderTextureSet_Diffuse", "")).replace("\\", "/").lower()
                if d.startswith("textures/") and m.node_tree and "Diffuse_Texture" in m.node_tree.nodes:
                    want[m.name] = d
    files = sorted(set(want.values()))
    if data:
        ba2.extract(data, [f for f in files if not os.path.exists(os.path.join(root, "vanilla", *f.split("/")))],
                    os.path.join(root, "vanilla"))
    for name, d in want.items():
        path = os.path.join(root, "vanilla", *d.split("/"))
        if os.path.exists(path):
            node = bpy.data.materials[name].node_tree.nodes["Diffuse_Texture"]
            node.image = bpy.data.images.load(path, check_existing=True)
    return out


_ONE = {}


def library_one(root, rel):
    """one prop's meshes, imported once per build (as library() does): [(mesh object, its matrix in the prop)]"""
    parts = _ONE.get(rel)
    if parts:
        try:
            parts[0][0].name  # (still there: a new build wipes the scene)
            return parts
        except ReferenceError:
            pass
    path = os.path.join(root, "vanilla", *rel.split("/"))
    if not os.path.exists(path) and os.path.isdir(GAME_DATA):
        ba2.extract(GAME_DATA, [rel], os.path.join(root, "vanilla"))
    if not os.path.exists(path):
        return []
    lib = B.collection("DressingLibrary")
    lib.hide_render = True
    lib.hide_viewport = True
    before = set(bpy.data.objects)
    with _ctx():
        bpy.ops.import_scene.pynifly(filepath=path)
    new = [o for o in bpy.data.objects if o not in before]
    bpy.context.view_layer.update()
    parts = []
    for o in new:
        for c in list(o.users_collection):
            c.objects.unlink(o)
        lib.objects.link(o)
        if (o.type == "MESH" and o.data.materials and not o.name.startswith("bhk") and o.data.polygons
                and "EditorMarker" not in o.name):
            parts.append((o, o.matrix_world.copy()))
    _preview_textures(root, parts)
    _ONE[rel] = parts
    return parts


def _preview_textures(root, parts):
    for o, _ in parts:
        for m in o.data.materials:
            d = str(m.get("BSShaderTextureSet_Diffuse", "")).replace("\\", "/").lower()
            if not (d.startswith("textures/") and m.node_tree and "Diffuse_Texture" in m.node_tree.nodes):
                continue
            path = os.path.join(root, "vanilla", *d.split("/"))
            if not os.path.exists(path) and os.path.isdir(GAME_DATA):
                ba2.extract(GAME_DATA, [d], os.path.join(root, "vanilla"))
            if os.path.exists(path):
                m.node_tree.nodes["Diffuse_Texture"].image = bpy.data.images.load(path, check_existing=True)


def place(lib, key, coll, name, x, y, z, deg=0.0, scale=1.0):
    """a copy of a library prop, its meshes sharing the library's data"""
    obs = []
    m = Matrix.Translation((x, y, z)) @ Matrix.Rotation(math.radians(deg), 4, "Z") @ Matrix.Scale(scale, 4)
    for i, (src, rel) in enumerate(lib.get(key, [])):
        ob = bpy.data.objects.new(f"{name}_{i}", src.data)
        coll.objects.link(ob)
        ob.matrix_basis = m @ rel
        ob["mg_role"] = "visual"
        ob["mg_hole"] = 0
        ob["mg_nocol"] = 1
        ob["mg_vanilla"] = 1
        obs.append(ob)
    return obs


# ---------------------------------------------------------------- where things can go


def _seg_dist(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / ab.length_squared)) if ab.length_squared > 0 else 0.0
    return (p - (a + ab * t)).length


def edge_distance(p, poly):
    p = Vector(p)
    n = len(poly)
    return min(_seg_dist(p, Vector(poly[i]), Vector(poly[(i + 1) % n])) for i in range(n))


def size(lib, key, scale=1.0):
    """a prop's radius on the ground (half its larger width), scaled"""
    xs, ys = [], []
    for ob, rel in lib.get(key, []):
        for c in ob.bound_box:
            q = rel @ Vector(c)
            xs.append(q.x)
            ys.append(q.y)
    if not xs:
        return 0.0
    return max(max(xs) - min(xs), max(ys) - min(ys)) / 2 * scale


def clearance(p, polys):
    """how far (x, y) is from the nearest of the polygons (0 inside one)"""
    p = Vector(p)
    best = 1e9
    for poly in polys:
        if B.inside(tuple(p), poly):
            return 0.0
        n = len(poly)
        for i in range(n):
            best = min(best, _seg_dist(p, Vector(poly[i]), Vector(poly[(i + 1) % n])))
    return best


def dress(root, pads, lawn, tees):
    """pads: each hole's paved pad (world polygons); lawn: the lawn's outline; tees: {hole: ((x, y), heading)}"""
    lib = library(root)
    coll = B.collection("Dressing", bpy.data.collections["MiniGolf"])
    rnd = random.Random(1952)
    z = B.DECK - 0.5
    taken = []  # (x, y, r) of what's been put down

    def free(x, y, r, margin=6.0):
        if not B.inside((x, y), lawn):
            return False
        if clearance((x, y), pads) < r + margin:
            return False
        # inside the lawn by r too
        if edge_distance((x, y), lawn) < r + 10.0:
            return False
        return all(math.hypot(x - a, y - b) > r + c + 4.0 for a, b, c in taken)

    def put(key, x, y, deg, r, scale=1.0, name=None):
        place(lib, key, coll, name or f"Dress_{key}{len(taken):03d}", x, y, z, deg, scale)
        taken.append((x, y, r))

    xs = [p[0] for p in lawn]
    ys = [p[1] for p in lawn]
    # planting: clipped boxwoods, holly and roses hugging the pads' edges (a park's borders),
    # flower beds of mulch, river rocks and a few boulders out in the open
    kinds = ["boxwood_r", "boxwood_s", "boxwood_t", "holly", "rose", "mulch", "rock", "boulder"]
    weights = [5, 3, 3, 4, 5, 2, 3, 1]
    plants = 0
    for _ in range(12000):
        if plants >= 80:
            break
        x, y = rnd.uniform(min(xs), max(xs)), rnd.uniform(min(ys), max(ys))
        key = rnd.choices(kinds, weights)[0]
        if key not in lib:
            continue
        scale = {"rock": rnd.uniform(0.8, 1.6), "boulder": rnd.uniform(0.25, 0.45)}.get(key, rnd.uniform(0.8, 1.1))
        r = size(lib, key, scale)
        if r <= 0 or r > 40:  # nothing big enough to crowd the holes
            continue
        room = clearance((x, y), pads)
        if room > r + 50.0 and key not in ("boulder", "mulch"):
            continue  # the planting sits near the holes, not out in the open
        if free(x, y, r, 3.0):
            put(key, x, y, rnd.uniform(0, 360), r, scale)
            plants += 1
    return {"placed": len(taken), "library": {k: len(v) for k, v in lib.items()}}
