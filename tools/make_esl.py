#!/usr/bin/env python3
"""Writes fo4minigolf.esl: the mini golf course, buildable in settlements under Furniture > Mini Golf.

    python tools/make_esl.py [out.esl]

Master: Fallout4.esm. Record layouts follow Amusement Games' and Wasteland Wagers' tools/make_esl.py.

Form IDs (src/Forms.h expects these):
    xx000800  KYWD fo4minigolfWorkshopMenu  the workshop menu's Mini Golf category: the plugin puts it in the Furniture
                                            menu (after Tables) when the game's data is loaded
    xx000801  ACTI fo4minigolfCourse        Mini Golf Course: Meshes\\fo4minigolf\\Course.nif (the 18 holes, the moving
                                            obstacles as named nodes, Havok collision to walk on). No script:
                                            activating it is what the plugin listens for. It makes its settlement
                                            happier, as a bar does.
    xx000802  COBJ workshop_co_fo4minigolfCourse -> xx000801
    xx000803  STAT fo4minigolfBall          the ball the plugin moves (Meshes\\fo4minigolf\\Ball.nif)
    xx000804  STAT fo4minigolfPutter        the putter behind the ball as you aim
    xx000805  STAT fo4minigolfDot           a guide dot: the aiming line is a row of them
"""

import struct
import sys

RECORD_VERSION = 131  # Fallout 4
FLAG_MASTER = 0x1
FLAG_LIGHT = 0x200
FIRST_ID = 0x800
OWN = 0x01000000  # our records come after the one master

# components (Fallout4.esm)
WOOD, STEEL, SCREWS = 0x0001FAC2, 0x0001FABD, 0x0003D294  # (the ids Amusement Games' recipes use)
WORKBENCH_FURNITURE = 0x0005B5E3  # WorkshopWorkbenchTypeFurniture (the settlement workbench)
PICKUP_SOUNDS = (0x000196B0, 0x000196BA)  # metal pick up / put down (as Amusement Games' token stand)
WORKSHOP_BONUS_HAPPINESS = 0x0012722C  # AVIF WorkshopRatingBonusHappiness
WORKSHOP_RESOURCE_OBJECT = 0x00129A8C  # AVIF WorkshopResourceObject
HAPPINESS = 10.0

COURSE_BOUNDS = (-600, -600, -60, 600, 600, 130)  # the course's extent (game units; the lawn is about 16.5 m across)
BALL_BOUNDS = (-2, -2, -2, 2, 2, 2)
PUTTER_BOUNDS = (-5, -14, 0, 5, 3, 64)
DOT_BOUNDS = (-1, -1, 0, 1, 1, 1)


def subrecord(sig: bytes, data: bytes) -> bytes:
    if len(data) > 0xFFFF:
        raise ValueError(f"{sig!r} too long")
    return sig + struct.pack("<H", len(data)) + data


def zstring(text: str) -> bytes:
    return text.encode("cp1252") + b"\0"


def record(sig: bytes, flags: int, form_id: int, body: bytes) -> bytes:
    # type, data size, flags, form id, version control info, form version, unknown
    return sig + struct.pack("<IIIIHH", len(body), flags, form_id, 0, RECORD_VERSION, 0) + body


def group(label: bytes, contents: bytes) -> bytes:
    # type, group size (header included), label, group type 0 (top), timestamp, vc info, unknown
    return b"GRUP" + struct.pack("<I", 24 + len(contents)) + label + struct.pack("<iHHI", 0, 0, 0, 0) + contents


def menu_keyword(form_id: int, editor_id: str, name: str) -> bytes:
    """a workshop menu category (a recipe filter, as Fallout4.esm's WorkshopRecipeFilterFurniture02Tables)"""
    return record(
        b"KYWD", 0, form_id,
        subrecord(b"EDID", zstring(editor_id)) + subrecord(b"CNAM", bytes.fromhex("ffffff00"))
        + subrecord(b"TNAM", struct.pack("<I", 9))  # type 9: recipe filter
        + subrecord(b"FULL", zstring(name)),
    )


def workshop_recipe(form_id: int, editor_id: str, created: int, components: list[tuple[int, int]], category: int) -> bytes:
    return record(
        b"COBJ", 0, form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"YNAM", struct.pack("<I", PICKUP_SOUNDS[0]))
        + subrecord(b"ZNAM", struct.pack("<I", PICKUP_SOUNDS[1]))
        + subrecord(b"FVPA", b"".join(struct.pack("<II", c, n) for c, n in components))
        + subrecord(b"DESC", zstring(""))
        + subrecord(b"CNAM", struct.pack("<I", created))
        + subrecord(b"BNAM", struct.pack("<I", WORKBENCH_FURNITURE))
        + subrecord(b"FNAM", struct.pack("<I", category))
        + subrecord(b"INTV", struct.pack("<HH", 1, 10)),
    )


def activator(form_id: int, editor_id: str, name: str, mesh: str, bounds: tuple, prompt: str) -> bytes:
    """an activator with no script (activating it is what the plugin listens for); its settlement happier for it"""
    return record(
        b"ACTI", 0, form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"OBND", struct.pack("<6h", *bounds))
        + subrecord(b"FULL", zstring(name))
        + subrecord(b"MODL", zstring(mesh))
        + subrecord(b"PRPS", struct.pack("<IfIf", WORKSHOP_BONUS_HAPPINESS, HAPPINESS, WORKSHOP_RESOURCE_OBJECT, 1.0))
        + subrecord(b"ATTX", zstring(prompt))
        + subrecord(b"FNAM", struct.pack("<H", 0)),
    )


def stat(form_id: int, editor_id: str, mesh: str, bounds: tuple) -> bytes:
    """a piece the plugin places and moves (laid out as Fallout4.esm's statics)"""
    return record(
        b"STAT", 0, form_id,
        subrecord(b"EDID", zstring(editor_id))
        + subrecord(b"OBND", struct.pack("<6h", *bounds))
        + subrecord(b"MODL", zstring(mesh))
        + subrecord(b"DNAM", struct.pack("<fIff", 90.0, 0, 1.0, 1.0)),
    )


def build() -> bytes:
    menu = OWN | FIRST_ID
    course = OWN | (FIRST_ID + 1)
    keywords = [menu_keyword(menu, "fo4minigolfWorkshopMenu", "Mini Golf")]
    acti = [activator(course, "fo4minigolfCourse", "Mini Golf Course", "fo4minigolf\\Course.nif", COURSE_BOUNDS, "Play")]
    recipes = [workshop_recipe(OWN | (FIRST_ID + 2), "workshop_co_fo4minigolfCourse", course,
                               [(WOOD, 60), (STEEL, 30), (SCREWS, 6)], menu)]
    stats = [stat(OWN | (FIRST_ID + 3), "fo4minigolfBall", "fo4minigolf\\Ball.nif", BALL_BOUNDS),
             stat(OWN | (FIRST_ID + 4), "fo4minigolfPutter", "fo4minigolf\\Putter.nif", PUTTER_BOUNDS),
             stat(OWN | (FIRST_ID + 5), "fo4minigolfDot", "fo4minigolf\\Dot.nif", DOT_BOUNDS)]
    count = len(keywords) + len(acti) + len(recipes) + len(stats)
    header = record(
        b"TES4",
        FLAG_MASTER | FLAG_LIGHT,
        0,
        subrecord(b"HEDR", struct.pack("<fiI", 1.0, count, FIRST_ID + 6))
        + subrecord(b"CNAM", zstring("evodube"))
        + subrecord(b"SNAM", zstring("Mini golf: an 18-hole course for your settlements"))
        + subrecord(b"MAST", zstring("Fallout4.esm"))
        + subrecord(b"DATA", struct.pack("<Q", 0)),
    )
    # groups in the order the game's own master has them
    return (header + group(b"KYWD", b"".join(keywords)) + group(b"ACTI", b"".join(acti))
            + group(b"STAT", b"".join(stats)) + group(b"COBJ", b"".join(recipes)))


def main() -> int:
    out = sys.argv[1] if len(sys.argv) > 1 else "fo4minigolf.esl"
    data = build()
    with open(out, "wb") as f:
        f.write(data)
    print(f"wrote {out} ({len(data)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
