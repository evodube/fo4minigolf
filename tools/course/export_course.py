"""Exports the course built by build_course.py (run inside Blender with PyNifly):

    <root>/Data/F4SE/Plugins/fo4minigolf/course.txt     the physics: ground, walls, obstacles (see src/sim/Course.h)
    <root>/Data/Meshes/fo4minigolf/Course.nif           everything that stands still, one shape per texture, with
                                                        Havok collision (a compressed mesh) to walk on
                                                        and the moving obstacles as named nodes in it
    <root>/Data/Meshes/fo4minigolf/Ball.nif, Putter.nif, Dot.nif (a guide dot)

Everything is read from the scene, so moving something in Blender and exporting again moves it in the game and in the
physics alike.
"""

import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_course as B  # noqa: E402
from holes import HOLES  # noqa: E402


def _f(v):
    return f"{v:.2f}".rstrip("0").rstrip(".") if abs(v) >= 0.005 else "0"


def world_mesh(ob):
    """the object's mesh with modifiers applied, in world space, as a new bmesh"""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = bpy.data.meshes.new_from_object(ev)
    me.transform(ob.matrix_world)
    bm = bmesh.new()
    bm.from_mesh(me)
    bpy.data.meshes.remove(me)
    return bm


def hole_objects(num):
    return list(bpy.data.collections[f"Hole{num:02d}"].objects)


def wall_lines(ob, prefix="wall"):
    bm = world_mesh(ob)
    bm.normal_update()
    rest = float(ob.get("mg_rest", 0.6))
    out = []
    for f in bm.faces:
        n = f.normal
        if abs(n.z) > 0.25 or f.calc_area() < 1e-3:
            continue
        t = Vector((n.y, -n.x))
        if t.length < 1e-6:
            continue
        t.normalize()
        c = f.calc_center_median().xy
        s = [(v.co.xy - c).dot(t) for v in f.verts]
        zs = [v.co.z for v in f.verts]
        a = c + t * min(s)
        b = c + t * max(s)
        if (b - a).length < 0.05:
            continue
        out.append(f"{prefix} {_f(a.x)} {_f(a.y)} {_f(b.x)} {_f(b.y)} {_f(min(zs))} {_f(max(zs))} {rest:g}")
    bm.free()
    return out


def ground_lines(ob):
    bm = world_mesh(ob)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.normal_update()
    surface = ob.get("mg_surface", "green")
    out = []
    zs = []
    for f in bm.faces:
        if f.normal.z < 0.05:
            continue
        vs = [v.co for v in f.verts]
        zs += [v.z for v in vs]
        out.append(f"tri {surface} " + " ".join(f"{_f(v.x)} {_f(v.y)} {_f(v.z)}" for v in vs))
    xs = [v.co.x for v in bm.verts]
    ys = [v.co.y for v in bm.verts]
    box = (min(xs), min(ys), max(xs), max(ys), min(zs) if zs else 0.0)
    bm.free()
    return out, box


def obstacle_lines(ob, turn):
    m = ob.get("mg_motion")
    loc = ob.matrix_world.translation
    period = float(ob.get("mg_period", 4.0))
    phase = float(ob.get("mg_phase", 0.0))
    deg = math.degrees(turn)
    name = ob.name
    if m == "spin":
        head = f"obstacle {name} spin {_f(loc.x)} {_f(loc.y)} {period:g} {phase:g}"
    elif m == "swing":
        head = f"obstacle {name} swing {_f(loc.x)} {_f(loc.y)} {float(ob['mg_swing']):g} {period:g} {phase:g}"
    elif m == "slide":
        d = Matrix.Rotation(turn, 2) @ Vector((float(ob.get("mg_dx", 0)), float(ob.get("mg_dy", 0))))
        head = f"obstacle {name} slide {_f(d.x)} {_f(d.y)} {period:g} {phase:g}"
    elif m == "wheel":
        head = (f"obstacle {name} wheel {_f(loc.x)} {_f(loc.y)} {_f(loc.z)} {_f(float(ob.get('mg_axis', 0)) + deg)} {period:g} "
                f"{phase:g} {int(ob.get('mg_blades', 4))} {float(ob.get('mg_half', 15)):g}")
    elif m == "pendulum":
        head = (f"obstacle {name} pendulum {_f(loc.x)} {_f(loc.y)} {_f(loc.z)} {_f(float(ob.get('mg_axis', 0)) + deg)} "
                f"{float(ob['mg_length']):g} {float(ob['mg_swing']):g} {period:g} {phase:g}")
    elif m == "lift":
        head = f"obstacle {name} lift {float(ob['mg_height']):g} {period:g} {phase:g}"
    else:
        raise ValueError(f"{name}: unknown motion {m}")
    lines = [head, f"ovisual {_f(loc.x)} {_f(loc.y)} {_f(loc.z)} {_f(deg)}"]
    for w in bpy.data.objects:
        if w.get("mg_role") == "owall" and w.get("mg_obstacle") == name:
            lines += wall_lines(w, "owall")
    return lines


def course_text():
    lines = ["course 1", "# made by tools/course/export_course.py from the Blender scene: don't edit by hand"]
    for num, par, name, _theme in HOLES:
        objs = hole_objects(num)
        tee = bpy.data.objects[f"Hole{num:02d}_Tee"]
        cup = bpy.data.objects[f"Hole{num:02d}_Cup"]
        turn = float(tee.get("mg_turn", 0.0)) + float(tee.get("mg_aim", 0.0))
        yaw = (90.0 - math.degrees(turn)) % 360.0  # the game's heading: clockwise from +y
        t, c = tee.matrix_world.translation, cup.matrix_world.translation
        lines += ["", f"hole {num} {par} {name}", f"tee {_f(t.x)} {_f(t.y)} {_f(t.z)} {_f(yaw)}",
                  f"cup {_f(c.x)} {_f(c.y)} {_f(c.z)} {B.CUP_R:g}"]
        body = []
        boxes = []
        for ob in objs:
            role = ob.get("mg_role")
            if ob.type != "MESH":
                continue
            if role == "ground":
                tris, box = ground_lines(ob)
                body += tris
                boxes.append(box)
            elif role == "wall":
                body += wall_lines(ob)
        for ob in objs:
            if ob.get("mg_role") == "obstacle":
                body += obstacle_lines(ob, float(ob.get("mg_turn", 0.0)) + float(ob.get("mg_yaw", 0.0)))
        x0 = min(b[0] for b in boxes) - 15
        y0 = min(b[1] for b in boxes) - 15
        x1 = max(b[2] for b in boxes) + 15
        y1 = max(b[3] for b in boxes) + 15
        z0 = min(b[4] for b in boxes) - 25
        lines.append(f"bounds {_f(x0)} {_f(y0)} {_f(x1)} {_f(y1)} {_f(z0)}")
        lines += body
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------- nifs


def _ctx():
    win = bpy.context.window_manager.windows[0]
    area = next(a for a in win.screen.areas if a.type == "VIEW_3D")
    region = next(r for r in area.regions if r.type == "WINDOW")
    return bpy.context.temp_override(window=win, area=area, region=region)


def _export(objs, root, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = root
    with _ctx():
        bpy.ops.export_scene.pynifly(filepath=path, target_game="FO4", intuit_defaults=False, preserve_hierarchy=True)


def _scratch():
    if "Export" in bpy.data.collections:
        c = bpy.data.collections["Export"]
        for o in list(c.objects):
            bpy.data.objects.remove(o)
    else:
        c = bpy.data.collections.new("Export")
        bpy.context.scene.collection.children.link(c)
    return c


def _root(coll, name, collision=None):
    r = bpy.data.objects.new(name, None)
    coll.objects.link(r)
    r["pynRoot"] = True
    if collision is not None:
        r["pynCollisionTarget"] = collision.name
    return r


def _joined(name, coll, mat_name, parts):
    bm = bmesh.new()
    for ob in parts:
        part = world_mesh(ob)  # (with its modifiers: the greens' cup holes)
        me = bpy.data.meshes.new("tmp")
        part.to_mesh(me)
        part.free()
        bm.from_mesh(me)
        bpy.data.meshes.remove(me)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.materials.append(bpy.data.materials[mat_name])
    ob = bpy.data.objects.new(name, me)
    coll.objects.link(ob)
    return ob


def export_course_nif(root_dir):
    coll = _scratch()
    by_mat = {}
    solid = []
    for ob in bpy.data.collections["MiniGolf"].all_objects:
        if ob.type != "MESH" or ob.get("mg_hidden") or ob.get("mg_role") in ("obstacle", "owall"):
            continue
        if not ob.data.materials:
            continue
        by_mat.setdefault(ob.data.materials[0].name, []).append(ob)
        # walk-on collision: the deck, the platforms, the greens and everything big; not the little props
        dims = ob.dimensions
        if ob.get("mg_role") in ("ground", "wall") or ob.name.startswith(("Deck", "Platform")) or max(dims) > 30:
            if ob.get("mg_surface") != "water":
                solid.append(ob)
    shapes = [_joined(f"Course_{m}", coll, m, parts) for m, parts in sorted(by_mat.items())]

    bm = bmesh.new()
    for ob in solid:
        part = world_mesh(ob)
        me = bpy.data.meshes.new("tmp")
        part.to_mesh(me)
        part.free()
        bm.from_mesh(me)
        bpy.data.meshes.remove(me)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=0.1)
    me = bpy.data.meshes.new("CourseCollision")
    bm.to_mesh(me)
    bm.free()
    col = bpy.data.objects.new("bhkPhysicsSystem_Course", me)
    coll.objects.link(col)
    with _ctx():
        bpy.context.view_layer.objects.active = col
        col.select_set(True)
        bpy.ops.rigidbody.object_add()
    col.rigid_body.type = "PASSIVE"
    col.rigid_body.collision_shape = "MESH"
    col["pynRigidBody"] = "bhkPhysicsSystem"
    col["pynCollisionShapeType"] = "compressed_mesh"
    col["pynCollisionFlags"] = "SYNC_ON_UPDATE"  # 128, as vanilla statics' bhkNPCollisionObject

    # the moving obstacles: each its own node (its name the obstacle's, as course.txt has it), placed as at time 0,
    # its mesh under it; the plugin turns, slides or lifts the node (src/Golf.cpp)
    nodes = []
    for ob in bpy.data.collections["MiniGolf"].all_objects:
        if ob.get("mg_role") != "obstacle":
            continue
        node = bpy.data.objects.new(ob.name, None)
        coll.objects.link(node)
        node.matrix_world = ob.matrix_world.copy()
        me = bpy.data.meshes.new_from_object(ob.evaluated_get(bpy.context.evaluated_depsgraph_get()))
        mesh = bpy.data.objects.new(ob.name + "Mesh", me)
        coll.objects.link(mesh)
        mesh.parent = node
        nodes += [node, mesh]

    root = _root(coll, "MiniGolfCourse", col)
    for s in shapes:
        s.parent = root
    for n in nodes:
        if n.type == "EMPTY":
            n.parent = root
    bsx = bpy.data.objects.new("BSXFlags", None)
    coll.objects.link(bsx)
    bsx.parent = root
    bsx["BSXFlags_Name"] = "BSX"
    # Havok on (PyNifly spells it HAVOC: an unknown name is dropped without a word, and without the bit the game
    # ignores the collision: nothing to stand on, nothing to select in workshop mode), articulated as vanilla
    # statics are (shacks' floors: 162, these two and their editor marker's)
    bsx["BSXFlags_Value"] = "HAVOC | ARTICULATED"
    path = os.path.join(root_dir, "Data", "Meshes", "fo4minigolf", "Course.nif")
    _export([root, bsx, col] + shapes + nodes, root, path)
    return path, len(shapes), len(me.polygons)


def export_single(root_dir, ob, rel):
    """one object as its own nif: its mesh in its own space (origin = the object's origin, no turn)"""
    coll = _scratch()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    copy = bpy.data.objects.new(ob.name.split("_")[-1], me)
    coll.objects.link(copy)
    root = _root(coll, ob.name)
    copy.parent = root
    path = os.path.join(root_dir, "Data", "Meshes", "fo4minigolf", *rel.split("/"))
    _export([root, copy], root, path)
    return path


def make_ball_and_putter():
    coll = B.collection("Kit", bpy.data.collections["MiniGolf"])
    if "Ball" not in bpy.data.objects:
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=20, v_segments=12, radius=1.5)
        B.finish(bm, "WhiteWood")
        B.new_object("Ball", bm, coll, "WhiteWood", mg_role="kit", mg_hidden=1)
    if "Putter" not in bpy.data.objects:
        # origin at the middle of the putter's face; the shaft rises behind it, the face looks along +y
        bm = bmesh.new()
        head = bmesh.ops.create_cube(bm, size=1.0)["verts"]
        bmesh.ops.scale(bm, vec=(8.0, 2.2, 2.4), verts=head)
        bmesh.ops.translate(bm, vec=(0, -1.1, 1.2), verts=head)
        shaft = bmesh.ops.create_cone(bm, cap_ends=True, segments=8, radius1=0.35, radius2=0.35, depth=62)["verts"]
        bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(-18), 3, "X"), verts=shaft)
        bmesh.ops.translate(bm, vec=(-2.5, -1.1 - 31 * math.sin(math.radians(18)), 2.4 + 31 * math.cos(math.radians(18))), verts=shaft)
        grip = bmesh.ops.create_cone(bm, cap_ends=True, segments=8, radius1=0.7, radius2=0.7, depth=16)["verts"]
        bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=Matrix.Rotation(math.radians(-18), 3, "X"), verts=grip)
        bmesh.ops.translate(bm, vec=(-2.5, -1.1 - 56 * math.sin(math.radians(18)), 2.4 + 56 * math.cos(math.radians(18))), verts=grip)
        B.finish(bm, "Corrugated")
        B.new_object("Putter", bm, coll, "Corrugated", mg_role="kit", mg_hidden=1)
    if "Dot" not in bpy.data.objects:
        # a guide dot: a flat disc lying on the green (the aiming line is a row of them)
        bm = bmesh.new()
        bmesh.ops.create_circle(bm, cap_ends=True, segments=10, radius=0.7)
        bmesh.ops.translate(bm, vec=(0, 0, 0.15), verts=bm.verts)
        B.finish(bm, "WhiteWood")
        B.new_object("Dot", bm, coll, "WhiteWood", mg_role="kit", mg_hidden=1)
    return bpy.data.objects["Ball"], bpy.data.objects["Putter"]


def export(root_dir):
    B._root = root_dir
    bpy.context.view_layer.update()  # matrix_world is only current after an update
    text = course_text()
    path = os.path.join(root_dir, "Data", "F4SE", "Plugins", "fo4minigolf", "course.txt")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        f.write(text)
    out = {"course.txt": len(text)}
    out["Course.nif"] = export_course_nif(root_dir)
    ball, putter = make_ball_and_putter()
    out["Ball"] = export_single(root_dir, ball, "Ball.nif")
    out["Putter"] = export_single(root_dir, putter, "Putter.nif")
    out["Dot"] = export_single(root_dir, bpy.data.objects["Dot"], "Dot.nif")
    _scratch()
    return out
