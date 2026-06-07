"""CSS @keyframes / animation shorthand -> anim dict for HTGL transpiler (Phase 2).

Supported subset:
- @keyframes <name> { from { <prop>: <val> } to { <prop>: <val> } }
  Also accepts 0% as "from" and 100% as "to".
  Exactly ONE animated property per keyframe block (left/top/width/height/background-color).
  Property map: left→x  top→y  width→w  height→h  background-color→bg
  Values are px integers (strip "px") for geometry; color strings for background-color.

- Inline animation shorthand: animation: <name> <duration> [<iteration-count>] [<direction>]
  - name: identifies @keyframes rule
  - duration: 1s → 1000ms, 500ms → 500ms
  - iteration-count: "infinite" present → looping; absent or a number → "once"
  - direction: "alternate" present → pingpong mode
  - Combined loop:  infinite + alternate → pingpong
                    infinite (no alternate) → loop
                    neither → once

Returns None for any input that cannot be parsed within the subset.
"""

import re

from .colors import to_rgb565
from .diagnostics import warn

# Properties we can animate and their HTGL axis codes
_CSS_PROP_MAP = {
    "left": "x",
    "top": "y",
    "width": "w",
    "height": "h",
    "background-color": "bg",
}

# Color-valued properties (parsed via to_rgb565 instead of _parse_px)
_COLOR_PROPS = {"background-color"}


def _strip_comments(css):
    """Remove /* ... */ comments from CSS text."""
    return re.sub(r"/\*.*?\*/", "", css, flags=re.DOTALL)


def _parse_px(value):
    """Parse a px integer from a CSS value string, e.g. '10px' → 10. Returns None on failure."""
    v = value.strip().lower()
    if v.endswith("px"):
        v = v[:-2].strip()
    try:
        return int(round(float(v)))
    except (ValueError, TypeError):
        return None


def _parse_block_props(block_text, diag=None):
    """Parse declarations inside a keyframe stop block, return {prop: value, ...}.

    Geometry properties (left/top/width/height) return int px values.
    Color properties (background-color) return int RGB565 values.
    """
    props = {}
    for decl in block_text.split(";"):
        if ":" not in decl:
            continue
        name, _, raw = decl.partition(":")
        name = name.strip().lower()
        raw = raw.strip()
        if name in _CSS_PROP_MAP:
            if name in _COLOR_PROPS:
                val = to_rgb565(raw, diag)
                props[name] = val
            else:
                val = _parse_px(raw)
                if val is not None:
                    props[name] = val
    return props


def _extract_keyframe_bodies(css_text):
    """Yield (name, body) pairs for each @keyframes rule, handling nested braces correctly."""
    # Find @keyframes <name> followed by the first '{', then balance braces.
    header_re = re.compile(r"@keyframes\s+([\w-]+)\s*\{", re.IGNORECASE)
    pos = 0
    while True:
        m = header_re.search(css_text, pos)
        if not m:
            break
        name = m.group(1)
        brace_start = m.end() - 1  # points at the opening '{'
        # Balance braces to find the matching '}'
        depth = 0
        i = brace_start
        while i < len(css_text):
            if css_text[i] == "{":
                depth += 1
            elif css_text[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        body = css_text[brace_start + 1: i]  # content between the outer braces
        yield name, body
        pos = i + 1


def parse_keyframes(css_text, diag=None):
    """Parse all @keyframes rules in *css_text*.

    Returns a dict mapping animation name → {"prop": "x"|"y"|"w"|"h", "from": int, "to": int}.
    Rules that don't reduce to a single supported animated property are skipped, with
    a warning recorded if `diag` is supplied.
    """
    css_text = _strip_comments(css_text)
    result = {}

    for name, body in _extract_keyframe_bodies(css_text):

        # Find individual keyframe stops: "from { ... }", "to { ... }", "0% { ... }", "100% { ... }"
        # Use (?<!\d) to prevent "0%" from matching inside "50%", etc.
        stop_pattern = re.compile(
            r"(?<!\d)(from|to|(?<!\d)0%|100%)\s*\{([^}]*)\}",
            re.DOTALL | re.IGNORECASE,
        )
        stops = {}  # "from"/"to" → {css_prop: int_px}
        for sm in stop_pattern.finditer(body):
            key = sm.group(1).strip().lower()
            # Normalise 0% → from, 100% → to
            if key == "0%":
                key = "from"
            elif key == "100%":
                key = "to"
            stops[key] = _parse_block_props(sm.group(2), diag)

        if "from" not in stops or "to" not in stops:
            warn(diag, "@keyframes '%s' ignored: needs both 'from' and 'to' (or 0%%/100%%)"
                 % name)
            continue  # need both endpoints

        from_props = stops["from"]
        to_props = stops["to"]

        # Find a single shared supported property
        shared = set(from_props) & set(to_props) & set(_CSS_PROP_MAP)
        if len(shared) != 1:
            warn(diag, "@keyframes '%s' ignored: HTGL animates exactly one of "
                 "left/top/width/height/background-color per keyframe (found %d: %s)"
                 % (name, len(shared), sorted(shared)))
            continue  # need exactly one animated property

        css_prop = next(iter(shared))
        result[name] = {
            "prop": _CSS_PROP_MAP[css_prop],
            "from": from_props[css_prop],
            "to": to_props[css_prop],
        }

    return result


# Tokens that are NOT the animation name and NOT a duration
_ITERATION_KEYWORDS = {"infinite", "once"}
_DIRECTION_KEYWORDS = {
    "normal", "reverse", "alternate", "alternate-reverse",
}
_FILL_KEYWORDS = {"none", "forwards", "backwards", "both"}
_TIMING_KEYWORDS = {
    "ease", "linear", "ease-in", "ease-out", "ease-in-out", "step-start", "step-end",
}
_PLAY_KEYWORDS = {"running", "paused"}

# All non-name, non-duration keywords (used to detect the name token)
_NON_NAME_KEYWORDS = (
    _ITERATION_KEYWORDS | _DIRECTION_KEYWORDS | _FILL_KEYWORDS |
    _TIMING_KEYWORDS | _PLAY_KEYWORDS
)


def _parse_duration(token):
    """Parse a CSS time token ('1s', '500ms') to integer ms, or return None."""
    t = token.strip().lower()
    if t.endswith("ms"):
        try:
            return int(round(float(t[:-2])))
        except ValueError:
            return None
    if t.endswith("s"):
        try:
            return int(round(float(t[:-1]) * 1000))
        except ValueError:
            return None
    return None


# CSS timing-function → HTGL ease name
# CSS "ease" (slow start, slow end) is closest to our ease-in-out
_CSS_TIMING_MAP = {
    "linear": "linear",
    "ease": "ease-in-out",
    "ease-in": "ease-in",
    "ease-out": "ease-out",
    "ease-in-out": "ease-in-out",
}


def parse_animation(value, timing_function=None, diag=None):
    """Parse an inline animation shorthand value.

    e.g. "slide 1s infinite alternate" or "bounce 500ms"

    timing_function: optional CSS animation-timing-function value (e.g. "ease-in").
    If None, timing keywords embedded in the shorthand are used; default is "linear".

    Returns {"name": str, "dur": int_ms, "loop": "once"|"loop"|"pingpong",
             "ease": "linear"|"ease-in"|"ease-out"|"ease-in-out"} or None. A value that
    can't be parsed within the subset records a warning if `diag` is supplied.
    """
    if not value:
        return None

    tokens = value.strip().split()
    if not tokens:
        return None

    name = None
    dur = None
    has_infinite = False
    has_alternate = False
    ease_from_shorthand = None  # timing keyword found in the shorthand tokens

    for token in tokens:
        t = token.lower()

        # Skip "none" for the whole shorthand
        if t == "none":
            return None

        # Duration
        d = _parse_duration(token)
        if d is not None:
            if dur is None:
                dur = d  # first time value = animation-duration
            # second time value would be delay — we ignore it
            continue

        # Iteration count keywords / numbers
        if t == "infinite":
            has_infinite = True
            continue
        if re.match(r"^\d+(\.\d+)?$", t):
            # numeric iteration count — treated as "once" for MVP
            continue

        # Direction keywords
        if t == "alternate" or t == "alternate-reverse":
            has_alternate = True
            continue
        if t in _DIRECTION_KEYWORDS:
            continue

        # Timing function keywords (handled separately from name detection)
        if t in _CSS_TIMING_MAP:
            ease_from_shorthand = _CSS_TIMING_MAP[t]
            continue

        # Other non-name keywords (step-start, step-end, running, paused, fill keywords, etc.)
        if t in _NON_NAME_KEYWORDS:
            continue

        # Anything else is treated as the animation name
        if name is None:
            name = token  # preserve original case

    if name is None or dur is None:
        warn(diag, "animation '%s' ignored: needs an @keyframes name and a duration "
             "(e.g. 'slide 1s')" % value.strip())
        return None

    # Resolve loop mode
    if has_infinite and has_alternate:
        loop = "pingpong"
    elif has_infinite:
        loop = "loop"
    else:
        loop = "once"

    # Resolve ease: explicit timing_function arg > shorthand keyword > default linear
    if timing_function is not None:
        ease = _CSS_TIMING_MAP.get(timing_function.strip().lower(), "linear")
    elif ease_from_shorthand is not None:
        ease = ease_from_shorthand
    else:
        ease = "linear"

    return {"name": name, "dur": dur, "loop": loop, "ease": ease}
