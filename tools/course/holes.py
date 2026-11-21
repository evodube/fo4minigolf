"""The 18 holes: number, par, name, theme. Shared by make_textures.py (the signs) and build_course.py (Blender)."""

# theme: which look the hole gets (carpet, rails and trim)
#   junk      scrap metal rails, green carpet: built by settlers out of what was lying round
#   americana white-painted wood, red or blue carpet: a pre-war putt-putt, like a Nuka-World attraction
#   vault     Vault-Tec blue and yellow, steel rails
#   military  olive drab, sandbags and hazard stripes
#   glow      the Glowing Sea: sickly green carpet, slag rails
HOLES = [
    (1, 2, "Welcome Mat", "junk"),
    (2, 3, "Old Windmill", "americana"),
    (3, 2, "Nuka Alley", "americana"),
    (4, 3, "Rust Ramp", "junk"),
    (5, 3, "Toxic Crossing", "glow"),
    (6, 3, "Vertibird", "military"),
    (7, 3, "Diner Dogleg", "americana"),
    (8, 3, "The Volcano", "junk"),
    (9, 3, "Vault Doors", "vault"),
    (10, 2, "Tire Yard", "junk"),
    (11, 2, "The Drop", "vault"),
    (12, 3, "Wrecking Ball", "junk"),
    (13, 3, "The Fork", "military"),
    (14, 3, "Glowing Sea", "glow"),
    (15, 3, "Castle Gate", "americana"),
    (16, 3, "Mr. Handy", "vault"),
    (17, 3, "Zigzag", "americana"),
    (18, 3, "Last Stop", "junk"),
]

assert len(HOLES) == 18
