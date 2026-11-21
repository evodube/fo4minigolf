#!/usr/bin/env python3
"""Writes DDS textures with a full mip chain (Pillow writes only the top level).

    save_dds(image, path, fmt)   fmt: "DXT1" (BC1), "DXT5" (BC3), "BC5" (two channels, for normal maps) or "BC7"
                                 (high quality colour, through etcpak: pip install etcpak)
"""
import io
import struct

from PIL import Image


def _encode(level: Image.Image, fmt: str) -> tuple[bytes, bytes]:
    buf = io.BytesIO()
    level.save(buf, format="DDS", pixel_format=fmt)
    data = buf.getvalue()
    header_len = 128
    if data[84:88] == b"DX10":
        header_len += 20
    return data[:header_len], data[header_len:]


def _save_bc7(image: Image.Image, path: str) -> None:
    import etcpak
    image = image.convert("RGBA")
    levels = [image]
    w, h = image.size
    while w > 1 or h > 1:
        w, h = max(1, w // 2), max(1, h // 2)
        levels.append(levels[-1].resize((w, h), Image.LANCZOS))
    body = b""
    for lv in levels:
        bw, bh = max(4, (lv.width + 3) // 4 * 4), max(4, (lv.height + 3) // 4 * 4)
        if (bw, bh) != lv.size:
            padded = Image.new("RGBA", (bw, bh))
            padded.paste(lv, (0, 0))
            lv = padded
        body += etcpak.compress_bc7(lv.tobytes(), bw, bh, None)
    W, H = image.size
    pitch = max(1, (W + 3) // 4) * 16
    header = struct.pack("<4sIIIIIII", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000, H, W,
                         pitch * max(1, (H + 3) // 4), 0, len(levels))
    header += b"\0" * 44
    header += struct.pack("<II4s5I", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)  # pixel format: FourCC DX10
    header += struct.pack("<5I", 0x1000 | 0x8 | 0x400000, 0, 0, 0, 0)
    header += struct.pack("<5I", 98, 3, 0, 1, 0)  # DXGI_FORMAT_BC7_UNORM, 2D texture, one of it
    with open(path, "wb") as f:
        f.write(header + body)


def save_dds(image: Image.Image, path: str, fmt: str = "DXT1") -> None:
    if fmt == "BC7":
        _save_bc7(image, path)
        return
    mode = "RGBA" if fmt == "DXT5" else "RGB"
    image = image.convert(mode)
    levels = [image]
    w, h = image.size
    while w > 1 or h > 1:
        w, h = max(1, w // 2), max(1, h // 2)
        levels.append(levels[-1].resize((w, h), Image.LANCZOS))
    header = None
    body = b""
    for lv in levels:
        hdr, data = _encode(lv, fmt)
        header = header or bytearray(hdr)
        body += data
    # flags |= DDSD_MIPMAPCOUNT; mip count; caps |= COMPLEX | MIPMAP
    flags, = struct.unpack_from("<I", header, 8)
    struct.pack_into("<I", header, 8, flags | 0x20000)
    struct.pack_into("<I", header, 28, len(levels))
    caps, = struct.unpack_from("<I", header, 108)
    struct.pack_into("<I", header, 108, caps | 0x8 | 0x400000 | 0x1000)
    with open(path, "wb") as f:
        f.write(bytes(header) + body)
