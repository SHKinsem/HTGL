"""Node tree (IR) -> .uib binary bytes.

V1 layout: [Header 16B][Node 18B * count][Anim 10B * anim_count][StringTable].
V2 adds an 8-bit opacity table (one byte per node) before the string table.
V3 optionally adds a 2-byte-per-node visual-style table (radius, backdrop blur).
Opaque documents continue to emit byte-identical V1 blobs. All little-endian.
"""

import struct
import zlib

from .diagnostics import warn
from .html_tree import TEXT

HEADER_FMT = "<4sBBHHHHH"
NODE_FMT = "<BBHhhhhHHH"
ANIM_FMT = "<HBBhhH"
HEADER_SIZE = struct.calcsize(HEADER_FMT)   # 16
NODE_SIZE = struct.calcsize(NODE_FMT)       # 18
ANIM_SIZE = struct.calcsize(ANIM_FMT)       # 10
NO_TEXT = 0xFFFF
VERSION_V1 = 1
VERSION_OPACITY = 2
VERSION_VISUAL_STYLE = 3
FLAG_CRC32 = 0x01
FLAG_OPACITY = 0x02
FLAG_VISUAL_STYLE = 0x04

_ANIM_PROP = {"x": 0, "y": 1, "w": 2, "h": 3, "bg": 4, "opacity": 5}
_ANIM_LOOP = {"once": 0, "loop": 1, "pingpong": 2}
_ANIM_EASE = {"linear": 0, "ease-in": 1, "ease-out": 2, "ease-in-out": 3}


def _clamp_i16(v, diag, what):
    """Clamp a coordinate/size to the int16 range the NODE/ANIM format stores.

    Without this an out-of-range value raises an uncaught struct.error; clamping
    turns an author typo (e.g. width:400000px) into a warning instead of a crash.
    """
    if v > 32767:
        warn(diag, "%s = %d exceeds the int16 max, clamped to 32767" % (what, v))
        return 32767
    if v < -32768:
        warn(diag, "%s = %d below the int16 min, clamped to -32768" % (what, v))
        return -32768
    return v


def build_uib(nodes, screen_w, screen_h, diag=None, crc=False):
    count = len(nodes)
    anims = [(i, n.anim) for i, n in enumerate(nodes) if getattr(n, "anim", None)]
    anim_count = len(anims)
    opacities = bytes(getattr(n, "opacity", 255) for n in nodes)
    has_opacity = any(alpha != 255 for alpha in opacities) or any(
        animation["prop"] == "opacity" for _, animation in anims)
    styles = bytearray()
    for n in nodes:
        styles.append(min(255, max(0, int(getattr(n, "radius", 0)))))
        styles.append(min(255, max(0, int(getattr(n, "backdrop_blur", 0)))))
    has_visual_style = any(styles)
    version = (VERSION_VISUAL_STYLE if has_visual_style else
               (VERSION_OPACITY if has_opacity else VERSION_V1))
    flags = ((FLAG_CRC32 if crc else 0) |
             (FLAG_OPACITY if has_opacity else 0) |
             (FLAG_VISUAL_STYLE if has_visual_style else 0))
    # Anim table plus optional V2/V3 tables precede the string table.
    strtab_off = HEADER_SIZE + NODE_SIZE * count + ANIM_SIZE * anim_count
    if has_opacity:
        strtab_off += count
    if has_visual_style:
        strtab_off += 2 * count

    # First pass: build string table and record each text node's offset.
    strtab = bytearray()
    text_offsets = {}  # node index -> offset within string table
    for i, n in enumerate(nodes):
        if n.type == TEXT and n.text is not None:
            if any(ord(c) > 127 for c in n.text):
                warn(diag, "non-ASCII text %r will render as '?' (engine font is ASCII-only)"
                     % n.text)
            data = n.text.encode("ascii", "replace")
            if len(data) > 255:
                warn(diag, "text %r... truncated to 255 bytes" % n.text[:24])
                data = data[:255]
            text_offsets[i] = len(strtab)
            strtab.append(len(data))
            strtab.extend(data)

    out = bytearray()
    out += struct.pack(
        HEADER_FMT, b"HTGL", version, flags, count,
        screen_w, screen_h, strtab_off, anim_count,
    )
    for i, n in enumerate(nodes):
        parent = n.parent
        text_ref = text_offsets.get(i, NO_TEXT)
        # font byte: TEXT → integer scale (font_size/8, >=1); BOX → tap id (0..255)
        fs = getattr(n, "font_size", 8)
        scale = max(1, int(round(fs / 8)))
        if n.type == TEXT and fs % 8 != 0:
            warn(diag, "font-size %dpx snapped to %dpx (font has 8px granularity)"
                 % (fs, scale * 8))
        font_byte = scale if n.type == TEXT else (getattr(n, "tap", 0) & 0xFF)
        x = _clamp_i16(n.x, diag, "node %d left/x" % i)
        y = _clamp_i16(n.y, diag, "node %d top/y" % i)
        w = _clamp_i16(n.w, diag, "node %d width" % i)
        h = _clamp_i16(n.h, diag, "node %d height" % i)
        out += struct.pack(
            NODE_FMT, n.type, font_byte, parent,
            x, y, w, h, n.bg, n.fg, text_ref,
        )
    for node_idx, a in anims:
        loop_code = _ANIM_LOOP.get(a["loop"], 0)
        ease_code = _ANIM_EASE.get(a.get("ease", "linear"), 0)
        mode_byte = loop_code | (ease_code << 4)
        # For bg (prop=4) the from/to values are RGB565 uint16 reinterpreted as int16.
        # Signed-wrap: values >= 0x8000 are stored as negative int16 (bit-preserving).
        if a["prop"] == "bg":
            frm = a["from"] - 0x10000 if a["from"] >= 0x8000 else a["from"]
            tov = a["to"]   - 0x10000 if a["to"]   >= 0x8000 else a["to"]
        else:
            frm = _clamp_i16(a["from"], diag, "anim from")
            tov = _clamp_i16(a["to"], diag, "anim to")
        out += struct.pack(
            ANIM_FMT, node_idx, _ANIM_PROP[a["prop"]],
            mode_byte, frm, tov, a["dur"],
        )
    if has_opacity:
        out += opacities
    if has_visual_style:
        out += styles
    out += strtab
    if crc:
        # CRC32 over the whole blob (header with flags=1 + nodes + anims + strtab),
        # appended as a little-endian u32 trailer. Verified by the engine on load.
        out += struct.pack("<I", zlib.crc32(bytes(out)) & 0xFFFFFFFF)
    return bytes(out)
