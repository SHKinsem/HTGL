from htgl.css import parse_style

def test_parses_known_props():
    s = "position:absolute; left:10px; top:20px; width:100px; height:30px;"
    out = parse_style(s)
    assert out == {
        "position": "absolute",
        "left": 10, "top": 20, "width": 100, "height": 30,
    }

def test_colors_kept_as_strings():
    out = parse_style("background-color:#f00; color: white")
    assert out["background-color"] == "#f00"
    assert out["color"] == "white"

def test_font_size_px_to_int():
    assert parse_style("font-size:16px")["font-size"] == 16

def test_unknown_props_ignored():
    out = parse_style("z-index:5; left:3px")
    assert "z-index" not in out
    assert out["left"] == 3

def test_empty_and_malformed_safe():
    assert parse_style("") == {}
    assert parse_style("garbage;;;:") == {}


def test_parses_rounded_glass_style_subset():
    out = parse_style("border-radius:18px; backdrop-filter: blur(3px)")
    assert out["border-radius"] == 18
    assert out["backdrop-blur"] == 3
