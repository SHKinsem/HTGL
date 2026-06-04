#!/bin/sh
# Transpile the example, render it, and compare against golden artifacts.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
SIM="$BUILD/htgl_sim.exe"
[ -x "$SIM" ] || SIM="$BUILD/htgl_sim"
[ -x "$SIM" ] || SIM="$BUILD/Debug/htgl_sim.exe"

cd "$ROOT/tool"
python htgl.py "$ROOT/examples/hello.html" -o "$BUILD/e2e.uib"
cd "$ROOT"

# .uib must match byte-for-byte
cmp "$BUILD/e2e.uib" "$ROOT/tests/hello.uib.golden"

"$SIM" "$BUILD/e2e.uib" "$BUILD/e2e.png"

# PNG must match byte-for-byte (deterministic encoder + renderer)
cmp "$BUILD/e2e.png" "$ROOT/tests/hello.png.golden"
echo "e2e ok"
