"""HTML subset -> flat Node tree (the transpiler IR).

Rules (Milestone 1):
- A synthetic SCREEN node is index 0, sized screen_w x screen_h.
- Every <div> becomes a BOX node.
- Any text inside a <div> becomes a TEXT node, child of that div.
- Geometry/colors come from the element's inline style (see css.parse_style).
"""

from html.parser import HTMLParser

from .colors import to_rgb565
from .css import parse_style

SCREEN = 0
BOX = 1
TEXT = 2

ROOT_PARENT = 0xFFFF

_ANIM_PROPS = ("x", "y", "w", "h")


def _int(value, default):
    try:
        return int(round(float(str(value).strip())))
    except (ValueError, TypeError):
        return default


def _parse_anim(attrs):
    """Read data-anim/* attributes into an anim dict, or None if absent/invalid."""
    prop = attrs.get("data-anim")
    if prop not in _ANIM_PROPS:
        return None
    return {
        "prop": prop,
        "from": _int(attrs.get("data-from"), 0),
        "to": _int(attrs.get("data-to"), 0),
        "dur": max(1, _int(attrs.get("data-dur"), 1000)),
        "loop": attrs.get("data-loop", "once"),
    }


class Node:
    def __init__(self, type, parent):
        self.type = type
        self.parent = parent
        self.x = 0
        self.y = 0
        self.w = 0
        self.h = 0
        self.bg = 0x0000
        self.fg = 0x0000
        self.font_size = 8
        self.text = None
        self.anim = None  # dict(prop, from, to, dur, loop) or None


class _Builder(HTMLParser):
    def __init__(self, nodes, stack):
        super().__init__(convert_charrefs=True)
        self.nodes = nodes
        self.stack = stack  # stack of node indices; top is current parent

    def handle_starttag(self, tag, attrs):
        if tag != "div":
            return  # ignore unsupported tags, keep their children inline
        parent_idx = self.stack[-1]
        node = Node(BOX, parent_idx)
        style = dict(attrs).get("style", "")
        props = parse_style(style)
        node.x = props.get("left", 0)
        node.y = props.get("top", 0)
        node.w = props.get("width", 0)
        node.h = props.get("height", 0)
        if "background-color" in props:
            node.bg = to_rgb565(props["background-color"])
        if "color" in props:
            node.fg = to_rgb565(props["color"])
        node.font_size = props.get("font-size", 8)
        node.anim = _parse_anim(dict(attrs))
        self.nodes.append(node)
        self.stack.append(len(self.nodes) - 1)

    def handle_endtag(self, tag):
        if tag != "div":
            return
        if len(self.stack) > 1:
            self.stack.pop()

    def handle_data(self, data):
        text = data.strip()
        if not text:
            return
        parent_idx = self.stack[-1]
        parent = self.nodes[parent_idx]
        node = Node(TEXT, parent_idx)
        node.text = text
        node.fg = parent.fg
        node.font_size = parent.font_size
        self.nodes.append(node)


def parse_html(html, screen_w, screen_h):
    screen = Node(SCREEN, ROOT_PARENT)
    screen.w = screen_w
    screen.h = screen_h
    nodes = [screen]
    stack = [0]
    _Builder(nodes, stack).feed(html)
    return nodes
