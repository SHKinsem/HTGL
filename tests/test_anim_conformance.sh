#!/bin/sh
# Cross-language anim conformance: the Python tool emits an anim-bearing .uib with
# the real build_uib, and the C engine must decode + interpolate it identically.
# Closes the one gap the e2e golden leaves (hello.html has no animations).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
PROBE="$BUILD/htgl_anim_probe.exe"
[ -x "$PROBE" ] || PROBE="$BUILD/htgl_anim_probe"
UIB="$BUILD/anim_conf.uib"

python "$ROOT/tests/gen_anim_uib.py" "$UIB"
"$PROBE" "$UIB"
echo "anim conformance ok"
