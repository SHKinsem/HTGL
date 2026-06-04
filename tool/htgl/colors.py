"""Color string -> RGB565."""

_NAMED = {
    "black": (0, 0, 0), "white": (255, 255, 255),
    "red": (255, 0, 0), "green": (0, 128, 0), "blue": (0, 0, 255),
    "lime": (0, 255, 0), "gray": (128, 128, 128), "grey": (128, 128, 128),
    "yellow": (255, 255, 0), "cyan": (0, 255, 255), "magenta": (255, 0, 255),
    "silver": (192, 192, 192), "navy": (0, 0, 128),
}


def _pack(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def to_rgb565(value):
    """Parse a CSS-ish color into a 16-bit RGB565 int. Unknown -> black."""
    s = value.strip().lower()
    if s in _NAMED:
        return _pack(*_NAMED[s])
    if s.startswith("#"):
        h = s[1:]
        if len(h) == 3:
            try:
                r = int(h[0] * 2, 16)
                g = int(h[1] * 2, 16)
                b = int(h[2] * 2, 16)
                return _pack(r, g, b)
            except ValueError:
                return 0x0000
        if len(h) == 6:
            try:
                r = int(h[0:2], 16)
                g = int(h[2:4], 16)
                b = int(h[4:6], 16)
                return _pack(r, g, b)
            except ValueError:
                return 0x0000
    return 0x0000
