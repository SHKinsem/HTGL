#!/bin/sh
# Measure the HTGL engine's flash/RAM footprint for a low-end MCU, with NO hardware.
#
# The engine (engine/*.c) is platform-independent C99, so we can cross-compile it for
# the thesis target — STM32F1 is a Cortex-M3 — and read the section sizes. This turns
# "fits a low-end MCU" from an assertion into a reproducible number.
#
# Requires arm-none-eabi-gcc + arm-none-eabi-size on PATH (GNU Arm Embedded toolchain).
# Usage:  sh scripts/measure_footprint.sh  [cpu]   (cpu defaults to cortex-m3)
set -eu

CPU="${1:-cortex-m3}"
CC=arm-none-eabi-gcc
SIZE=arm-none-eabi-size
CFLAGS="-mcpu=$CPU -mthumb -Os -ffunction-sections -fdata-sections -Wall"

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

echo "HTGL engine footprint  ($($CC -dumpmachine) $($CC -dumpversion), -mcpu=$CPU -Os)"
echo

# 1) Code + constants (flash) and static data (RAM) of the engine itself.
#    Berkeley `size`: text = .text + .rodata (flash), data = .data (RAM init),
#    bss = .bss (RAM zero-init). The 8x8 font is `const`, so it counts under text.
$CC $CFLAGS -c engine/htgl.c -o "$OUT/htgl.o"
$CC $CFLAGS -c engine/draw.c -o "$OUT/draw.o"
$SIZE -t "$OUT/htgl.o" "$OUT/draw.o"
echo

# 2) Per-context RAM: one htgl_ctx instance, measured as real ARM .bss (host sizeof
#    would be wrong — ARM pointers are 4 bytes, not 8). Dominated by the
#    HTGL_MAX_NODES-sized runtime arrays (abs_*/cur_*).
cat > "$OUT/ctx.c" <<'EOF'
#include "htgl_internal.h"
struct htgl_ctx htgl_one_ctx;   /* a single instance -> lands in .bss */
EOF
$CC $CFLAGS -I engine -c "$OUT/ctx.c" -o "$OUT/ctx.o"
CTX_BSS=$($SIZE "$OUT/ctx.o" | awk 'NR==2 {print $3}')
echo "Per-context RAM (sizeof htgl_ctx, ARM .bss):  ${CTX_BSS} bytes  [HTGL_MAX_NODES=256]"
echo "Line buffer (caller-owned, RGB565):           screen_w * band_rows * 2 bytes"
echo "                                              e.g. 240px x 1 row = 480 bytes"
