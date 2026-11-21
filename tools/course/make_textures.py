#!/usr/bin/env python3
"""Makes the mini golf course's own textures: the hole signs, the mini golf carpet, and a few plain materials for the set
pieces the game has no tileable texture of (rubber, steel, glass, canvas, lamps). The rest of the course uses the game's
textures. (The other generators here, carpets, wood, metal and so on, are kept in case a texture the game doesn't
have is ever wanted; main() doesn't call them, nor the flags', gone with the flags.)

    python tools/course/make_textures.py <out folder>

Writes, for every texture, <name>_d.dds (colour), <name>_n.dds (normal, BC5) and <name>_s.dds (R specular,
G gloss, BC5), all with mipmaps, plus, with MG_PNG=1 set, a PNG of each colour map (<name>_d.png) to look at.

The signs and flags use Anton and Archivo Black (tools/course/fonts, SIL Open Font License).
"""

import io
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from holes import HOLES  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_NUM = os.path.join(HERE, "fonts", "Anton-Regular.ttf")
FONT_TEXT = os.path.join(HERE, "fonts", "ArchivoBlack-Regular.ttf")

RNG = np.random.default_rng(20261121)


# ---------------------------------------------------------------- noise


def noise(size, scale, seed=None, aspect=(1.0, 1.0)):
    """Tileable smooth noise in [0, 1]: white noise low-passed in frequency space. scale = feature size in pixels."""
    rng = np.random.default_rng(seed) if seed is not None else RNG
    w = rng.standard_normal((size, size))
    fy = np.fft.fftfreq(size)[:, None] * aspect[1]
    fx = np.fft.fftfreq(size)[None, :] * aspect[0]
    f2 = fx * fx + fy * fy
    sigma = 1.0 / max(scale, 1e-3)
    filt = np.exp(-f2 / (2 * sigma * sigma))
    out = np.real(np.fft.ifft2(np.fft.fft2(w) * filt))
    out -= out.min()
    out /= max(out.max(), 1e-9)
    return out


def fbm(size, base, octaves=5, seed=None, aspect=(1.0, 1.0)):
    rng = np.random.default_rng(seed) if seed is not None else RNG
    out = np.zeros((size, size))
    amp = 1.0
    total = 0.0
    scale = base
    for _ in range(octaves):
        out += amp * noise(size, scale, int(rng.integers(1 << 30)), aspect)
        total += amp
        amp *= 0.5
        scale *= 0.5
    return out / total


def colour(rgb):
    return np.array(rgb, dtype=np.float64) / 255.0


def mix(a, b, t):
    t = t[..., None] if np.ndim(t) == 2 else t
    return a * (1 - t) + b * t


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


# ---------------------------------------------------------------- DDS


def _dds_level(img, fmt):
    buf = io.BytesIO()
    img.save(buf, format="DDS", pixel_format=fmt)
    data = buf.getvalue()
    assert data[:4] == b"DDS "
    header_len = 128
    fourcc = data[84:88]
    if fourcc == b"DX10":
        header_len += 20
    return data[:header_len], data[header_len:]


def save_dds(img, path, fmt):
    """Saves with a full mip chain. fmt: DXT1, DXT5 or BC5."""
    img = img.convert("RGBA" if fmt == "DXT5" else "RGB")
    levels = []
    w, h = img.size
    cur = img
    while True:
        levels.append(cur)
        if w == 1 and h == 1:
            break
        w, h = max(1, w // 2), max(1, h // 2)
        cur = cur.resize((w, h), Image.LANCZOS)
    header, first = _dds_level(levels[0], fmt)
    blobs = [first]
    for lv in levels[1:]:
        if lv.size[0] < 4 or lv.size[1] < 4:
            lv = lv.resize((max(4, lv.size[0]), max(4, lv.size[1])), Image.NEAREST)
            _, d = _dds_level(lv, fmt)
            # a block-compressed level smaller than 4x4 still takes one block
        else:
            _, d = _dds_level(lv, fmt)
        blobs.append(d)
    hdr = bytearray(header)
    flags = struct.unpack_from("<I", hdr, 8)[0] | 0x20000  # DDSD_MIPMAPCOUNT
    struct.pack_into("<I", hdr, 8, flags)
    struct.pack_into("<I", hdr, 28, len(levels))
    caps = struct.unpack_from("<I", hdr, 108)[0] | 0x8 | 0x400000 | 0x1000  # COMPLEX | MIPMAP | TEXTURE
    struct.pack_into("<I", hdr, 108, caps)
    with open(path, "wb") as f:
        f.write(bytes(hdr))
        for b in blobs:
            f.write(b)


def to_img(arr):
    return Image.fromarray((np.clip(arr, 0, 1) * 255 + 0.5).astype(np.uint8))


def normal_from_height(h, strength):
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5 * strength
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5 * strength
    n = np.dstack([-dx, dy, np.ones_like(h)])
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    return n


def write_set(out, name, diffuse, height, strength, spec, gloss):
    """diffuse HxWx3 in [0,1]; height HxW; spec, gloss: scalars or HxW arrays."""
    d = to_img(diffuse)
    if os.environ.get("MG_PNG"):
        d.save(os.path.join(out, f"{name}_d.png"))
    save_dds(d, os.path.join(out, f"{name}_d.dds"), "DXT1")

    n = normal_from_height(height, strength)
    size = n.shape[0]
    rg = np.dstack([n[..., 0] * 0.5 + 0.5, n[..., 1] * 0.5 + 0.5, np.zeros((size, size))])
    save_dds(to_img(rg), os.path.join(out, f"{name}_n.dds"), "BC5")

    s = np.broadcast_to(np.asarray(spec, dtype=np.float64), height.shape)
    g = np.broadcast_to(np.asarray(gloss, dtype=np.float64), height.shape)
    sg = to_img(np.dstack([s, g, np.zeros_like(s)])).resize((256, 256), Image.LANCZOS)  # little detail: small
    save_dds(sg, os.path.join(out, f"{name}_s.dds"), "BC5")
    print("wrote", name)


# ---------------------------------------------------------------- materials

S = 1024


def carpet(out, name, rgb, wear=0.25):
    fibres = noise(S, 0.9) * 0.6 + noise(S, 2.0) * 0.4
    mottling = fbm(S, 160, 4)
    dirt = smoothstep(0.55, 0.85, fbm(S, 220, 5))
    base = colour(rgb)
    c = base[None, None, :] * (0.78 + 0.32 * fibres[..., None]) * (0.9 + 0.2 * mottling[..., None])
    c = mix(c, c * 0.55 + colour((70, 58, 40)) * 0.25, dirt * wear)
    write_set(out, name, c, fibres, 3.0, 0.04, 0.12)


def wood(out, name, painted=None):
    """horizontal planks 128 px high; painted = paint colour (with chips showing the wood)"""
    y = np.arange(S)[:, None] * np.ones((1, S))
    plank = (y // 128).astype(int)
    grain = noise(S, 40, aspect=(0.08, 1.0))
    grain = 0.5 + 0.5 * np.sin(grain * 60 + fbm(S, 64, 3) * 6)
    tint = np.random.default_rng(7).random(16)[plank % 16]
    c = mix(colour((120, 82, 48))[None, None, :], colour((168, 122, 74))[None, None, :], grain * 0.7 + tint * 0.3)
    seam = (y % 128 < 3) | (y % 128 > 125)
    height = grain * 0.4 - seam * 1.0
    spec, gloss = 0.08, 0.2
    if painted is not None:
        chips = smoothstep(0.62, 0.7, fbm(S, 90, 5))
        paint = colour(painted)[None, None, :] * (0.92 + 0.08 * fbm(S, 30, 3)[..., None])
        c = mix(paint, c, chips)
        height = height * (0.4 + 0.6 * chips) + (1 - chips) * 0.3
        spec, gloss = 0.15 * (1 - chips) + 0.05, 0.35 * (1 - chips) + 0.15
    c = np.where(seam[..., None], c * 0.35, c)
    write_set(out, name, c, height, 4.0, spec, gloss)


def scrap_metal(out, name):
    x = np.arange(S)[None, :] * np.ones((S, 1))
    corr = 0.5 + 0.5 * np.sin(x / S * 2 * math.pi * 16)
    rust = smoothstep(0.42, 0.62, fbm(S, 200, 6))
    paint_left = smoothstep(0.55, 0.6, fbm(S, 260, 5))
    metal = colour((128, 128, 124))[None, None, :] * (0.75 + 0.35 * fbm(S, 12, 4)[..., None])
    rustc = mix(colour((92, 44, 22))[None, None, :], colour((160, 84, 38))[None, None, :], fbm(S, 20, 4))
    paint = colour((84, 104, 90))[None, None, :] * (0.9 + 0.1 * noise(S, 8)[..., None])
    c = mix(metal, rustc, rust)
    c = mix(c, paint, paint_left * (1 - rust))
    c *= (0.8 + 0.2 * corr)[..., None]
    height = corr * 1.0 + rust * 0.25 * fbm(S, 6, 3)
    write_set(out, name, c, height, 6.0, 0.35 * (1 - rust) + 0.03, 0.45 * (1 - rust) + 0.1)


def painted_steel(out, name, rgb, stripes=False):
    scratches = smoothstep(0.86, 0.9, noise(S, 3, aspect=(1.0, 0.05)))
    wear = smoothstep(0.6, 0.75, fbm(S, 160, 5))
    paint = colour(rgb)[None, None, :] * (0.88 + 0.12 * fbm(S, 40, 4)[..., None])
    if stripes:
        xx, yy = np.meshgrid(np.arange(S), np.arange(S))
        band = ((xx + yy) // 90) % 2 == 0
        paint = np.where(band[..., None], paint, colour((24, 24, 22))[None, None, :])
    metal = colour((150, 150, 148))[None, None, :] * (0.8 + 0.2 * noise(S, 2)[..., None])
    t = np.clip(scratches + wear * 0.8, 0, 1)
    c = mix(paint, metal, t)
    rivets = np.zeros((S, S))
    yy, xx = np.mgrid[0:S, 0:S]
    for cx in (32, S // 2 + 32):
        for cy in range(32, S, 128):
            rivets += np.exp(-((xx - cx) ** 2 + (yy - cy) ** 2) / 30.0)
    c = c * (1 - 0.15 * rivets[..., None])
    write_set(out, name, c, rivets * 1.5 - t * 0.2, 5.0, 0.25 + 0.3 * t, 0.4 + 0.2 * t)


def concrete(out, name):
    base = fbm(S, 220, 6)
    speck = noise(S, 1.2)
    cracks = smoothstep(0.985, 1.0, 1 - np.abs(noise(S, 90) - 0.5) * 2)
    c = colour((138, 134, 126))[None, None, :] * (0.7 + 0.35 * base[..., None]) * (0.92 + 0.12 * speck[..., None])
    c *= (1 - 0.5 * cracks)[..., None]
    write_set(out, name, c, base * 0.3 + speck * 0.4 - cracks, 3.0, 0.05, 0.1)


def brick(out, name):
    yy, xx = np.mgrid[0:S, 0:S]
    row = yy // 64
    xo = (xx + (row % 2) * 64) % 128
    mortar = (yy % 64 < 6) | (xo < 6)
    bid = (row * 8 + (xx + (row % 2) * 64) // 128) % 64
    tint = np.random.default_rng(3).random(64)[bid]
    c = mix(colour((118, 52, 36))[None, None, :], colour((168, 84, 56))[None, None, :], tint * 0.7 + fbm(S, 30, 3) * 0.3)
    c = np.where(mortar[..., None], colour((150, 144, 130))[None, None, :] * (0.8 + 0.2 * noise(S, 2)[..., None]), c)
    write_set(out, name, c, np.where(mortar, 0.0, 1.0) + fbm(S, 8, 3) * 0.2, 6.0, 0.05, 0.15)


def water(out, name, deep, shallow):
    ripples = fbm(S, 120, 5)
    caustic = np.abs(np.sin(noise(S, 60) * 30))
    c = mix(colour(deep)[None, None, :], colour(shallow)[None, None, :], ripples * 0.7 + caustic * 0.3)
    write_set(out, name, c, ripples, 4.0, 0.9, 0.9)


def slag(out, name):
    rock = fbm(S, 90, 6)
    cells = noise(S, 26)
    glow = smoothstep(0.47, 0.5, cells) * smoothstep(0.53, 0.5, cells)
    c = colour((44, 40, 34))[None, None, :] * (0.6 + 0.6 * rock[..., None])
    c = mix(c, colour((150, 220, 60))[None, None, :], glow * 0.85)
    write_set(out, name, c, rock - glow * 0.5, 5.0, 0.1, 0.2)


def rubber(out, name):
    yy, xx = np.mgrid[0:S, 0:S]
    grid = ((xx % 64) < 4) | ((yy % 64) < 4)
    c = colour((34, 34, 34))[None, None, :] * (0.85 + 0.2 * noise(S, 2)[..., None])
    c = np.where(grid[..., None], c * 0.6, c)
    line = (np.abs(yy - S // 2) < 10)
    c = np.where(line[..., None], colour((220, 218, 205))[None, None, :], c)
    write_set(out, name, c, np.where(grid, 0.0, 1.0) * 0.3, 3.0, 0.08, 0.25)


PALETTE = [
    (236, 232, 220), (196, 36, 32), (240, 196, 40), (170, 20, 28),     # white, red, yellow, Nuka red
    (36, 84, 150), (246, 200, 40), (88, 96, 58), (26, 26, 26),         # Vault blue, Vault yellow, olive, black
    (226, 112, 36), (232, 214, 170), (110, 72, 44), (170, 172, 176),   # orange, cream, brown, silver
    (52, 140, 64), (232, 120, 160), (40, 150, 150), (140, 66, 30),     # green, pink, teal, rust
]


def palette(out, name):
    """16 swatches (4 x 4 of 256 px) of worn paint: the obstacles' faces are UV'd into these."""
    c = np.zeros((S, S, 3))
    wear = fbm(S, 60, 5)
    chips = smoothstep(0.7, 0.76, fbm(S, 40, 5))
    for i, rgb in enumerate(PALETTE):
        r, k = divmod(i, 4)
        sl = (slice(r * 256, r * 256 + 256), slice(k * 256, k * 256 + 256))
        c[sl] = colour(rgb)[None, None, :] * (0.86 + 0.14 * wear[sl][..., None])
    metal = colour((120, 118, 112))[None, None, :]
    c = mix(c, metal, chips * 0.9)
    write_set(out, name, c, chips * -0.5 + wear * 0.1, 3.0, 0.25, 0.35)


def signs(out, name):
    """18 hole signs, 6 x 3 cells of 341 px on a 2048 x 1024 atlas: hole number, name and par."""
    W, H = 2048, 1024
    cw, ch = W // 6, H // 3
    base = to_img(np.dstack([fbm(1024, 120, 5)] * 3)).resize((W, H))
    img = Image.new("RGB", (W, H))
    draw = ImageDraw.Draw(img)
    big = ImageFont.truetype(FONT_NUM, 170)
    small = ImageFont.truetype(FONT_TEXT, 30)
    par = ImageFont.truetype(FONT_TEXT, 36)
    themes = {
        "junk": ((206, 192, 160), (60, 40, 30), (150, 50, 30)),
        "americana": ((240, 232, 210), (180, 28, 32), (30, 60, 130)),
        "vault": ((36, 84, 150), (246, 200, 40), (240, 240, 240)),
        "military": ((92, 98, 62), (236, 230, 200), (230, 180, 40)),
        "glow": ((54, 58, 46), (170, 230, 70), (230, 230, 200)),
    }
    for i, (num, p, title, theme) in enumerate(HOLES):
        bg, fg, accent = themes[theme]
        x0, y0 = (i % 6) * cw, (i // 6) * ch
        draw.rectangle([x0, y0, x0 + cw - 1, y0 + ch - 1], fill=bg)
        draw.rectangle([x0 + 12, y0 + 12, x0 + cw - 13, y0 + ch - 13], outline=accent, width=8)
        t = str(num)
        tw = draw.textlength(t, font=big)
        draw.text((x0 + (cw - tw) / 2, y0 + 10), t, font=big, fill=fg)
        size = 30
        while size > 14:
            small = ImageFont.truetype(FONT_TEXT, size)
            if draw.textlength(title.upper(), font=small) < cw - 50:
                break
            size -= 2
        tw = draw.textlength(title.upper(), font=small)
        draw.text((x0 + (cw - tw) / 2, y0 + 226), title.upper(), font=small, fill=fg)
        pt = f"PAR {p}"
        tw = draw.textlength(pt, font=par)
        draw.text((x0 + (cw - tw) / 2, y0 + 270), pt, font=par, fill=accent)
    arr = np.asarray(img).astype(np.float64) / 255.0
    grime = np.asarray(base).astype(np.float64)[..., :1] / 255.0
    arr = arr * (0.82 + 0.22 * grime)
    d = to_img(arr)
    if os.environ.get("MG_PNG"):
        d.save(os.path.join(out, f"{name}_d.png"))
    save_dds(d, os.path.join(out, f"{name}_d.dds"), "DXT1")
    flat = np.zeros((H, W))
    save_dds(to_img(np.dstack([flat + 0.5, flat + 0.5, flat])).resize((64, 64)), os.path.join(out, f"{name}_n.dds"), "BC5")
    save_dds(to_img(np.dstack([flat + 0.06, flat + 0.2, flat])).resize((64, 64)), os.path.join(out, f"{name}_s.dds"), "BC5")
    print("wrote", name)


def flags(out, name):
    """18 flags, 6 x 3 cells of 170 px on a 1024 x 512 atlas: a white number on red (on yellow for vault holes)."""
    W, H = 1024, 512
    cw, ch = W // 6, H // 3
    img = Image.new("RGB", (W, H))
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT_NUM, 120)
    for i, (num, _p, _t, theme) in enumerate(HOLES):
        x0, y0 = (i % 6) * cw, (i // 6) * ch
        bg = (246, 200, 40) if theme == "vault" else (200, 30, 30)
        fg = (36, 84, 150) if theme == "vault" else (250, 250, 245)
        draw.rectangle([x0, y0, x0 + cw - 1, y0 + ch - 1], fill=bg)
        t = str(num)
        tw = draw.textlength(t, font=font)
        draw.text((x0 + (cw - tw) / 2, y0 - 2), t, font=font, fill=fg)
    d = img.filter(ImageFilter.SMOOTH)
    if os.environ.get("MG_PNG"):
        d.save(os.path.join(out, f"{name}_d.png"))
    save_dds(d, os.path.join(out, f"{name}_d.dds"), "DXT1")
    flat = np.zeros((H, W))
    save_dds(to_img(np.dstack([flat + 0.5, flat + 0.5, flat])).resize((64, 64)), os.path.join(out, f"{name}_n.dds"), "BC5")
    save_dds(to_img(np.dstack([flat + 0.05, flat + 0.15, flat])).resize((64, 64)), os.path.join(out, f"{name}_s.dds"), "BC5")
    print("wrote", name)


def turf(out, name):
    """Mini golf carpet: bright green, a short dense nap of fine fibres (some lighter tips, some darker roots), the
    tufting's faint rows, the nap lying a little differently here and there, a little sheen, no dirt.
    (The game's own carpets are stained and dull, its turf grassy: none looks like a mini golf course's.)"""
    rng = np.random.default_rng(42)
    yy, xx = np.mgrid[0:S, 0:S]
    grain = rng.random((S, S))
    # fibres: short strands, smeared a pixel or two along their own (random) lie
    strands = (noise(S, 0.6, aspect=(1.0, 0.45)) + noise(S, 0.6, aspect=(0.45, 1.0))) * 0.5
    f = np.clip(grain * 0.45 + strands * 0.55, 0, 1)
    tips = (rng.random((S, S)) > 0.955).astype(float)   # light fibre ends catching the light
    roots = (rng.random((S, S)) > 0.94).astype(float)   # dark gaps between tufts
    rows = 0.5 + 0.5 * np.sin(yy / S * 2 * math.pi * 128 + noise(S, 30) * 2.0)
    lay = fbm(S, 160, 3)
    base = colour((44, 136, 56))
    c = base[None, None, :] * (0.72 + 0.5 * f[..., None]) * (0.92 + 0.14 * lay[..., None]) * (0.96 + 0.06 * rows[..., None])
    c = mix(c, colour((120, 205, 110))[None, None, :], tips * 0.6)
    c = mix(c, colour((18, 60, 24))[None, None, :], roots * 0.55)
    height = f * 0.8 + rows * 0.15 + tips * 0.4 - roots * 0.5
    write_set(out, name, c, height, 3.0, 0.07 + 0.05 * lay, 0.2)


def rubber_mat(out, name):
    """black rubber matting: a fine pebble texture"""
    peb = noise(S, 1.6)
    c = colour((30, 30, 31))[None, None, :] * (0.8 + 0.35 * peb[..., None]) * (0.92 + 0.12 * fbm(S, 120, 3)[..., None])
    write_set(out, name, c, peb, 4.0, 0.06, 0.3)


def steel(out, name, rgb, gloss=0.5, brushed=True):
    """bare or dark steel: brushed streaks, a little grime"""
    streak = noise(S, 2, aspect=(1.0, 0.02)) if brushed else noise(S, 2)
    grime = smoothstep(0.55, 0.8, fbm(S, 200, 5))
    c = colour(rgb)[None, None, :] * (0.85 + 0.25 * streak[..., None]) * (1 - 0.35 * grime[..., None])
    write_set(out, name, c, streak * 0.2, 2.0, 0.6 - 0.3 * grime, gloss - 0.2 * grime)


def glass(out, name):
    """dark window glass with soft sky reflections"""
    yy, xx = np.mgrid[0:S, 0:S]
    sky = 0.5 + 0.5 * np.sin((xx * 0.6 + yy) / S * math.pi * 2)
    c = mix(colour((22, 30, 38))[None, None, :], colour((90, 110, 124))[None, None, :], (sky * 0.5 + fbm(S, 300, 3) * 0.3)[..., None])
    write_set(out, name, c, np.zeros((S, S)), 1.0, 0.9, 0.95)


def canvas(out, name):
    """sail canvas: a woven cream cloth, weathered"""
    yy, xx = np.mgrid[0:S, 0:S]
    weave = (0.5 + 0.25 * np.sin(xx * 1.6) + 0.25 * np.sin(yy * 1.6))
    stain = smoothstep(0.5, 0.85, fbm(S, 180, 5))
    c = colour((214, 204, 180))[None, None, :] * (0.85 + 0.15 * weave[..., None])
    c = mix(c, colour((120, 104, 80))[None, None, :], stain * 0.5)
    write_set(out, name, c, weave, 2.0, 0.03, 0.1)


def solid(out, name, rgb, spec=0.3, gloss=0.6):
    """one flat colour (lamps, neon tubes, flames): a little texture so it isn't dead flat"""
    c = colour(rgb)[None, None, :] * (0.94 + 0.06 * fbm(S, 64, 3)[..., None])
    write_set(out, name, c, np.zeros((S, S)), 1.0, spec, gloss)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "dist", "Data", "Textures", "fo4minigolf")
    os.makedirs(out, exist_ok=True)
    # Everything else on the course uses the game's own textures (see build_course.py MATERIALS): only the signs
    # and the flags need text of their own.
    only = sys.argv[2].split(",") if len(sys.argv) > 2 else None
    jobs = {
        "Signs": lambda: signs(out, "Signs"),
        "Turf": lambda: turf(out, "Turf"),
        # set pieces' materials the game has no plain tileable texture for
        "Rubber": lambda: rubber_mat(out, "Rubber"),
        "DarkMetal": lambda: steel(out, "DarkMetal", (62, 64, 66), 0.45),
        "Chrome": lambda: steel(out, "Chrome", (196, 198, 200), 0.85),
        "Olive": lambda: painted_steel(out, "Olive", (86, 92, 56)),
        "DarkWood": lambda: wood(out, "DarkWood", painted=(92, 58, 36)),
        "Canvas": lambda: canvas(out, "Canvas"),
        "Glass": lambda: glass(out, "Glass"),
        "Neon": lambda: solid(out, "Neon", (255, 120, 170), 0.5, 0.8),
        "Lamp": lambda: solid(out, "Lamp", (255, 190, 80), 0.5, 0.8),
        "Flame": lambda: solid(out, "Flame", (150, 200, 255), 0.2, 0.5),
    }
    for k, job in jobs.items():
        if only is None or k in only:
            job()


if __name__ == "__main__":
    main()
