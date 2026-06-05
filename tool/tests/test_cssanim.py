"""Tests for cssanim.py — Phase 2 CSS animation parsing.

TDD: these tests are written before (or with) the implementation and must all pass.
"""

import struct
import pytest

from htgl.cssanim import parse_keyframes, parse_animation
from htgl.html_tree import parse_html
from htgl.uib import (
    build_uib, HEADER_FMT, NODE_FMT, ANIM_FMT,
    HEADER_SIZE, NODE_SIZE, ANIM_SIZE,
)


# ---------------------------------------------------------------------------
# parse_keyframes — property mapping
# ---------------------------------------------------------------------------

class TestParseKeyframes:
    def test_left_from_to(self):
        css = "@keyframes slide { from { left: 10px } to { left: 200px } }"
        kf = parse_keyframes(css)
        assert "slide" in kf
        assert kf["slide"] == {"prop": "x", "from": 10, "to": 200}

    def test_top_from_to(self):
        css = "@keyframes move { from { top: 20px } to { top: 150px } }"
        kf = parse_keyframes(css)
        assert kf["move"] == {"prop": "y", "from": 20, "to": 150}

    def test_width_from_to(self):
        css = "@keyframes grow { from { width: 30px } to { width: 100px } }"
        kf = parse_keyframes(css)
        assert kf["grow"] == {"prop": "w", "from": 30, "to": 100}

    def test_height_from_to(self):
        css = "@keyframes tall { from { height: 5px } to { height: 80px } }"
        kf = parse_keyframes(css)
        assert kf["tall"] == {"prop": "h", "from": 5, "to": 80}

    def test_zero_percent_as_from(self):
        css = "@keyframes slide { 0% { left: 10px } 100% { left: 200px } }"
        kf = parse_keyframes(css)
        assert kf["slide"] == {"prop": "x", "from": 10, "to": 200}

    def test_mixed_from_and_percent(self):
        css = "@keyframes slide { from { left: 10px } 100% { left: 200px } }"
        kf = parse_keyframes(css)
        assert kf["slide"] == {"prop": "x", "from": 10, "to": 200}

    def test_multiple_keyframe_blocks(self):
        css = """
        @keyframes slideX { from { left: 0px } to { left: 100px } }
        @keyframes slideY { from { top: 0px }  to { top: 50px }   }
        """
        kf = parse_keyframes(css)
        assert kf["slideX"]["prop"] == "x"
        assert kf["slideY"]["prop"] == "y"

    def test_missing_from_returns_empty(self):
        # No "from"/0% stop — rule skipped
        css = "@keyframes bad { to { left: 100px } }"
        kf = parse_keyframes(css)
        assert "bad" not in kf

    def test_missing_to_returns_empty(self):
        css = "@keyframes bad { from { left: 0px } }"
        kf = parse_keyframes(css)
        assert "bad" not in kf

    def test_no_supported_prop_returns_empty(self):
        css = "@keyframes bad { from { opacity: 0 } to { opacity: 1 } }"
        kf = parse_keyframes(css)
        assert "bad" not in kf

    def test_extra_keyframe_stops_use_from_to(self):
        # A 50% stop should be ignored; from/to are what matter
        css = "@keyframes multi { from { left: 0px } 50% { left: 50px } to { left: 100px } }"
        kf = parse_keyframes(css)
        assert kf["multi"] == {"prop": "x", "from": 0, "to": 100}

    def test_whitespace_and_semicolons(self):
        css = """
        @keyframes pad {
            from { left: 5px; }
            to   { left: 95px; }
        }
        """
        kf = parse_keyframes(css)
        assert kf["pad"] == {"prop": "x", "from": 5, "to": 95}


# ---------------------------------------------------------------------------
# parse_animation — shorthand parsing
# ---------------------------------------------------------------------------

class TestParseAnimation:
    def test_basic_1s(self):
        r = parse_animation("slide 1s")
        assert r == {"name": "slide", "dur": 1000, "loop": "once"}

    def test_basic_500ms(self):
        r = parse_animation("bounce 500ms")
        assert r == {"name": "bounce", "dur": 500, "loop": "once"}

    def test_infinite_gives_loop(self):
        r = parse_animation("slide 1s infinite")
        assert r == {"name": "slide", "dur": 1000, "loop": "loop"}

    def test_infinite_alternate_gives_pingpong(self):
        r = parse_animation("slide 1s infinite alternate")
        assert r == {"name": "slide", "dur": 1000, "loop": "pingpong"}

    def test_alternate_without_infinite_gives_once(self):
        r = parse_animation("slide 1s alternate")
        assert r == {"name": "slide", "dur": 1000, "loop": "once"}

    def test_order_tolerant_duration_first(self):
        r = parse_animation("1s slide infinite alternate")
        assert r == {"name": "slide", "dur": 1000, "loop": "pingpong"}

    def test_order_tolerant_mixed(self):
        r = parse_animation("infinite slide 800ms alternate")
        assert r == {"name": "slide", "dur": 800, "loop": "pingpong"}

    def test_numeric_iteration_count_is_once(self):
        r = parse_animation("slide 1s 3")
        assert r == {"name": "slide", "dur": 1000, "loop": "once"}

    def test_fractional_seconds(self):
        r = parse_animation("slide 1.5s")
        assert r == {"name": "slide", "dur": 1500, "loop": "once"}

    def test_none_returns_none(self):
        assert parse_animation("none") is None

    def test_empty_returns_none(self):
        assert parse_animation("") is None
        assert parse_animation(None) is None

    def test_missing_name_returns_none(self):
        assert parse_animation("1s infinite") is None

    def test_missing_duration_returns_none(self):
        assert parse_animation("slide infinite") is None


# ---------------------------------------------------------------------------
# End-to-end: <style> + animated <div> → same .uib anim record as data-anim
# ---------------------------------------------------------------------------

# Reference: the Phase-1 data-anim version (known-good)
_DATA_ANIM_HTML = (
    '<div style="position:absolute;left:0;top:0;width:240px;height:200px;background-color:#1a1a2e;">'
    '<div style="position:absolute;left:10px;top:60px;width:30px;height:30px;background-color:#ff5050"'
    ' data-anim="x" data-from="10" data-to="200" data-dur="1000" data-loop="pingpong"></div>'
    '</div>'
)

# CSS equivalent we want Phase 2 to produce
_CSS_ANIM_HTML = (
    '<style>'
    '@keyframes slideX { from { left: 10px } to { left: 200px } }'
    '</style>'
    '<div style="position:absolute;left:0;top:0;width:240px;height:200px;background-color:#1a1a2e;">'
    '<div style="position:absolute;left:10px;top:60px;width:30px;height:30px;background-color:#ff5050;'
    'animation: slideX 1s infinite alternate"></div>'
    '</div>'
)


def _extract_anim_record(blob, nodes):
    """Return the raw bytes of the first (and only) anim record."""
    off = HEADER_SIZE + NODE_SIZE * len(nodes)
    return blob[off: off + ANIM_SIZE]


def test_css_anim_produces_same_record_as_data_anim():
    """CSS @keyframes + animation shorthand must yield the identical anim record as data-anim."""
    ref_nodes = parse_html(_DATA_ANIM_HTML, 240, 200)
    ref_blob = build_uib(ref_nodes, 240, 200)
    ref_record = _extract_anim_record(ref_blob, ref_nodes)

    css_nodes = parse_html(_CSS_ANIM_HTML, 240, 200)
    css_blob = build_uib(css_nodes, 240, 200)
    css_record = _extract_anim_record(css_blob, css_nodes)

    assert css_record == ref_record, (
        f"CSS anim record {css_record!r} != data-anim record {ref_record!r}"
    )


def test_css_anim_header_fields():
    nodes = parse_html(_CSS_ANIM_HTML, 240, 200)
    blob = build_uib(nodes, 240, 200)
    hdr = struct.unpack_from(HEADER_FMT, blob, 0)
    anim_count = hdr[7]
    assert anim_count == 1

    # strtab is past the anim table
    strtab_off = hdr[6]
    nodes_end = HEADER_SIZE + NODE_SIZE * len(nodes)
    assert strtab_off == nodes_end + ANIM_SIZE


def test_style_block_produces_no_text_node():
    """<style> content must NOT become a TEXT node."""
    nodes = parse_html(_CSS_ANIM_HTML, 240, 200)
    from htgl.html_tree import TEXT
    text_nodes = [n for n in nodes if n.type == TEXT]
    assert text_nodes == [], f"Expected no TEXT nodes but got: {text_nodes}"


def test_style_block_produces_no_extra_box_node():
    """<style> must not add extra BOX nodes either."""
    # _CSS_ANIM_HTML has 1 screen + 1 outer div + 1 inner div = 3 nodes
    nodes = parse_html(_CSS_ANIM_HTML, 240, 200)
    assert len(nodes) == 3, f"Expected 3 nodes, got {len(nodes)}: {[n.type for n in nodes]}"


def test_data_anim_wins_over_css_anim():
    """When both data-anim and CSS animation are present, data-anim takes precedence."""
    html = (
        '<style>'
        '@keyframes slideX { from { left: 5px } to { left: 99px } }'
        '</style>'
        '<div style="left:5px;top:5px;width:20px;height:20px;animation:slideX 2s infinite"'
        ' data-anim="x" data-from="10" data-to="200" data-dur="1000" data-loop="pingpong"></div>'
    )
    nodes = parse_html(html, 240, 200)
    # Find the animated div (index 1: 0=SCREEN, 1=div)
    animated = nodes[1]
    assert animated.anim is not None
    # data-anim values should win
    assert animated.anim["from"] == 10
    assert animated.anim["to"] == 200
    assert animated.anim["dur"] == 1000
    assert animated.anim["loop"] == "pingpong"


def test_y_axis_css_anim():
    """Test top→y mapping end-to-end."""
    html = (
        '<style>'
        '@keyframes moveY { from { top: 20px } to { top: 150px } }'
        '</style>'
        '<div style="position:absolute;left:110px;top:20px;width:30px;height:30px;'
        'animation: moveY 800ms infinite alternate"></div>'
    )
    nodes = parse_html(html, 240, 200)
    div = nodes[1]
    assert div.anim is not None
    assert div.anim["prop"] == "y"
    assert div.anim["from"] == 20
    assert div.anim["to"] == 150
    assert div.anim["dur"] == 800
    assert div.anim["loop"] == "pingpong"
