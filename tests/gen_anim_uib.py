"""Emit an anim-bearing .uib with the REAL build_uib, for the C conformance probe.

Authored on the Python side so the C engine validates the *other* language's anim
bytes: the ANIM_FMT layout, the mode byte (loop | ease<<4), and the bg RGB565
signed-wrap. The e2e golden has no animations, so this is the only cross-language
guarantee for the animation encoding.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tool"))
from htgl.uib import build_uib

SCREEN, BOX = 0, 1
ROOT = 0xFFFF


class N:
    """Minimal stand-in for the transpiler's node, with the fields build_uib reads."""
    def __init__(self, **kw):
        self.type = kw["type"]
        self.parent = kw["parent"]
        self.x = kw.get("x", 0)
        self.y = kw.get("y", 0)
        self.w = kw.get("w", 0)
        self.h = kw.get("h", 0)
        self.bg = kw.get("bg", 0)
        self.fg = kw.get("fg", 0)
        self.text = kw.get("text")
        self.font_size = kw.get("font_size", 8)
        self.tap = kw.get("tap", 0)
        self.anim = kw.get("anim")


# BOX #1: geometry anim with a non-zero ease nibble + non-once loop.
# BOX #2: bg color anim exercising the RGB565 signed-wrap path.
nodes = [
    N(type=SCREEN, parent=ROOT, w=240, h=200, bg=0x0000),
    N(type=BOX, parent=0, x=10, y=0, w=30, h=30, bg=0xF800,
      anim={"prop": "x", "from": 10, "to": 200, "dur": 1000, "loop": "loop", "ease": "ease-out"}),
    N(type=BOX, parent=0, x=50, y=50, w=30, h=30, bg=0xF800,
      anim={"prop": "bg", "from": 0xF800, "to": 0x001F, "dur": 1000, "loop": "once", "ease": "linear"}),
]

if len(sys.argv) < 2:
    sys.exit("usage: gen_anim_uib.py <out.uib>")
# Emit WITH the CRC32 trailer so the C probe's successful load also proves the
# Python zlib.crc32 and the engine's htgl_crc32 agree on real bytes.
with open(sys.argv[1], "wb") as f:
    f.write(build_uib(nodes, 240, 200, crc=True))
