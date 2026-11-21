"""Reads files out of the game's BA2 archives (for previewing the game's textures and meshes in Blender while building
the course; nothing extracted here ships with the mod). Plain Python, runs inside Blender.

    extract(data_folder, ["textures/interiors/Institute/InstituteTurf01_d.dds", "meshes/..."], out_folder)

General archives (GNRL: meshes) store whole files; texture archives (DX10) store mip chunks, and a DDS header is put
back in front of them.
"""

import os
import struct
import zlib

DDSD = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000  # CAPS HEIGHT WIDTH PIXELFORMAT MIPMAPCOUNT
# DXGI formats the game uses, and their bytes per 4x4 block (0 = not block compressed: bytes per pixel in the tuple)
BLOCK = {71: 8, 72: 8, 74: 16, 77: 16, 80: 8, 83: 16, 95: 16, 98: 16, 99: 16}
UNCOMPRESSED = {28: 4, 87: 4, 61: 1, 56: 2}


def _dds_header(width, height, mips, dxgi):
    if dxgi in BLOCK:
        pitch = max(1, (width + 3) // 4) * max(1, (height + 3) // 4) * BLOCK[dxgi]
        flags = DDSD | 0x80000  # LINEARSIZE
    else:
        pitch = width * UNCOMPRESSED.get(dxgi, 4)
        flags = DDSD | 0x8  # PITCH
    pf = struct.pack("<II4sIIIII", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)
    caps = 0x1000 | 0x8 | 0x400000
    hdr = struct.pack("<4sIIIIIII", b"DDS ", 124, flags, height, width, pitch, 0, mips) + b"\0" * 44 + pf
    hdr += struct.pack("<IIIII", caps, 0, 0, 0, 0)
    dx10 = struct.pack("<IIIII", dxgi, 3, 0, 1, 0)  # format, TEXTURE2D, misc, array size, misc2
    return hdr + dx10


def _index(path):
    with open(path, "rb") as f:
        magic, version, kind, count, names_at = struct.unpack("<4sI4sIQ", f.read(24))
        if version >= 2:
            f.read(8)  # version 2/3 extra header fields (Next Gen)
        if version == 3:
            f.read(4)
        entries = []
        if kind == b"GNRL":
            for _ in range(count):
                _h, _ext, _d, _flags, offset, packed, size, _align = struct.unpack("<I4sIIQIII", f.read(36))
                entries.append(("GNRL", offset, packed, size))
        else:
            for _ in range(count):
                _h, _ext, _d, _u8, nchunks, _chs, height, width, mips, dxgi, _u16 = struct.unpack("<I4sIBBHHHBBH", f.read(24))
                chunks = []
                for _c in range(nchunks):
                    offset, packed, size, _m0, _m1, _a = struct.unpack("<QIIHHI", f.read(24))
                    chunks.append((offset, packed, size))
                entries.append(("DX10", width, height, mips, dxgi, chunks))
        f.seek(names_at)
        names = []
        for _ in range(count):
            n = struct.unpack("<H", f.read(2))[0]
            names.append(f.read(n).decode("utf-8", "replace").replace("\\", "/").lower())
    return dict(zip(names, entries))


def _read(f, offset, packed, size):
    f.seek(offset)
    if packed:
        return zlib.decompress(f.read(packed))
    return f.read(size)


def extract(data_folder, wanted, out_folder):
    """Extracts each wanted path (relative to Data, any case) into out_folder/<path>. Returns {path: written path}."""
    wanted = {w.replace("\\", "/").lower(): w for w in wanted}
    found = {}
    for arc in sorted(os.listdir(data_folder)):
        if not arc.lower().endswith(".ba2") or not wanted:
            continue
        full = os.path.join(data_folder, arc)
        try:
            index = _index(full)
        except Exception:
            continue
        hits = [w for w in wanted if w in index]
        if not hits:
            continue
        with open(full, "rb") as f:
            for w in hits:
                e = index[w]
                if e[0] == "GNRL":
                    data = _read(f, e[1], e[2], e[3])
                else:
                    _k, width, height, mips, dxgi, chunks = e
                    data = _dds_header(width, height, mips, dxgi) + b"".join(_read(f, *c) for c in chunks)
                dest = os.path.join(out_folder, *wanted[w].replace("\\", "/").split("/"))
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                with open(dest, "wb") as o:
                    o.write(data)
                found[wanted[w]] = dest
                del wanted[w]
    return found
