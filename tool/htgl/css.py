"""Inline style string -> dict of whitelisted properties.

Length props are converted to int px. Color/keyword props stay strings.
Unknown properties are dropped (forward-compatible, browser-lenient).
"""

_PX_PROPS = {"left", "top", "width", "height", "font-size"}
_STR_PROPS = {"position", "background-color", "color"}


def _to_px(value):
    v = value.strip().lower()
    if v.endswith("px"):
        v = v[:-2].strip()
    try:
        return int(round(float(v)))
    except ValueError:
        return None


def parse_style(style):
    out = {}
    for decl in style.split(";"):
        if ":" not in decl:
            continue
        name, _, raw = decl.partition(":")
        name = name.strip().lower()
        raw = raw.strip()
        if not name or not raw:
            continue
        if name in _PX_PROPS:
            px = _to_px(raw)
            if px is not None:
                out[name] = px
        elif name in _STR_PROPS:
            out[name] = raw
    return out
