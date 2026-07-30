import struct

from htgl.css import parse_style
from htgl.diagnostics import Diagnostics
from htgl.html_tree import parse_html
from htgl.uib import (
    FLAG_OPACITY,
    HEADER_FMT,
    HEADER_SIZE,
    NODE_SIZE,
    VERSION_OPACITY,
    build_uib,
)


def test_opacity_css_is_quantized_and_clamped_with_a_diagnostic():
    assert parse_style("opacity: .5")["opacity"] == 128
    diag = Diagnostics()
    assert parse_style("opacity: 2", diag)["opacity"] == 255
    assert diag.warnings == ["opacity 2 clamped to the supported range 0..1"]


def test_opacity_flattens_to_descendants_and_emits_v2_alpha_table():
    nodes = parse_html(
        '<div style="width:20px;height:20px;opacity:.5">'
        '<div style="width:10px;height:10px;opacity:.5">label</div>'
        '</div>',
        20,
        20,
    )
    # synthetic SCREEN is opaque; root, its child, and inherited text opacity follow.
    assert [node.opacity for node in nodes] == [255, 128, 64, 64]

    blob = build_uib(nodes, 20, 20)
    _, version, flags, count, _, _, strtab_off, _ = struct.unpack_from(HEADER_FMT, blob, 0)
    assert version == VERSION_OPACITY
    assert flags & FLAG_OPACITY
    alpha_offset = HEADER_SIZE + NODE_SIZE * count
    assert blob[alpha_offset:alpha_offset + count] == bytes([255, 128, 64, 64])
    assert strtab_off == alpha_offset + count


def test_fully_opaque_document_stays_v1_byte_layout():
    nodes = parse_html('<div style="width:20px;height:20px"></div>', 20, 20)
    blob = build_uib(nodes, 20, 20)
    _, version, flags, count, _, _, strtab_off, _ = struct.unpack_from(HEADER_FMT, blob, 0)
    assert version == 1
    assert flags == 0
    assert strtab_off == HEADER_SIZE + NODE_SIZE * count
