"""Tests for touch/tap encoding in the transpiler.

- A div with data-tap="3" must produce a BOX node with font byte == 3.
- A div without data-tap must produce font byte == 0.
- data-tap out of range (0, 256, negative) must produce 0.
"""

import struct

from htgl.html_tree import parse_html
# Format strings come from the emitter (single source of truth), not redefined here.
from htgl.uib import build_uib, HEADER_FMT, NODE_FMT, HEADER_SIZE, NODE_SIZE


def _box_font_byte(html, box_index=1):
    """Return the font byte for the box at `box_index` in the emitted blob."""
    nodes = parse_html(html, 240, 320)
    blob = build_uib(nodes, 240, 320)
    off = HEADER_SIZE + NODE_SIZE * box_index
    rec = struct.unpack_from(NODE_FMT, blob, off)
    return rec[1]  # font byte is index 1 in node record


def test_data_tap_sets_font_byte():
    html = '<div style="left:10px;top:10px;width:40px;height:40px" data-tap="3"></div>'
    assert _box_font_byte(html) == 3


def test_data_tap_1_and_255():
    html1 = '<div style="width:10px;height:10px" data-tap="1"></div>'
    assert _box_font_byte(html1) == 1

    html255 = '<div style="width:10px;height:10px" data-tap="255"></div>'
    assert _box_font_byte(html255) == 255


def test_no_data_tap_gives_zero():
    html = '<div style="left:10px;top:10px;width:40px;height:40px"></div>'
    assert _box_font_byte(html) == 0


def test_data_tap_zero_gives_zero():
    # 0 is outside [1..255] so must clamp to 0
    html = '<div style="width:10px;height:10px" data-tap="0"></div>'
    assert _box_font_byte(html) == 0


def test_data_tap_out_of_range_high_gives_zero():
    html = '<div style="width:10px;height:10px" data-tap="256"></div>'
    assert _box_font_byte(html) == 0


def test_data_tap_negative_gives_zero():
    html = '<div style="width:10px;height:10px" data-tap="-1"></div>'
    assert _box_font_byte(html) == 0


def test_data_tap_invalid_string_gives_zero():
    html = '<div style="width:10px;height:10px" data-tap="foo"></div>'
    assert _box_font_byte(html) == 0


def test_text_node_font_scale_unaffected():
    """TEXT nodes must still use font_size/8 for their font byte, not tap."""
    html = '<div style="font-size:16px" data-tap="5">Hello</div>'
    nodes = parse_html(html, 240, 320)
    blob = build_uib(nodes, 240, 320)
    # node[1] is the BOX (tap=5 -> font byte=5)
    off_box = HEADER_SIZE + NODE_SIZE * 1
    rec_box = struct.unpack_from(NODE_FMT, blob, off_box)
    assert rec_box[1] == 5  # tap id stored in font byte of BOX

    # node[2] is the TEXT (scale=2 for 16px -> font byte=2)
    off_text = HEADER_SIZE + NODE_SIZE * 2
    rec_text = struct.unpack_from(NODE_FMT, blob, off_text)
    assert rec_text[1] == 2  # text scale unchanged
