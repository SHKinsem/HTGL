<div align="center">

# HTGL

**Write embedded UIs in HTML/CSS. Render them on a microcontroller.**

HTGL is an LVGL-style embedded UI framework — but instead of hand-coding widgets in C,
you author screens in an HTML/CSS subset, transpile them to a compact binary, and render
with a tiny portable C99 engine. The same `.html` previews in any browser.

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Engine: C99](https://img.shields.io/badge/engine-C99-00599C.svg)
![Transpiler: Python 3](https://img.shields.io/badge/transpiler-Python_3-3776AB.svg)
![Milestone 1](https://img.shields.io/badge/milestone%201-complete-success.svg)
![Animation](https://img.shields.io/badge/animation-data--anim-success.svg)

<br/>

<img src="docs/images/hello.png" height="300" alt="HTGL rendering hello.html on the host simulator"/>
&nbsp;&nbsp;
<img src="demos/anim_decl.gif" height="300" alt="HTGL playing a declarative data-anim animation"/>

<sub>Left: <code>examples/hello.html</code> → static PNG &nbsp;·&nbsp; Right: declarative <code>data-anim</code> interpolated by <code>htgl_tick()</code> → GIF</sub>

</div>

---

## Why HTGL

- 🎨 **Author in HTML/CSS** — a subset everyone already knows, and you can open the file in a browser to preview it.
- 🪶 **Tiny & portable** — the engine is pure **C99** with **no mandatory heap** and a chunked renderer that never needs a full framebuffer, so it fits low-end MCUs (e.g. STM32F1, tens of KB RAM, no FPU).
- 🔌 **HAL-decoupled** — the engine is platform-agnostic; only a `flush()` callback differs between the PC simulator and real hardware.
- 📦 **One binary, two ways to load** — the transpiler emits a compact `.uib` that you can either `#include` as a C array (zero runtime parsing) **or** load at runtime from SD/OTA (swap the UI without reflashing).
- 🎬 **Animation** — declare motion with `data-anim` and the engine interpolates it on a `htgl_tick(now_ms)` call (integer math, no FPU). CSS `@keyframes` support is on the way.

## How it works

```mermaid
flowchart LR
    subgraph A["✍️ Author"]
        H["hello.html<br/><i>HTML/CSS subset</i>"]
    end
    subgraph B["🖥️ PC · Python transpiler"]
        T["tool/htgl.py"]
        U[".uib binary<br/><i>+ optional C array</i>"]
    end
    subgraph C["📟 Device · portable C99 engine"]
        L["htgl_load<br/><i>zero-copy</i>"]
        Y["htgl_layout"]
        R["htgl_render<br/><i>banded · no full framebuffer</i>"]
        HAL{{"HAL flush()"}}
    end
    H --> T --> U
    U -->|"#include C array<br/>or SD / OTA blob"| L --> Y --> R --> HAL
    HAL -->|"PC simulator"| PNG["PNG image"]
    HAL -->|"MCU"| LCD["SPI → ILI9341"]
```

The engine renders the screen in **horizontal bands** sized to a small line buffer, drawing only
what intersects each band and flushing it out — the same strategy LVGL uses to avoid holding a
full framebuffer in RAM. Porting to new hardware means implementing one `flush()` function.

## Example

`examples/hello.html` — ordinary HTML you can also open in a browser:

```html
<div style="position:absolute; left:0; top:0; width:240px; height:320px; background-color:#202840">
  <div style="position:absolute; left:20px; top:30px; width:200px; height:60px; background-color:#e0e0e0">
    <div style="position:absolute; left:12px; top:24px; color:#202840; font-size:16px">HTGL</div>
  </div>
  <div style="position:absolute; left:20px; top:120px; width:90px; height:90px; background-color:#ff5050"></div>
  <div style="position:absolute; left:130px; top:120px; width:90px; height:90px; background-color:#50c0ff"></div>
  <div style="position:absolute; left:20px; top:240px; color:#ffffff; font-size:8px">hello, embedded</div>
</div>
```

→ transpiles to a **199-byte** `.uib` → renders to the static image at the top of this README.

## Animation

Declare motion and the engine interpolates it when you call `htgl_tick(now_ms)` (integer-only, no FPU).
Two equivalent ways to author it:

**1. `data-anim` attribute** — compact, embedded-friendly:

```html
<div style="position:absolute; left:10px; top:60px; width:30px; height:30px; background-color:#ff5050"
     data-anim="x" data-from="10" data-to="200" data-dur="1000" data-loop="pingpong" data-ease="ease-out"></div>
```

**2. CSS `@keyframes` + `animation`** — the *same file also animates in a browser*:

```html
<style>@keyframes slide { from { left: 10px } to { left: 200px } }</style>
<div style="position:absolute; left:10px; top:60px; width:30px; height:30px; background-color:#ff5050;
            animation: slide 1s infinite alternate ease-out"></div>
```

Both compile to **byte-identical** animation records.

- property: `x` · `y` · `w` · `h`
- loop: `once` (clamp) · `loop` (restart) · `pingpong` (reverse each pass)
- easing: `linear` · `ease-in` · `ease-out` · `ease-in-out` (integer quadratic curves)

<div align="center"><img src="demos/anim_ease.gif" width="260" alt="linear vs ease-in vs ease-out"/></div>

<sub>Same x-translation under three easing curves — ease-out leads, linear is in the middle, ease-in lags.</sub>

Render a timeline to frames with `htgl_sim in.uib out_prefix <frames> <total_ms>`, then stitch a GIF
(see `demos/`).

## The `.uib` binary format

A flat, little-endian, zero-copy layout. Compile-time and runtime loading share the exact same bytes.

| Section | Size | Contents |
|---|---|---|
| **Header** | 16 B | magic `HTGL`, version, node count, screen W/H, string-table offset, anim count |
| **Node[]** | 18 B each | type (`SCREEN`/`BOX`/`TEXT`), parent index, x/y/w/h, bg/fg (RGB565), font scale, text ref |
| **Anim[]** | 10 B each | node index, prop (x/y/w/h), mode (loop + easing, packed), from/to, duration (ms) |
| **String table** | var | length-prefixed ASCII for text nodes |

Animation-free blobs carry `anim count = 0` and an empty `Anim[]`, so they stay byte-identical to the original format.

## Prerequisites

- **Python 3** with `pytest` (transpiler + its tests)
- A **C99 compiler** and **CMake** (engine + simulator)
- `sh` (used by the end-to-end test)

> On Windows with MSYS2, ensure the MinGW toolchain is on `PATH` and select the generator
> explicitly: `cmake -S . -B build -G "MinGW Makefiles"`. On Linux/macOS the default generator works as-is.

## Build & run the host simulator

```sh
# 1. transpile the example to a .uib binary
cd tool && python htgl.py ../examples/hello.html -o ../build/hello.uib && cd ..

# 2. build the engine + simulator
cmake -S . -B build && cmake --build build

# 3. render to a PNG (use ./build/htgl_sim.exe on Windows)
./build/htgl_sim build/hello.uib build/hello.png
```

## Test

```sh
cd tool && python -m pytest      # transpiler (75 tests)
ctest --test-dir build           # engine + end-to-end golden image (7 suites)
```

## Project structure

```
htgl/
├── tool/            Python transpiler:  HTML/CSS subset → .uib (+ C array)
│   └── htgl/        colors · css · cssanim · html_tree · uib · emitc · cli
├── engine/          Portable C99 engine (no platform code)
│   ├── htgl.c       load / layout / banded render
│   ├── draw.c       rect + bitmap-font rasterization
│   └── *.h          public API + internal layout
├── port/
│   ├── sim/         Host backend: flush() → PNG  (the only host-specific code)
│   └── stm32/       MCU backend (roadmap)
├── examples/        hello.html
├── tests/           C unit tests + golden .uib / .png
└── docs/            design spec + implementation plans
```

## Roadmap

| Milestone | Scope | Status |
|---|---|---|
| **M1** | HTML → `.uib` → host simulator → PNG (vertical slice) | ✅ complete |
| **Anim P1** | `data-anim` declarative animation + `htgl_tick` runtime | ✅ complete |
| **Anim P2** | CSS `@keyframes`/`animation` subset → same runtime | ✅ complete |
| **Anim P3** | Easing curves (linear/ease-in/ease-out/ease-in-out) | ✅ complete |
| **M2** | STM32F1 + ILI9341 port (new `flush()`), engine core unchanged | 🔜 |
| **M3** | Images, buttons + touch input (event dispatch) | 🔜 |
| **M4** | Flex/percent layout (fixed-point), easing curves | 🔜 |
| **M5** | Runtime `.uib` from SD/OTA, multi-font/UTF-8, browser live preview | 🔜 |

Design and per-milestone plans live under [`docs/superpowers/`](docs/superpowers/).

## License

[MIT](LICENSE) — free for commercial and embedded use.
