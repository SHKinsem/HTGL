import struct

from htgl.html_tree import parse_html
from htgl.uib import (
    build_uib, HEADER_FMT, NODE_FMT, ANIM_FMT,
    HEADER_SIZE, NODE_SIZE, ANIM_SIZE,
    _ANIM_EASE,
)

ANIM_DIV = (
    '<div style="position:absolute;left:0;top:0;width:240px;height:320px;background-color:#000">'
    '<div style="position:absolute;left:10px;top:60px;width:30px;height:30px;background-color:#f00" '
    'data-anim="x" data-from="10" data-to="200" data-dur="1000" data-loop="pingpong"></div>'
    '</div>'
)


def test_header_anim_count_and_strtab_shift():
    nodes = parse_html(ANIM_DIV, 240, 320)
    blob = build_uib(nodes, 240, 320)
    hdr = struct.unpack_from(HEADER_FMT, blob, 0)
    anim_count = hdr[7]          # last header field (was 'reserved')
    strtab_off = hdr[6]
    assert anim_count == 1
    nodes_end = HEADER_SIZE + NODE_SIZE * len(nodes)
    # string table now sits AFTER the anim table
    assert strtab_off == nodes_end + ANIM_SIZE * 1


def test_anim_record_fields():
    nodes = parse_html(ANIM_DIV, 240, 320)
    blob = build_uib(nodes, 240, 320)
    off = HEADER_SIZE + NODE_SIZE * len(nodes)   # anim table starts here
    node_idx, prop, loop, frm, to, dur = struct.unpack_from(ANIM_FMT, blob, off)
    assert node_idx == 2         # 0=SCREEN, 1=outer box, 2=inner animated box
    assert prop == 0             # x
    assert loop == 2             # pingpong
    assert (frm, to, dur) == (10, 200, 1000)


def test_no_anim_is_backward_compatible():
    nodes = parse_html('<div>Hi</div>', 100, 100)
    blob = build_uib(nodes, 100, 100)
    hdr = struct.unpack_from(HEADER_FMT, blob, 0)
    assert hdr[7] == 0           # anim_count
    # strtab immediately follows the node array, exactly as before animation existed
    assert hdr[6] == HEADER_SIZE + NODE_SIZE * len(nodes)
    # and the text is still resolvable at that offset
    text_node_off = HEADER_SIZE + NODE_SIZE * 2
    text_ref = struct.unpack_from(NODE_FMT, blob, text_node_off)[9]
    pos = hdr[6] + text_ref
    assert blob[pos] == 2 and blob[pos + 1:pos + 3] == b"Hi"


def test_text_resolves_with_anim_table_present():
    html = (
        '<div style="width:100px;height:100px">'
        '<div data-anim="y" data-from="0" data-to="50" data-dur="500" data-loop="loop">Hi</div>'
        '</div>'
    )
    nodes = parse_html(html, 100, 100)
    blob = build_uib(nodes, 100, 100)
    hdr = struct.unpack_from(HEADER_FMT, blob, 0)
    assert hdr[7] == 1
    # the TEXT node's text_ref is relative to strtab_off, which is past the anim table
    text_idx = next(i for i, n in enumerate(nodes) if n.type == 2)
    text_ref = struct.unpack_from(NODE_FMT, blob, HEADER_SIZE + NODE_SIZE * text_idx)[9]
    pos = hdr[6] + text_ref
    assert blob[pos] == 2 and blob[pos + 1:pos + 3] == b"Hi"


# ---------------------------------------------------------------------------
# Easing: data-ease attribute
# ---------------------------------------------------------------------------

def _get_mode_byte(html, screen_w=240, screen_h=320):
    """Parse html, build uib, return the mode byte of the first anim record."""
    nodes = parse_html(html, screen_w, screen_h)
    blob = build_uib(nodes, screen_w, screen_h)
    off = HEADER_SIZE + NODE_SIZE * len(nodes)
    _, _, mode, _, _, _ = struct.unpack_from(ANIM_FMT, blob, off)
    return mode


def test_ease_default_linear():
    """No data-ease → ease=0 → high nibble 0 → mode == loop_code."""
    html = (
        '<div style="position:absolute;left:0;top:0;width:240px;height:320px;">'
        '<div data-anim="x" data-from="10" data-to="200" data-dur="1000" data-loop="once"></div>'
        '</div>'
    )
    mode = _get_mode_byte(html)
    loop_code = mode & 0x0F
    ease_code = (mode >> 4) & 0x0F
    assert loop_code == 0   # once
    assert ease_code == 0   # linear (default)


def test_ease_linear_explicit():
    html = (
        '<div style="width:240px;height:320px;">'
        '<div data-anim="x" data-from="0" data-to="100" data-dur="500"'
        ' data-loop="once" data-ease="linear"></div>'
        '</div>'
    )
    mode = _get_mode_byte(html)
    assert (mode >> 4) & 0x0F == _ANIM_EASE["linear"]   # 0


def test_ease_in():
    html = (
        '<div style="width:240px;height:320px;">'
        '<div data-anim="x" data-from="0" data-to="100" data-dur="500"'
        ' data-loop="once" data-ease="ease-in"></div>'
        '</div>'
    )
    mode = _get_mode_byte(html)
    assert (mode >> 4) & 0x0F == _ANIM_EASE["ease-in"]   # 1


def test_ease_out():
    html = (
        '<div style="width:240px;height:320px;">'
        '<div data-anim="x" data-from="0" data-to="100" data-dur="500"'
        ' data-loop="loop" data-ease="ease-out"></div>'
        '</div>'
    )
    mode = _get_mode_byte(html)
    assert (mode & 0x0F) == 1                              # loop
    assert (mode >> 4) & 0x0F == _ANIM_EASE["ease-out"]  # 2


def test_ease_in_out():
    html = (
        '<div style="width:240px;height:320px;">'
        '<div data-anim="x" data-from="0" data-to="100" data-dur="500"'
        ' data-loop="pingpong" data-ease="ease-in-out"></div>'
        '</div>'
    )
    mode = _get_mode_byte(html)
    assert (mode & 0x0F) == 2                                 # pingpong
    assert (mode >> 4) & 0x0F == _ANIM_EASE["ease-in-out"]  # 3


def test_backward_compat_no_ease_attr():
    """Existing data-anim without data-ease must produce mode byte == loop code (ease high nibble 0).
    This verifies that re-transpiling old HTMLs (like anim_decl.html) is byte-identical."""
    html = (
        '<div style="position:absolute;left:0;top:0;width:240px;height:200px;background-color:#1a1a2e;">'
        '<div style="position:absolute;left:10px;top:60px;width:30px;height:30px;background-color:#ff5050"'
        ' data-anim="x" data-from="10" data-to="200" data-dur="1000" data-loop="pingpong"></div>'
        '</div>'
    )
    mode = _get_mode_byte(html, 240, 200)
    assert mode == 2    # pingpong=2, ease=0 → mode=0x02 (unchanged from before easing was added)


# ---------------------------------------------------------------------------
# bg color animation (prop=4)
# ---------------------------------------------------------------------------

def _signed16(c):
    """Wrap an RGB565 uint16 into a signed int16 (bit-preserving)."""
    return c - 0x10000 if c >= 0x8000 else c


def test_bg_anim_prop_code():
    """data-anim="bg" must produce prop code 4."""
    html = (
        '<div style="width:240px;height:200px;">'
        '<div style="position:absolute;left:10px;top:10px;width:50px;height:50px;'
        'background-color:#ff0000"'
        ' data-anim="bg" data-from="#ff0000" data-to="#0000ff"'
        ' data-dur="1000" data-loop="once"></div>'
        '</div>'
    )
    nodes = parse_html(html, 240, 200)
    blob = build_uib(nodes, 240, 200)
    off = HEADER_SIZE + NODE_SIZE * len(nodes)
    node_idx, prop, loop, frm, to, dur = struct.unpack_from(ANIM_FMT, blob, off)
    assert prop == 4, f"expected prop=4 (bg), got {prop}"


def test_bg_anim_from_to_bytes():
    """data-from="#ff0000" -> RGB565 0xF800; data-to="#0000ff" -> 0x001F.
    Both are stored as signed int16 (bit-preserving wrap)."""
    html = (
        '<div style="width:240px;height:200px;">'
        '<div style="position:absolute;left:10px;top:10px;width:50px;height:50px;'
        'background-color:#ff0000"'
        ' data-anim="bg" data-from="#ff0000" data-to="#0000ff"'
        ' data-dur="1000" data-loop="once"></div>'
        '</div>'
    )
    nodes = parse_html(html, 240, 200)
    blob = build_uib(nodes, 240, 200)
    off = HEADER_SIZE + NODE_SIZE * len(nodes)
    node_idx, prop, loop, frm, to, dur = struct.unpack_from(ANIM_FMT, blob, off)

    # #ff0000 -> RGB565 0xF800 -> signed int16 = -2048
    # #0000ff -> RGB565 0x001F -> signed int16 = 31
    red_rgb565   = 0xF800
    blue_rgb565  = 0x001F
    assert frm == _signed16(red_rgb565),  f"from={frm} != {_signed16(red_rgb565)}"
    assert to   == _signed16(blue_rgb565), f"to={to} != {_signed16(blue_rgb565)}"


def test_bg_anim_dur_and_loop():
    """dur and loop are packed correctly for a bg anim."""
    html = (
        '<div style="width:240px;height:200px;">'
        '<div style="width:50px;height:50px;background-color:#f00"'
        ' data-anim="bg" data-from="#f00" data-to="#00f"'
        ' data-dur="1200" data-loop="pingpong" data-ease="ease-in-out"></div>'
        '</div>'
    )
    nodes = parse_html(html, 240, 200)
    blob = build_uib(nodes, 240, 200)
    off = HEADER_SIZE + NODE_SIZE * len(nodes)
    node_idx, prop, loop_byte, frm, to, dur = struct.unpack_from(ANIM_FMT, blob, off)
    assert prop == 4
    assert dur == 1200
    assert (loop_byte & 0x0F) == 2       # pingpong
    assert (loop_byte >> 4) & 0x0F == 3  # ease-in-out


def test_bg_anim_named_color():
    """Named colors (e.g., 'red', 'blue') should also work via to_rgb565."""
    html = (
        '<div style="width:240px;height:200px;">'
        '<div style="width:50px;height:50px;background-color:red"'
        ' data-anim="bg" data-from="red" data-to="blue"'
        ' data-dur="500" data-loop="once"></div>'
        '</div>'
    )
    nodes = parse_html(html, 240, 200)
    blob = build_uib(nodes, 240, 200)
    off = HEADER_SIZE + NODE_SIZE * len(nodes)
    node_idx, prop, loop_byte, frm, to, dur = struct.unpack_from(ANIM_FMT, blob, off)
    assert prop == 4
    from htgl.colors import to_rgb565
    assert frm == _signed16(to_rgb565("red"))
    assert to   == _signed16(to_rgb565("blue"))
