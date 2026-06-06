"""HTML subset -> flat Node tree (the transpiler IR).

Rules (Milestone 1):
- A synthetic SCREEN node is index 0, sized screen_w x screen_h.
- Every <div> becomes a BOX node.
- Any text inside a <div> becomes a TEXT node, child of that div.
- Geometry/colors come from the element's inline style (see css.parse_style).

Phase 2 — CSS animation:
- A <style> block with @keyframes and inline animation: shorthand is supported.
- <style> content is collected as CSS, NOT turned into TEXT nodes.
- data-anim wins over CSS animation when both are present on the same element.
"""

from html.parser import HTMLParser

from .colors import to_rgb565
from .css import parse_style
from .cssanim import parse_keyframes, parse_animation

SCREEN = 0
BOX = 1
TEXT = 2

ROOT_PARENT = 0xFFFF

_ANIM_PROPS = ("x", "y", "w", "h", "bg")
_EASE_VALUES = ("linear", "ease-in", "ease-out", "ease-in-out")


def _int(value, default):
    try:
        return int(round(float(str(value).strip())))
    except (ValueError, TypeError):
        return default


def _parse_anim(attrs, diag=None):
    """Read data-anim/* attributes into an anim dict, or None if absent/invalid."""
    prop = attrs.get("data-anim")
    if prop not in _ANIM_PROPS:
        return None
    raw_ease = attrs.get("data-ease", "linear").strip().lower()
    ease = raw_ease if raw_ease in _EASE_VALUES else "linear"
    if prop == "bg":
        # from/to are color values parsed via to_rgb565 (returned as uint16)
        from_val = to_rgb565(attrs.get("data-from", "black"), diag)
        to_val   = to_rgb565(attrs.get("data-to",   "black"), diag)
    else:
        from_val = _int(attrs.get("data-from"), 0)
        to_val   = _int(attrs.get("data-to"),   0)
    return {
        "prop": prop,
        "from": from_val,
        "to":   to_val,
        "dur":  max(1, _int(attrs.get("data-dur"), 1000)),
        "loop": attrs.get("data-loop", "once"),
        "ease": ease,
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
        self.tap = 0      # data-tap id (1..255); 0 = not interactive


class _Builder(HTMLParser):
    def __init__(self, nodes, stack, diag=None):
        super().__init__(convert_charrefs=True)
        self.nodes = nodes
        self.stack = stack  # stack of node indices; top is current parent
        self.diag = diag
        self._in_style = False   # True while inside <style>...</style>
        self._in_script = False  # True while inside <script>...</script>
        self._style_chunks = []  # CSS text collected from <style> blocks
        # Per-node raw "animation" inline style values, indexed by node list position
        # We store them during parsing and resolve after feed() completes.
        self._pending_anim = []  # list of (node_index, raw_animation_value, timing_function_or_None)

    def handle_starttag(self, tag, attrs):
        if tag == "style":
            self._in_style = True
            return
        if tag == "script":
            self._in_script = True
            return
        if tag != "div":
            return  # ignore unsupported tags, keep their children inline
        parent_idx = self.stack[-1]
        node = Node(BOX, parent_idx)
        attr_dict = dict(attrs)
        style = attr_dict.get("style", "")
        props = parse_style(style, self.diag)
        node.x = props.get("left", 0)
        node.y = props.get("top", 0)
        node.w = props.get("width", 0)
        node.h = props.get("height", 0)
        if "background-color" in props:
            node.bg = to_rgb565(props["background-color"], self.diag)
        if "color" in props:
            node.fg = to_rgb565(props["color"], self.diag)
        node.font_size = props.get("font-size", 8)
        node.anim = _parse_anim(attr_dict, self.diag)  # Phase-1 data-anim (wins if present)
        # Parse data-tap: store tap id (1..255) or 0 if absent/invalid/out-of-range
        try:
            tap_val = int(attr_dict.get("data-tap", "0"))
        except (ValueError, TypeError):
            tap_val = 0
        node.tap = tap_val if 1 <= tap_val <= 255 else 0
        node_idx = len(self.nodes)
        self.nodes.append(node)
        self.stack.append(node_idx)
        # Collect raw animation value for Phase-2 resolution (done after feed())
        raw_anim = props.get("animation") or _parse_inline_animation_from_style(style)
        if raw_anim:
            timing_fn = _parse_timing_function_from_style(style)
            self._pending_anim.append((node_idx, raw_anim, timing_fn))

    def handle_endtag(self, tag):
        if tag == "style":
            self._in_style = False
            return
        if tag == "script":
            self._in_script = False
            return
        if tag != "div":
            return
        if len(self.stack) > 1:
            self.stack.pop()

    def handle_data(self, data):
        # Suppress text inside <style> and <script> — do NOT create TEXT nodes for them.
        if self._in_style:
            self._style_chunks.append(data)
            return
        if self._in_script:
            return
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

    def resolve_css_anims(self):
        """Call after feed() to wire CSS @keyframes animations onto nodes.

        Only sets node.anim when not already set by data-anim (Phase-1 wins).
        """
        if not self._pending_anim:
            return
        css_text = "".join(self._style_chunks)
        if not css_text.strip():
            return
        keyframes = parse_keyframes(css_text)
        if not keyframes:
            return
        for node_idx, raw_anim, timing_fn in self._pending_anim:
            node = self.nodes[node_idx]
            if node.anim is not None:
                continue  # data-anim wins — skip
            parsed = parse_animation(raw_anim, timing_function=timing_fn)
            if parsed is None:
                continue
            kf = keyframes.get(parsed["name"])
            if kf is None:
                continue
            node.anim = {
                "prop": kf["prop"],
                "from": kf["from"],
                "to": kf["to"],
                "dur": max(1, parsed["dur"]),
                "loop": parsed["loop"],
                "ease": parsed["ease"],
            }


def _parse_inline_animation_from_style(style):
    """Extract the raw value of the 'animation' property from an inline style string.

    Returns the raw value string, or None if not present.
    The css.parse_style function only handles known whitelisted properties; this
    function handles 'animation' directly.
    """
    for decl in style.split(";"):
        if ":" not in decl:
            continue
        name, _, raw = decl.partition(":")
        if name.strip().lower() == "animation":
            return raw.strip()
    return None


def _parse_timing_function_from_style(style):
    """Extract the raw value of 'animation-timing-function' from an inline style string.

    Returns the value string, or None if not present.
    """
    for decl in style.split(";"):
        if ":" not in decl:
            continue
        name, _, raw = decl.partition(":")
        if name.strip().lower() == "animation-timing-function":
            return raw.strip()
    return None


def parse_html(html, screen_w, screen_h, diag=None):
    screen = Node(SCREEN, ROOT_PARENT)
    screen.w = screen_w
    screen.h = screen_h
    nodes = [screen]
    stack = [0]
    builder = _Builder(nodes, stack, diag)
    builder.feed(html)
    builder.resolve_css_anims()
    return nodes
