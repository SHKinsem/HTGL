"""Color string -> RGB565."""

import re

from .diagnostics import warn

_NAMED = {
    "black": (0, 0, 0), "white": (255, 255, 255),
    "red": (255, 0, 0), "green": (0, 128, 0), "blue": (0, 0, 255),
    "lime": (0, 255, 0), "gray": (128, 128, 128), "grey": (128, 128, 128),
    "yellow": (255, 255, 0), "cyan": (0, 255, 255), "magenta": (255, 0, 255),
    "silver": (192, 192, 192), "navy": (0, 0, 128),
}

# rgb(r, g, b) / rgba(r, g, b, a) with 0..255 integer channels (alpha ignored).
_RGB_RE = re.compile(
    r"^rgba?\(\s*(\d{1,3})\s*,\s*(\d{1,3})\s*,\s*(\d{1,3})\s*(?:,\s*[\d.]+\s*)?\)$"
)


def _pack(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def to_rgb565(value, diag=None):
    """Parse a CSS-ish color into a 16-bit RGB565 int.

    Accepts #rgb, #rrggbb, rgb()/rgba(), and a small named-color whitelist.
    Anything else returns black (0x0000) and records a warning if `diag` is given.
    """
    s = value.strip().lower()
    if s in _NAMED:
        return _pack(*_NAMED[s])
    if s.startswith("#"):
        h = s[1:]
        try:
            if len(h) == 3:
                return _pack(int(h[0] * 2, 16), int(h[1] * 2, 16), int(h[2] * 2, 16))
            if len(h) == 6:
                return _pack(int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))
        except ValueError:
            pass  # malformed hex -> fall through to the warning below
    else:
        m = _RGB_RE.match(s)
        if m:
            r, g, b = (min(255, int(m.group(i))) for i in (1, 2, 3))
            if s.startswith("rgba"):
                warn(diag, "color '%s': alpha is ignored (HTGL has no transparency)"
                     % value.strip())
            return _pack(r, g, b)
    warn(diag, "unrecognized color '%s' -> black; use #rgb / #rrggbb / rgb() / rgba() "
         "or a named color" % value.strip())
    return 0x0000
