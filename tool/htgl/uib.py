"""Node tree (IR) -> .uib binary bytes.

Layout: [Header 16B][Node 18B * count][Anim 10B * anim_count][StringTable].
The header's last u16 carries anim_count (0 for animation-free blobs, which are
then byte-identical to the original format). All little-endian.
"""

import struct

from .html_tree import ROOT_PARENT, TEXT

HEADER_FMT = "<4sBBHHHHH"
NODE_FMT = "<BBHhhhhHHH"
ANIM_FMT = "<HBBhhH"
HEADER_SIZE = struct.calcsize(HEADER_FMT)   # 16
NODE_SIZE = struct.calcsize(NODE_FMT)       # 18
ANIM_SIZE = struct.calcsize(ANIM_FMT)       # 10
NO_TEXT = 0xFFFF
VERSION = 1

_ANIM_PROP = {"x": 0, "y": 1, "w": 2, "h": 3}
_ANIM_LOOP = {"once": 0, "loop": 1, "pingpong": 2}


def build_uib(nodes, screen_w, screen_h):
    count = len(nodes)
    anims = [(i, n.anim) for i, n in enumerate(nodes) if getattr(n, "anim", None)]
    anim_count = len(anims)
    # Anim table sits between the node array and the string table.
    strtab_off = HEADER_SIZE + NODE_SIZE * count + ANIM_SIZE * anim_count

    # First pass: build string table and record each text node's offset.
    strtab = bytearray()
    text_offsets = {}  # node index -> offset within string table
    for i, n in enumerate(nodes):
        if n.type == TEXT and n.text is not None:
            data = n.text.encode("ascii", "replace")[:255]
            text_offsets[i] = len(strtab)
            strtab.append(len(data))
            strtab.extend(data)

    out = bytearray()
    out += struct.pack(
        HEADER_FMT, b"HTGL", VERSION, 0, count,
        screen_w, screen_h, strtab_off, anim_count,
    )
    for i, n in enumerate(nodes):
        parent = ROOT_PARENT if n.parent == ROOT_PARENT else n.parent
        text_ref = text_offsets.get(i, NO_TEXT)
        # font byte carries integer scale for TEXT nodes (font_size / 8, >=1)
        scale = max(1, int(round(getattr(n, "font_size", 8) / 8)))
        font_byte = scale if n.type == TEXT else 0
        out += struct.pack(
            NODE_FMT, n.type, font_byte, parent,
            n.x, n.y, n.w, n.h, n.bg, n.fg, text_ref,
        )
    for node_idx, a in anims:
        out += struct.pack(
            ANIM_FMT, node_idx, _ANIM_PROP[a["prop"]],
            _ANIM_LOOP.get(a["loop"], 0), a["from"], a["to"], a["dur"],
        )
    out += strtab
    return bytes(out)
