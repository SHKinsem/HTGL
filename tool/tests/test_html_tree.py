from htgl.html_tree import parse_html, SCREEN, BOX, TEXT

HTML = """
<div style="position:absolute; left:0; top:0; width:240px; height:320px; background-color:#fff">
  <div style="position:absolute; left:10px; top:10px; width:100px; height:40px; background-color:#f00">
    <div style="position:absolute; left:5px; top:12px; color:#000; font-size:8px">Hi</div>
  </div>
</div>
"""

def test_root_is_screen():
    nodes = parse_html(HTML, screen_w=240, screen_h=320)
    assert nodes[0].type == SCREEN
    assert nodes[0].parent == 0xFFFF
    assert nodes[0].w == 240 and nodes[0].h == 320

def test_box_nesting_and_parent_indices():
    nodes = parse_html(HTML, screen_w=240, screen_h=320)
    boxes = [n for n in nodes if n.type == BOX]
    assert len(boxes) == 3
    outer = boxes[0]
    assert outer.parent == 0          # child of screen
    assert outer.w == 240 and outer.h == 320
    inner = boxes[1]
    assert inner.parent == nodes.index(outer)
    assert inner.x == 10 and inner.y == 10
    assert inner.bg == 0xF800

def test_text_node_carries_string_and_fg():
    nodes = parse_html(HTML, screen_w=240, screen_h=320)
    texts = [n for n in nodes if n.type == TEXT]
    assert len(texts) == 1
    assert texts[0].text == "Hi"
    assert texts[0].fg == 0x0000
    assert texts[0].font_size == 8

def test_defaults_when_unstyled():
    nodes = parse_html('<div></div>', screen_w=100, screen_h=100)
    box = [n for n in nodes if n.type == BOX][0]
    assert box.x == 0 and box.y == 0 and box.w == 0 and box.h == 0
    assert box.bg == 0x0000
