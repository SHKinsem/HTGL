"""Inline style string -> dict of whitelisted properties.

Length props are converted to int px. Color/keyword props stay strings.
Unknown properties are dropped (forward-compatible, browser-lenient) but recorded
as warnings when a Diagnostics is supplied.
"""

from .diagnostics import warn

_PX_PROPS = {"left", "top", "width", "height", "font-size"}
_STR_PROPS = {"position", "background-color", "color"}
# Handled outside parse_style (resolved by html_tree); don't warn that they're "unsupported".
_KNOWN_ELSEWHERE = {"animation", "animation-timing-function"}


def _to_px(value, diag=None, prop=None):
    v = value.strip().lower()
    if v.endswith("px"):
        v = v[:-2].strip()
    try:
        return int(round(float(v)))
    except ValueError:
        warn(diag, "ignored %s: '%s' (only integer px is supported, "
             "not %%, em, rem, vh, calc(), auto)" % (prop or "length", value.strip()))
        return None


def parse_style(style, diag=None):
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
            px = _to_px(raw, diag, name)
            if px is not None:
                out[name] = px
        elif name in _STR_PROPS:
            out[name] = raw
        elif name not in _KNOWN_ELSEWHERE:
            warn(diag, "ignored unsupported CSS property '%s'" % name)
    return out
