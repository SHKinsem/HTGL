# Contributing to HTGL

Thanks for your interest! HTGL is MIT-licensed and contributions are welcome. This guide
covers the dev setup, how to build and test, and the few places you touch to extend the
framework.

By contributing you agree your work is licensed under the project's [MIT License](LICENSE).

## What HTGL is (30-second version)

You author a UI in an HTML/CSS *subset* → a Python transpiler (`tool/`) compiles it to a compact
little-endian `.uib` binary → a portable C99 engine (`engine/`) validates and renders it in
horizontal bands. Ports live in `port/` (a host PNG simulator and an ESP32 board). See
[`README.md`](README.md) for the pitch and [`docs/USAGE.md`](docs/USAGE.md) for the exhaustive
reference (supported subset, CLI flags, C API + return codes, the `.uib` format).

```
tool/      Python transpiler:  HTML/CSS subset -> .uib (+ optional C array)
engine/    Portable C99 engine (NO platform code): htgl.c (load/layout/render/tick), draw.c
port/      sim/ (host -> PNG)  ·  esp32/ (ST7789 via TFT_eSPI)
tests/     C unit tests + golden .uib/.png + the Python<->C anim conformance harness
tool/tests/  pytest suites for the transpiler
docs/      USAGE.md reference + docs/superpowers/ design spec & plans
```

## Prerequisites

- **Python 3** (standard library only — no third-party runtime deps) + `pytest` for tests.
- A **C99 compiler** and **CMake** (engine + simulator).
- `sh` (the end-to-end and conformance tests are POSIX shell scripts).
- Optional: `arm-none-eabi-gcc` for the footprint script; PlatformIO for the ESP32 port.

## Build & test

The transpiler and the engine are tested independently.

```sh
# Transpiler (from the repo root; pyproject sets testpaths + pythonpath)
python -m pytest

# Engine + end-to-end golden + Python<->C anim conformance
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

> **Windows / MSYS2:** put the MinGW toolchain on `PATH` and pick the generator explicitly:
> `cmake -S . -B build -G "MinGW Makefiles"`. Run `pytest` with the **Windows** Python, not the
> MinGW one (which has no pytest). On Linux/macOS the defaults work as-is.

CI (`.github/workflows/ci.yml`) runs exactly these on every push/PR. **A PR must keep both suites
green.** Please add a test with any behavior change.

### Transpile something by hand

```sh
cd tool
python htgl.py ../examples/hello.html -o /tmp/hello.uib
#   --emit-c <f.c> --symbol <name>   compile-time C array (note: symbol gets a `_blob` suffix)
#   --strict                          treat author warnings as errors
#   --crc                             append a CRC32 integrity trailer
./build/htgl_sim /tmp/hello.uib /tmp/hello.png    # render to PNG (host simulator)
```

### Golden artifacts

`tests/hello.uib.golden` and `tests/hello.png.golden` are byte-exact oracles for
`examples/hello.html`. If you *intentionally* change the format or renderer, regenerate them and
explain why in the PR — an accidental golden change is a red flag.

## Extending the framework

The engine deliberately uses `switch` statements and a property whitelist rather than a plugin
registry (it's ~2300 lines — a registry would be over-engineering). To add a feature you edit a
small, fixed set of sites:

| To add… | Edit |
|---|---|
| a **node type** | `engine/htgl.c` `htgl_render` switch · the load-time type guard (`-14`) · `tool/htgl/html_tree.py` tag dispatch · `tool/htgl/uib.py` packing |
| an **animatable property** | `engine/htgl.c` `htgl_tick` switch · `uib.py` `_ANIM_PROP` · `html_tree.py` `_ANIM_PROPS` · `cssanim.py` `_CSS_PROP_MAP` |
| a **CSS property** | `tool/htgl/css.py` whitelist · `html_tree.py` (store it on `Node`) — may need a format/version bump |
| a **display / platform** | implement one `flush(x,y,w,h,rgb565)` — the only platform seam (see `port/sim/hal_png.c`, `port/esp32/src/main.cpp`) |

The load-time guards (`-14` unknown node type, `-15` unknown anim prop) mean a half-finished
extension fails *loudly* instead of silently rendering nothing — keep that property.

## Conventions

- **Engine stays platform-independent.** No `#include` of platform headers in `engine/`; all
  platform code lives under `port/`. Pure **C99**, no mandatory heap, and never assume a full
  framebuffer (the banded renderer is the whole point).
- **The `.uib` format is defined in two places that must agree**: `tool/htgl/uib.py`
  (`HEADER_FMT`/`NODE_FMT`/`ANIM_FMT`) and `engine/htgl_internal.h` (the packed structs). The
  `anim_conformance` ctest guards Python↔C agreement for the animation table; the e2e golden guards
  the node/header layout. If you change a struct, update both sides and the conformance harness.
- **Author-facing failures should warn, not be silent.** The transpiler has a `Diagnostics` channel
  (`tool/htgl/diagnostics.py`); thread it through and `warn(...)` when you drop or substitute
  something an author wrote.
- New `htgl_load` rejection reasons are **negative return codes**, documented in
  [`docs/USAGE.md` §9](docs/USAGE.md#9-htgl_load-return-codes).
- Match the surrounding code's style, comment density, and naming. Keep files focused.

## Submitting

1. Branch, make your change, add/adjust tests.
2. `python -m pytest` and `ctest --test-dir build` both green locally.
3. Open a PR describing the change and why; CI must pass.

Questions or design discussion: open an issue. Thanks for contributing!
