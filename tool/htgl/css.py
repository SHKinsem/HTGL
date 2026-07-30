"""Inline style string -> dict of whitelisted properties.

Length props are converted to int px. Color/keyword props stay strings.
Unknown properties are dropped (forward-compatible, browser-lenient) but recorded
as warnings when a Diagnostics is supplied.
"""

import re

from .diagnostics import warn

_PX_PROPS = {"left", "top", "width", "height", "font-size", "border-radius"}
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


def _to_opacity(value, diag=None):
    """Convert CSS opacity (0..1) to an 8-bit alpha value."""
    try:
        opacity = float(value.strip())
    except ValueError:
        warn(diag, "ignored opacity: '%s' (expected a number from 0 to 1)" % value.strip())
        return None
    if opacity < 0 or opacity > 1:
        warn(diag, "opacity %.3g clamped to the supported range 0..1" % opacity)
        opacity = min(1.0, max(0.0, opacity))
    return int(round(opacity * 255))


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
        elif name == "opacity":
            alpha = _to_opacity(raw, diag)
            if alpha is not None:
                out[name] = alpha
        elif name == "backdrop-filter":
            # A deliberately small subset: blur(0..8px). It maps to HTGL's
            # band-local glass filter; other browser filters are not portable.
            match = re.search(r"(?:^|\s)blur\s*\(\s*([^)]*)\s*\)", raw, re.I)
            if match:
                blur = _to_px(match.group(1), diag, name)
                if blur is not None:
                    if blur < 0 or blur > 8:
                        warn(diag, "backdrop blur %dpx clamped to 0..8px" % blur)
                    out["backdrop-blur"] = min(8, max(0, blur))
            else:
                warn(diag, "ignored backdrop-filter '%s' (only blur(Npx) is supported)" % raw)
        elif name not in _KNOWN_ELSEWHERE:
            warn(diag, "ignored unsupported CSS property '%s'" % name)
    return out
