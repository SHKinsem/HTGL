"""Transpiler diagnostics: rgb() parsing, warnings on silent drops, the int16
crash fix, and --strict. All transpiler functions take an optional diag; passing
none must preserve the original silent behavior (covered by the other suites)."""
import struct

from htgl import cli
from htgl.colors import to_rgb565
from htgl.css import parse_style
from htgl.cssanim import parse_keyframes, parse_animation
from htgl.diagnostics import Diagnostics
from htgl.html_tree import parse_html
from htgl.uib import build_uib, NODE_FMT, HEADER_SIZE


# --- rgb()/rgba(): valid CSS that used to silently become black ---

def test_rgb_parsing():
    assert to_rgb565("rgb(255,0,0)") == 0xF800
    assert to_rgb565("rgb(0, 255, 0)") == 0x07E0
    assert to_rgb565("rgb(0,0,255)") == 0x001F
    assert to_rgb565("rgb(255, 255, 255)") == 0xFFFF


def test_rgba_parses_and_warns_alpha():
    d = Diagnostics()
    assert to_rgb565("rgba(0,0,255,0.5)", d) == 0x001F
    assert any("alpha" in w for w in d.warnings)


# --- the silent-black trap now warns ---

def test_unknown_color_warns():
    d = Diagnostics()
    assert to_rgb565("orange", d) == 0x0000
    assert any("orange" in w for w in d.warnings)


def test_malformed_hex_warns():
    d = Diagnostics()
    assert to_rgb565("#1234", d) == 0x0000
    assert len(d.warnings) == 1


def test_known_color_no_warning():
    d = Diagnostics()
    to_rgb565("red", d)
    to_rgb565("#ff0000", d)
    assert d.warnings == []


# --- dropped units / unsupported properties ---

def test_dropped_unit_warns():
    d = Diagnostics()
    out = parse_style("width:50%", d)
    assert "width" not in out
    assert any("width" in w for w in d.warnings)


def test_unsupported_property_warns():
    d = Diagnostics()
    parse_style("border:1px solid red", d)
    assert any("border" in w for w in d.warnings)


def test_animation_property_not_warned():
    d = Diagnostics()
    parse_style("animation:slide 1s infinite", d)
    assert d.warnings == []   # resolved by html_tree, not "unsupported"


def test_valid_px_no_warning():
    d = Diagnostics()
    parse_style("left:10px; width:30px", d)
    assert d.warnings == []


# --- the crash fix: an oversized coordinate clamps instead of raising ---

def test_oversized_coordinate_clamps_not_crashes():
    d = Diagnostics()
    nodes = parse_html('<div style="left:400000px;width:10px;height:10px"></div>', 100, 100, d)
    blob = build_uib(nodes, 100, 100, d)   # must not raise struct.error
    rec = struct.unpack_from(NODE_FMT, blob, HEADER_SIZE + 18 * 1)
    assert rec[3] == 32767                 # x is field index 3 (type,font,parent,x,...)
    assert any("clamp" in w.lower() for w in d.warnings)


# --- non-ASCII text + font-size granularity ---

def test_non_ascii_text_warns():
    d = Diagnostics()
    nodes = parse_html('<div>café</div>', 100, 100, d)
    build_uib(nodes, 100, 100, d)
    assert any("non-ASCII" in w for w in d.warnings)


def test_font_size_granularity_warns():
    d = Diagnostics()
    nodes = parse_html('<div style="font-size:14px">Hi</div>', 100, 100, d)
    build_uib(nodes, 100, 100, d)
    assert any("snapped" in w for w in d.warnings)


# --- diag=None preserves silent behavior (no crash, no warnings recorded) ---

def test_no_diag_is_silent_and_unchanged():
    assert to_rgb565("not-a-color") == 0x0000
    assert parse_style("border:1px") == {}
    nodes = parse_html('<div style="left:99999px;width:5px;height:5px"></div>', 50, 50)
    build_uib(nodes, 50, 50)   # must not raise


# --- cli --strict ---

def test_cli_strict_fails_on_warning(tmp_path):
    html = tmp_path / "in.html"
    html.write_text('<div style="width:50%;height:10px;border:1px"></div>')
    uib = tmp_path / "out.uib"
    assert cli.main([str(html), "-o", str(uib)]) == 0              # warnings don't fail by default
    assert cli.main([str(html), "-o", str(uib), "--strict"]) == 1  # --strict -> non-zero


# --- CSS @keyframes / animation silent failures now warn ---

def test_keyframes_missing_endpoint_warns():
    d = Diagnostics()
    parse_keyframes("@keyframes slide { from { left: 0px } }", d)   # no 'to'
    assert any("slide" in w and "from" in w and "to" in w for w in d.warnings)


def test_keyframes_two_properties_warns():
    d = Diagnostics()
    parse_keyframes("@keyframes m { from { left:0px; top:0px } to { left:9px; top:9px } }", d)
    assert any("exactly one" in w for w in d.warnings)


def test_keyframes_valid_no_warning():
    d = Diagnostics()
    kf = parse_keyframes("@keyframes slide { from { left: 0px } to { left: 100px } }", d)
    assert d.warnings == []
    assert kf["slide"]["prop"] == "x"


def test_animation_missing_duration_warns():
    d = Diagnostics()
    assert parse_animation("slide", diag=d) is None
    assert any("duration" in w for w in d.warnings)


def test_animation_valid_no_warning():
    d = Diagnostics()
    parsed = parse_animation("slide 1s infinite alternate", diag=d)
    assert d.warnings == []
    assert parsed["loop"] == "pingpong"


def test_css_animation_unknown_keyframes_warns():
    # an `animation:` that references a @keyframes that doesn't exist -> warned, not silent
    d = Diagnostics()
    html = ('<div style="left:0;top:0;width:100px;height:100px">'
            '<div style="left:0;top:0;width:10px;height:10px;animation: slide 1s infinite">'
            '</div></div>')
    parse_html(html, 100, 100, d)
    assert any("slide" in w and "keyframes" in w for w in d.warnings)


def test_css_animation_valid_sets_anim_no_warning():
    d = Diagnostics()
    html = ('<style>@keyframes slide { from { left: 0px } to { left: 100px } }</style>'
            '<div style="left:0;top:0;width:100px;height:100px">'
            '<div style="left:0;top:0;width:10px;height:10px;animation: slide 1s infinite">'
            '</div></div>')
    nodes = parse_html(html, 100, 100, d)
    assert d.warnings == []
    box = nodes[2]                       # screen(0), outer box(1), inner box(2)
    assert box.anim is not None and box.anim["prop"] == "x"
