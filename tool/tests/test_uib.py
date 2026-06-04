import struct

from htgl.html_tree import parse_html
from htgl.uib import build_uib

HEADER_FMT = "<4sBBHHHHH"
NODE_FMT = "<BBHhhhhHHH"
HEADER_SIZE = struct.calcsize(HEADER_FMT)  # 16
NODE_SIZE = struct.calcsize(NODE_FMT)      # 18

def test_header_fields():
    nodes = parse_html('<div></div>', 240, 320)
    blob = build_uib(nodes, 240, 320)
    magic, ver, flags, count, w, h, strtab_off, reserved = \
        struct.unpack_from(HEADER_FMT, blob, 0)
    assert magic == b"HTGL"
    assert ver == 1 and flags == 0
    assert count == len(nodes)
    assert w == 240 and h == 320
    assert strtab_off == HEADER_SIZE + NODE_SIZE * len(nodes)

def test_node_record_layout():
    html = '<div style="left:10px;top:20px;width:30px;height:40px;background-color:#f00"></div>'
    nodes = parse_html(html, 100, 100)
    blob = build_uib(nodes, 100, 100)
    # node[1] is the box (node[0] is SCREEN)
    off = HEADER_SIZE + NODE_SIZE * 1
    type, font, parent, x, y, w, h, bg, fg, text_ref = \
        struct.unpack_from(NODE_FMT, blob, off)
    assert type == 1 and parent == 0
    assert (x, y, w, h) == (10, 20, 30, 40)
    assert bg == 0xF800
    assert text_ref == 0xFFFF

def test_string_table_for_text():
    nodes = parse_html('<div>Hi</div>', 100, 100)
    blob = build_uib(nodes, 100, 100)
    # text node is node[2] (screen, box, text)
    off = HEADER_SIZE + NODE_SIZE * 2
    rec = struct.unpack_from(NODE_FMT, blob, off)
    text_ref = rec[9]
    strtab_off = struct.unpack_from(HEADER_FMT, blob, 0)[6]
    pos = strtab_off + text_ref
    length = blob[pos]
    assert length == 2
    assert blob[pos + 1:pos + 1 + length] == b"Hi"
