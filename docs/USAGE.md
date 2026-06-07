# HTGL Usage Reference

This is the **complete reference** for authoring, transpiling, loading, and rendering HTGL
UIs. The [README](../README.md) is a quickstart; this document is the exhaustive contract.

It documents the **exact** supported subset (and its limits), every CLI flag, the full C API
with its return-code table, both loading modes and their memory-safety model, a from-scratch
porting walkthrough, and a troubleshooting guide. Where the code silently drops or substitutes
something, this document says so explicitly — a feature you think is supported but isn't is a
silent-failure trap.

> **Honesty contract.** HTGL implements a *deliberately tiny* subset. If a CSS construct is not
> in the supported table below, the transpiler **drops it — and now warns on stderr** when it does
> (dropped units/properties, unrecognized colors, non-ASCII text, snapped font sizes, clamped
> coordinates). Pass `--strict` to turn those warnings into a non-zero exit. Still preview the
> rendered `.uib`/PNG, not just the source `.html` in a browser — the browser shows CSS the engine
> ignores.

---

## Table of contents

1. [Pipeline overview](#1-pipeline-overview)
2. [The CLI (`htgl.py`)](#2-the-cli-htglpy)
3. [Authoring: the supported HTML/CSS subset](#3-authoring-the-supported-htmlcss-subset)
4. [Colors](#4-colors)
5. [Text and fonts](#5-text-and-fonts)
6. [Animation](#6-animation)
7. [Touch / tap input](#7-touch--tap-input)
8. [The C API (`engine/htgl.h`)](#8-the-c-api-enginehtglh)
9. [`htgl_load` return codes](#9-htgl_load-return-codes)
10. [Loading modes and the memory-safety model](#10-loading-modes-and-the-memory-safety-model)
11. [Porting walkthrough](#11-porting-walkthrough)
12. [Footprint](#12-footprint)
13. [Troubleshooting](#13-troubleshooting)
14. [The `.uib` binary format](#14-the-uib-binary-format)

---

## 1. Pipeline overview

```
author.html  ──(tool/htgl.py)──►  ui.uib  ──┬──► #include as C array  ──►  htgl_load() at boot
                                            └──► load at runtime (SD/OTA) ─►  htgl_load()
                                                                                  │
                            htgl_init → htgl_load → htgl_layout → htgl_render → HAL.flush()
                                                       ▲              │
                                                   htgl_tick ─────────┘  (animation, optional)
```

You write an HTML/CSS-subset file, transpile it on a PC to a compact little-endian `.uib` blob,
then either embed that blob as a C array at compile time or load it at runtime. The portable C99
engine validates the blob, resolves coordinates, and renders the screen in horizontal **bands**
sized to a caller-owned line buffer — it never needs a full framebuffer.

---

## 2. The CLI (`htgl.py`)

The transpiler entry point is `tool/htgl.py`, which calls `htgl.cli.main`. Run it from the `tool/`
directory (so the `htgl` package is importable):

```sh
cd tool
python htgl.py <input.html> -o <output.uib> [--emit-c <file.c>] [--symbol <name>] [--strict] [--crc]
```

### Arguments

| Argument | Required | Description |
|---|---|---|
| `input` (positional) | yes | Path to the input `.html` file. Read as UTF-8. |
| `-o`, `--output` | **yes** | Path to the output `.uib` binary. Parent directories are created automatically. |
| `--emit-c <file>` | no | Also write a C source file containing the blob as a `const unsigned char[]` array (compile-time embedding mode). Parent directories are created automatically. |
| `--symbol <name>` | no | Base name for the C array symbols. Defaults to the **stem of the `--emit-c` filename**. Ignored when `--emit-c` is absent. |
| `--strict` | no | Treat transpiler warnings (dropped CSS, unrecognized color, non-ASCII text, clamped coordinate, snapped font size) as errors: print them and **exit non-zero**. |
| `--crc` | no | Append a 4-byte CRC32 integrity trailer (sets header `flags` bit 0). The engine verifies it on load and rejects a corrupted blob (`-16`). Off by default → byte-identical to the pre-CRC format. |

`-o/--output` is **mandatory** — there is no stdout mode. Warnings always print to **stderr**;
`--strict` makes them fail the build. There is no verbosity flag and no screen-size override.

### Generated C symbols

`emitc.py` **appends `_blob` and `_blob_len`** to whatever symbol you pass. So `--symbol ui`
emits:

```c
const unsigned char ui_blob[];       /* the .uib bytes */
const unsigned int  ui_blob_len;     /* length in bytes */
```

> **`--symbol` gotcha.** Because the suffix is appended, the *default* symbol (the `--emit-c`
> stem) gets a **doubled** `_blob`: `--emit-c src/ui_blob.c` with no `--symbol` emits
> `ui_blob_blob[]` / `ui_blob_blob_len` — and `extern const unsigned char ui_blob[];` will fail to
> link. Always pass an explicit `--symbol` (the README/ESP32 path uses `--symbol ui` to get a clean
> `ui_blob`).

### How the screen size is chosen

You do **not** pass screen dimensions on the command line. The transpiler infers them from the
**first `<div>` in document order**: its `width`/`height` (in px) become the screen size. If the
first div has no positive width/height, the screen defaults to **240 × 320**
(`cli.py:_infer_screen_size`). In practice your outermost div is the screen — give it an explicit
`width`/`height` in px.

### Exit status

`main()` returns `0` on success (warnings alone do **not** fail unless `--strict` is set, in which
case it returns `1`). Malformed input that the `html.parser` tolerates still produces a blob (the
parser is lenient); a missing input file raises a Python exception (non-zero exit).

---

## 3. Authoring: the supported HTML/CSS subset

HTGL parses HTML with Python's lenient `html.parser`. The model is intentionally minimal:

- A synthetic **SCREEN** node (index 0) is created, sized to the inferred screen dimensions.
- Every `<div>` becomes a **BOX** node, nested under the div it sits inside.
- Any non-whitespace text inside a div becomes a **TEXT** node, child of that div.
- `<style>` blocks are collected as CSS (for `@keyframes`), **not** rendered as text.
- `<script>` blocks are ignored entirely.
- **All other tags are ignored**, but their text/children are kept inline (a `<span>`’s text
  still becomes a TEXT node of the enclosing div; the `<span>` itself contributes no box).

Layout is **absolute only**: every box is positioned relative to its parent box's top-left using
`left`/`top`, and sized with `width`/`height`. There is no flow, no flex, no margins/padding.

### Supported / NOT supported

| Construct | Supported? | Notes |
|---|---|---|
| `<div>` → box | ✅ | The only structural element. Nesting allowed; children offset from parent. |
| text inside a div → label | ✅ | ASCII only (see [§5](#5-text-and-fonts)). |
| `<style>` with `@keyframes` | ✅ | Used for CSS animation only ([§6](#6-animation)). |
| `<script>` | ⛔ ignored | Contents dropped. |
| `position: absolute` | ✅ | The **only** positioning mode. `position` value is stored but layout is always absolute-from-parent. |
| `position: relative/fixed/static/sticky` | ⛔ | Treated the same as absolute (the value string is kept but unused). Do not rely on it. |
| `left`, `top`, `width`, `height` | ✅ | **Integer px only.** |
| `%`, `em`, `rem`, `vh`, `vw`, `calc()`, `auto` | ⛔ **silently dropped** | A `width:50%` yields **no** width property → that dimension is `0` and the element is invisible. |
| `background-color` | ✅ | See [§4](#4-colors). |
| `color` (text color) | ✅ | Foreground for TEXT children of the div. |
| `font-size` | ✅ but **snapped** | Quantized to a `×8px` integer scale; see [§5](#5-text-and-fonts). |
| `border`, `border-radius` | ⛔ | Dropped. No outlines, no rounded corners. |
| `box-shadow` | ⛔ | Dropped. |
| gradients (`linear-gradient`, …) | ⛔ | Dropped → background falls through to **black**. |
| `opacity`, alpha | ⛔ | No transparency. Boxes are fully opaque fills. |
| `transform` (rotate/scale/translate) | ⛔ | Dropped. |
| flexbox / grid | ⛔ | Dropped. No layout effect. |
| percentage / responsive layout | ⛔ | See `%` row above. |
| images (`<img>`, `background-image`) | ⛔ | Not supported at all. |
| text wrapping / multi-line | ⛔ | Text is a single line; long text overruns the box (then gets clipped at screen edges). |
| multiple fonts / font-family | ⛔ | One built-in 8×8 bitmap font, scaled by integer factor only. |
| `data-anim` / CSS `animation` | ✅ | See [§6](#6-animation). |
| `data-tap` | ✅ | See [§7](#7-touch--tap-input). |

> **Drops are warned.** `css.parse_style` keeps only a whitelist of properties
> (`left/top/width/height/font-size` as px; `position/background-color/color` as strings); every
> other declaration is discarded **with a warning**, and a non-px unit on a px property is dropped
> **with a warning** too (`css.py`). Run the CLI and read stderr, or use `--strict` to fail on them.

### Coordinate range

Geometry is stored as **signed 16-bit** (`int16_t x,y,w,h` in the node struct, `uib.NODE_FMT`).
Values outside `[-32768, 32767]` are **clamped, with a warning** — a coordinate like
`width:400000px` no longer crashes the tool. Keep coordinates within a realistic screen range.

---

## 4. Colors

Colors apply to `background-color` (box fill) and `color` (text). They are parsed by
`colors.to_rgb565` into 16-bit RGB565 at transpile time.

**Accepted forms:**

| Form | Example | Result |
|---|---|---|
| `#rgb` (3 hex) | `#f50` | Expanded to `#ff5500`, packed to RGB565. |
| `#rrggbb` (6 hex) | `#202840` | Packed to RGB565. |
| Named color (whitelist) | `red`, `navy` | From the 13-entry table below. |
| `rgb()` / `rgba()` | `rgb(255,0,0)`, `rgba(0,0,255,.5)` | Parsed to RGB565 (**alpha ignored**, with a warning). |
| **Anything else** | `hsl(...)`, `tomato`, `#1234` | Becomes black (`0x0000`) **with a warning**. |

**The complete named-color whitelist** (`colors._NAMED`) — these are the **only** names that work:

```
black  white  red  green  blue  lime  gray  grey
yellow cyan   magenta  silver  navy
```

(`gray` and `grey` are aliases → 13 keys, 12 distinct colors.) Note `green` is the CSS
half-intensity `#008000`; use `lime` for full-bright `#00ff00`.

> **Black-fallback (now warned).** `rgb()`/`rgba()` are parsed (alpha dropped). Any color HTGL
> still can't parse — an unlisted named color like `orange`, an `hsl(...)`, or a malformed hex —
> returns `0x0000` (black) **and emits a warning**. If a box renders black, check the transpiler
> warnings (or just use `#rrggbb`).

RGB565 packing drops the low bits of each channel (5-6-5), so expect mild banding on gradients of
near colors — this is inherent to the 16-bit panel format, not a bug.

---

## 5. Text and fonts

- The engine ships a single built-in **8×8 bitmap font** (`font8x8_basic`, `const`, in flash).
- `font-size` does **not** set pixel height directly. The transpiler converts it to an integer
  **scale factor**: `scale = max(1, round(font_size / 8))` (`uib.py`). The glyph is then drawn at
  `8 × scale` px. So:

  | `font-size` | Stored scale | Rendered glyph height |
  |---:|:---:|---:|
  | ≤ `11px` | 1 | 8 px |
  | `12px`–`20px` | 2 | 16 px |
  | `21px`–`27px` | 3 | 24 px |
  | … | … | … |

  Effectively font-size **snaps to the nearest multiple of 8**, with a floor of 8px. Sub-pixel
  sizes are impossible. The boundary uses Python's **banker's rounding** (`round` rounds halves to
  even), so the bands are slightly irregular: `20px` → `round(2.5)` = **2** (16px, *not* 24px),
  while `28px` → `round(3.5)` = **4** (32px). The exact rule is `8 × max(1, round(font_size / 8))`.
- **Text is ASCII-only.** At transpile time, non-ASCII characters are replaced with `?`
  (`uib.py`: `text.encode("ascii", "replace")`). At render time the engine also maps any byte
  `>= 128` to `?` (`draw.c:draw_glyph`). A label that shows `?` where you expected é/中/emoji is
  this substitution, not a font bug.
- Strings are **length-prefixed and capped at 255 bytes** (`uib.py` `[:255]`). Longer text is
  truncated.
- Text does **not** wrap and has a **transparent background** (only foreground pixels are set);
  it advances `8 × scale` px per character and is clipped at the band/screen edges.

---

## 6. Animation

A single property of a box can be animated over time. The engine interpolates with **integer math
only** (no FPU) when you call `htgl_tick(now_ms)`. There are two equivalent authoring syntaxes; both
compile to **byte-identical** animation records in the `.uib`.

### Animated properties

| Code | Property | CSS equivalent | Interpolation |
|---|---|---|---|
| `x` | left offset | `left` | linear in int16 |
| `y` | top offset | `top` | linear in int16 |
| `w` | width | `width` | linear in int16 |
| `h` | height | `height` | linear in int16 |
| `bg` | background color | `background-color` | per-RGB565-channel blend |

Exactly **one** property per element may be animated.

### Syntax 1 — `data-anim` attributes (compact, embedded-friendly)

```html
<div style="position:absolute; left:10px; top:60px; width:30px; height:30px; background-color:#ff5050"
     data-anim="x"
     data-from="10" data-to="200"
     data-dur="1000"
     data-loop="pingpong"
     data-ease="ease-out"></div>
```

| Attribute | Values | Default | Notes |
|---|---|---|---|
| `data-anim` | `x` `y` `w` `h` `bg` | (required) | If absent or unrecognized, no animation is attached. |
| `data-from` / `data-to` | integer (geometry) or color (for `bg`) | `0` / `0` (geometry); `black` (bg) | For `bg`, parsed as a color via the same rules as [§4](#4-colors) (unparseable → black). |
| `data-dur` | integer ms | `1000` | Clamped to a minimum of `1` ms. |
| `data-loop` | `once` `loop` `pingpong` | `once` | `once`=clamp at end · `loop`=restart · `pingpong`=reverse each pass. |
| `data-ease` | `linear` `ease-in` `ease-out` `ease-in-out` | `linear` | Unrecognized values fall back to `linear`. |

### Syntax 2 — CSS `@keyframes` + `animation` (also previews in a browser)

```html
<style>@keyframes slide { from { left: 10px } to { left: 200px } }</style>
<div style="position:absolute; left:10px; top:60px; width:30px; height:30px;
            background-color:#ff5050;
            animation: slide 1s infinite alternate ease-out"></div>
```

CSS-subset rules (`cssanim.py`):

- `@keyframes <name> { from { … } to { … } }` — `0%`/`100%` are accepted as aliases for
  `from`/`to`. **Exactly one** animated property may appear, and it must be present in **both**
  `from` and `to` (otherwise the keyframe is skipped **with a warning**).
- Property map: `left→x`, `top→y`, `width→w`, `height→h`, `background-color→bg`. Geometry
  values are px integers; `background-color` uses [§4](#4-colors) parsing.
- `animation:` shorthand — `name`, a duration (`1s`→1000ms, `500ms`→500), optional
  iteration/direction/timing keywords:
  - `infinite` → looping; a numeric count is treated as `once` (MVP limitation).
  - `alternate` → pingpong. `infinite alternate` → **pingpong**; `infinite` alone → **loop**;
    neither → **once**.
  - timing keyword (or a separate `animation-timing-function`) maps `ease`→`ease-in-out`,
    and `linear`/`ease-in`/`ease-out`/`ease-in-out` to themselves.
- Anything the shorthand can't resolve (missing name or duration, unknown keyframe) → no animation,
  **with a warning** (e.g. `animation references @keyframes 'slide', which is missing or unsupported`).

> **Precedence:** if a single element has **both** a `data-anim` attribute and a CSS `animation`,
> the `data-anim` wins (`html_tree.resolve_css_anims` skips nodes that already have `node.anim`).

### Runtime model

`htgl_tick(ctx, now_ms)` advances the animation clock to an absolute time in milliseconds and
mutates the per-node *current* values (`cur_x/cur_y/cur_w/cur_h/cur_bg`). It returns `1` if any
value changed since the previous tick, else `0`. **After ticking you must call `htgl_layout` then
`htgl_render`** to see the change. With no animations loaded it is a no-op returning `0`.

Easing curves are integer quadratics on a 0..256 fixed-point fraction; geometry results are
clamped to int16 range inside `htgl_tick`.

---

## 7. Touch / tap input

Make a box tappable by giving it `data-tap="<id>"` where `<id>` is `1..255`:

```html
<div style="position:absolute; left:20px; top:200px; width:200px; height:60px; background-color:#3060c0"
     data-tap="7"></div>
```

- The id is stored in the box node's **`font` byte** in the `.uib` (BOX nodes reuse that byte as
  the tap id; TEXT nodes use it as the font scale). Values outside `1..255` (or non-numeric) are
  stored as `0` = **not interactive** (`html_tree.py`).
- A box with tap id `0` (the default) and all TEXT nodes are **never hit-tested**.

### Driving it from C

```c
void on_tap(int tap_id, void *user) { /* tap_id is the data-tap value 1..255 */ }

htgl_set_tap_handler(&ctx, on_tap, /*user=*/NULL);  /* pass cb=NULL to disable */

/* feed pointer events (screen coordinates) from your touch controller / mouse: */
htgl_pointer_down(&ctx, x, y);
htgl_pointer_up(&ctx, x, y);
```

- **Call `htgl_layout` before hit-testing** so absolute coordinates and current sizes are valid.
  `htgl_pointer_down` records the topmost interactive box under `(x,y)` (highest node index wins,
  since later nodes draw on top).
- A **tap fires only if press *and* release land on the same element** (standard touch UX). After
  every `htgl_pointer_up` the pressed-node state resets to "none".
- The hit-test uses each box's current width/height (so an animated box is hit at its animated
  position).

There is no built-in touch hardware driver — you wire your panel's touch IC (or the simulator's
mouse) to `htgl_pointer_down/up`. The ESP32 port stubs this out because bare ST7789 panels usually
have no touch controller (`port/esp32/src/main.cpp`).

---

## 8. The C API (`engine/htgl.h`)

All functions are `extern "C"`-guarded. The context struct (`htgl_ctx`) is opaque in the public
header; its full definition lives in `engine/htgl_internal.h` so you can stack- or statically
allocate one:

```c
#include "htgl.h"
#include "htgl_internal.h"   /* needed only to size/allocate htgl_ctx */

static htgl_ctx ctx;                 /* ~3.6 KB .bss with HTGL_MAX_NODES=256 */
static uint16_t line_buf[240 * 16];  /* caller-owned RGB565 line buffer */
```

### Lifecycle

```
htgl_init(&ctx, &hal, line_buf, line_buf_px)
        │
        ▼
rc = htgl_load(&ctx, blob, len)        ──►  if (rc != 0) bail; see §9
        │
   ┌────┤  (per frame, if animating)
   │    ▼
   │  htgl_tick(&ctx, now_ms)          ──►  returns 1 if anything moved
   │    │
   ▼    ▼
htgl_layout(&ctx)                       ──►  resolve absolute coords
        │
        ▼
htgl_render(&ctx)                       ──►  bands flushed via hal.flush()
```

For a **static** screen: `init → load → layout → render` once. For **animation**: call
`tick → layout → render` each frame with the current `now_ms`.

### The HAL

```c
typedef struct {
    /* Push a band (x,y,w,h) of RGB565 pixels to the display. */
    void (*flush)(int x, int y, int w, int h, const uint16_t *buf);
} htgl_hal;
```

`flush` is the **only** function you implement to port HTGL. `buf` holds `w*h` little-endian
RGB565 words for the band rectangle `(x,y,w,h)` in screen coordinates.

### Function reference

| Function | Signature | Purpose |
|---|---|---|
| `htgl_init` | `htgl_ctx *htgl_init(htgl_ctx *ctx, const htgl_hal *hal, uint16_t *line_buf, int line_buf_px)` | Zero the context and attach the HAL + caller-owned RGB565 line buffer (`line_buf_px` = its capacity in pixels). Returns `ctx`. |
| `htgl_load` | `int htgl_load(htgl_ctx *ctx, const uint8_t *blob, int len)` | Validate and attach a `.uib` blob **zero-copy** (the blob must outlive the context). Returns `0` on success or a negative code; see [§9](#9-htgl_load-return-codes). |
| `htgl_layout` | `void htgl_layout(htgl_ctx *ctx)` | Resolve each node's relative coords into absolute screen coords. Reflects current (possibly animated) values. Call after `load`, after each `tick`, and before hit-testing. |
| `htgl_render` | `void htgl_render(htgl_ctx *ctx)` | Render the whole screen in horizontal bands sized to `line_buf_px / screen_w`, flushing each via `hal.flush`. |
| `htgl_screen_w` | `int htgl_screen_w(const htgl_ctx *ctx)` | Screen width from the loaded blob (`0` if none loaded). |
| `htgl_screen_h` | `int htgl_screen_h(const htgl_ctx *ctx)` | Screen height from the loaded blob (`0` if none loaded). |
| `htgl_tick` | `int htgl_tick(htgl_ctx *ctx, uint32_t now_ms)` | Advance the animation clock to `now_ms`. Returns `1` if any animated value changed, else `0`. Safe with no animations (returns `0`). Must be followed by `layout`+`render`. |
| `htgl_set_tap_handler` | `void htgl_set_tap_handler(htgl_ctx *ctx, htgl_tap_cb cb, void *user)` | Register the tap callback (`cb=NULL` disables). `user` is passed through to the callback. |
| `htgl_pointer_down` | `void htgl_pointer_down(htgl_ctx *ctx, int x, int y)` | Record a press at screen `(x,y)`. Run `htgl_layout` first. |
| `htgl_pointer_up` | `void htgl_pointer_up(htgl_ctx *ctx, int x, int y)` | Record a release; fires the tap callback if it lands on the same element that was pressed. Always clears the pressed state. |

Callback type: `typedef void (*htgl_tap_cb)(int tap_id, void *user);`

Constants in `htgl.h`: `HTGL_TYPE_SCREEN=0`, `HTGL_TYPE_BOX=1`, `HTGL_TYPE_TEXT=2`,
`HTGL_ROOT_PARENT=0xFFFF`, `HTGL_NO_TEXT=0xFFFF`. The node cap `HTGL_MAX_NODES=256` is in
`htgl_internal.h`.

> **Note:** `htgl_init` does not validate `line_buf`/`line_buf_px`; the line-buffer-too-small check
> happens in `htgl_load` (return code `-12`), because `screen_w` (which sets the required minimum)
> comes from the blob.

---

## 9. `htgl_load` return codes

These codes are **not in the header** — this table is the authoritative source. `htgl_load`
fully validates an untrusted blob and returns the **first** failure it finds:

| Code | Meaning |
|---:|---|
| `0` | OK — blob validated and attached. |
| `-1` | `len` smaller than the 16-byte header. |
| `-2` | Bad magic (first 4 bytes are not `"HTGL"`). |
| `-3` | Version field ≠ 1. |
| `-4` | `node_count` is 0, or greater than `HTGL_MAX_NODES` (256). |
| `-5` | The node array (header + 18 B × node_count) overflows `len`. |
| `-6` | `strtab_off` is past the end of the blob (`> len`). |
| `-7` | `screen_w` or `screen_h` is 0. |
| `-8` | `strtab_off` points *before* the end of the anim table (string table must come after nodes **and** anims). |
| `-9` | A node's parent index is neither the root sentinel (`0xFFFF`) nor a strictly earlier node (enforces parent-before-child). |
| `-10` | The anim table (after the node array) overflows `len`. |
| `-11` | An anim record's `node_idx` is out of range (`>= node_count`). |
| `-12` | The caller's line buffer is too small: `line_buf_px < screen_w`. (Blob-controlled `screen_w`, so this is an untrusted-input bound, not just a contract.) |
| `-13` | A TEXT node's `text_ref` (or its length-prefixed string body) lies outside the blob. |
| `-14` | A node has an unknown `type` (`> HTGL_TYPE_TEXT`). The renderer has no handler for it, so it is rejected rather than silently drawn as nothing. |
| `-15` | An anim record has an unknown `prop` (`> 4`). `htgl_tick` has no case for it, so it is rejected rather than silently animating nothing. |
| `-16` | The header's CRC32 flag (`flags` bit 0) is set but the trailing CRC doesn't match the blob (corrupted / truncated / half-written), or the blob is too short to hold the 4-byte trailer. |

On any non-zero code the context is reset to a clean **not-loaded** state: `htgl_screen_w/h`
return `0` and `htgl_render` is a safe no-op. A *failed reload* therefore also clears the
previously-loaded UI rather than leaving it stale — don't call `layout`/`render` expecting output.

---

## 10. Loading modes and the memory-safety model

There is exactly **one** byte format. The two "modes" differ only in *where the bytes come from*.

### Mode A — compile-time embedding (`--emit-c`)

```sh
cd tool
python htgl.py ui.html -o /tmp/ui.uib --emit-c ../port/esp32/src/ui_blob.c --symbol ui
```

This writes both the `.uib` and a C file declaring `ui_blob[]` / `ui_blob_len`. Compile `ui_blob.c`
into your firmware and load it from the array:

```c
extern const unsigned char ui_blob[];
extern const unsigned int  ui_blob_len;

htgl_load(&ctx, ui_blob, (int)ui_blob_len);
```

Zero runtime parsing of HTML, no filesystem needed. This is what `port/esp32` does.

### Mode B — runtime blob (SD / OTA)

Read the same `.uib` bytes from an SD card, flash partition, or OTA download into a buffer and call
`htgl_load`. The host simulator (`port/sim/main.c`) does exactly this with `fread`. You can swap the
UI without reflashing.

### Memory-safety model (read this before loading untrusted blobs)

- **The loader is the trust boundary.** `htgl_load` validates the *entire* structure — magic,
  version, node/anim/string-table bounds, parent indices, and every text reference — before the
  engine dereferences anything (see [§9](#9-htgl_load-return-codes)). A malformed or hostile blob
  from SD/OTA returns a negative code instead of reading out of bounds. The renderer (`draw.c`)
  additionally **clips all geometry** to the band, so even in-bounds-but-absurd coordinates can't
  scribble outside the line buffer.
- **Integrity (optional, recommended for SD/OTA).** Transpile with `--crc` to append a CRC32
  trailer; the loader verifies it *before anything else* and returns `-16` on mismatch. The
  bounds-checks above reject *malformed* blobs; the CRC additionally catches *silent corruption* —
  bit-rot or a half-finished OTA write that would still pass the structural checks. Off by default
  (legacy blobs have `flags=0` and skip it).
- **The blob is borrowed, not copied.** `htgl_load` is zero-copy: it stores pointers into your
  buffer. **The blob memory must remain valid for the entire lifetime of the context** (don't free
  the SD read buffer while the UI is live; a `const` flash array is ideal).
- **The caller owns the line buffer, and it must hold ≥ `screen_w` pixels.** The renderer clears
  each band by writing `screen_w` words per row; a line buffer narrower than the screen would write
  past its end. Because `screen_w` is blob-controlled, `htgl_load` enforces this and returns `-12`
  if `line_buf_px < screen_w`. A practical buffer is `screen_w × rows` for whatever band height you
  want (e.g. `240 × 16`); band height auto-derives as `line_buf_px / screen_w`.
- **`htgl_ctx` itself is caller-allocated** (stack, static, or heap). The engine keeps **zero**
  global state, so multiple independent UIs can coexist by using separate contexts.

---

## 11. Porting walkthrough

Porting HTGL to a new display means implementing **one** function: `flush(x, y, w, h, buf)`. The
engine, transpiler, and `.uib` format are unchanged.

### The minimal HAL

```c
#include "htgl.h"
#include "htgl_internal.h"   /* for the htgl_ctx definition */

/* Push one band of RGB565 pixels to your panel. buf has w*h little-endian words. */
static void my_flush(int x, int y, int w, int h, const uint16_t *buf) {
    /* set the panel address window to (x, y, w, h), then stream w*h words */
    panel_set_window(x, y, w, h);
    panel_write_pixels(buf, (size_t)w * h);
}

static const htgl_hal hal = { my_flush };

static htgl_ctx  ctx;
static uint16_t  line_buf[240 * 16];   /* >= screen_w; here 240 wide, 16-row bands */

void ui_boot(const uint8_t *blob, int blob_len) {
    htgl_init(&ctx, &hal, line_buf, sizeof line_buf / sizeof line_buf[0]);
    int rc = htgl_load(&ctx, blob, blob_len);
    if (rc != 0) { /* handle: see §9 return codes */ return; }
    htgl_layout(&ctx);
    htgl_render(&ctx);   /* my_flush() is called once per band */
}

void ui_loop(uint32_t now_ms) {     /* call repeatedly if animating */
    htgl_tick(&ctx, now_ms);
    htgl_layout(&ctx);
    htgl_render(&ctx);
}
```

### Two reference ports to copy from

- **Host simulator — `port/sim/hal_png.c`.** `png_flush` writes each band into an RGB888 image
  buffer (`stb_image_write`) which is saved as a PNG. The driver `port/sim/main.c` reads a `.uib`
  with `fread`, validates it, and supports both single-frame (`htgl_sim in.uib out.png`) and
  animation-timeline (`htgl_sim in.uib out_prefix <frames> <total_ms>`) modes. This is the clearest
  example of the full lifecycle on a host.
- **ESP32 + ST7789 — `port/esp32/src/main.cpp`.** `flush` is a one-liner over TFT_eSPI:

  ```cpp
  static void flush(int x, int y, int w, int h, const uint16_t *buf) {
      tft.setSwapBytes(true);                 /* LE RGB565 -> panel's BE order */
      tft.pushImage(x, y, w, h, (uint16_t *)buf);
  }
  ```

  `setup()` does `init → load → set_tap_handler`; `loop()` does `tick(millis()) → layout → render`
  at ~60 fps. The UI is embedded as a C array (`ui_blob.c` via `--emit-c`) — no SD card.

### Porting checklist

1. Implement `flush` for your panel's address-window + pixel-write sequence.
2. Allocate an `htgl_ctx` and a `line_buf` of at least `screen_w` pixels.
3. `init → load` (check the return code) `→ layout → render`.
4. If animating, run `tick → layout → render` in your main loop with a millisecond clock.
5. If you have a touch controller, wire its ISR/poll to `htgl_pointer_down/up`.
6. Watch byte order: if colors look wrong, your panel likely wants the opposite endianness
   (see [§13](#13-troubleshooting), TFT_RGB_ORDER / `setSwapBytes`).

---

## 12. Footprint

Measured by cross-compiling the engine for the thesis target (STM32F1 = Cortex-M3) with
`arm-none-eabi-gcc 13.3.0 -mcpu=cortex-m3 -Os` (reproduce via `scripts/measure_footprint.sh`).
Numbers from the [README](../README.md#footprint--does-it-really-fit-a-low-end-mcu):

| What | Size | Where |
|---|---:|---|
| Engine code + 8×8 font (`htgl.c` + `draw.c`) | **~2.6 KB** | flash (`.text` / `.rodata`) |
| Engine static RAM | **0 B** | none — no global state |
| Per-UI context (`htgl_ctx`, `HTGL_MAX_NODES=256`) | **~3.6 KB** (3636 B) | RAM (`.bss`, caller-allocated) |
| Line buffer (caller-owned) | `screen_w × rows × 2 B` | RAM — e.g. 240 × 1 row = **480 B** |

A 240-px-wide UI needs **~2.6 KB flash + ~4 KB RAM** total. The font is `const` (flash, not RAM).
Per-context RAM scales with `HTGL_MAX_NODES` and can be lowered for tighter parts (currently a
compile-time constant in `htgl_internal.h`).

---

## 13. Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| **A box renders black** (or the whole UI is black) | Unsupported color: `rgb()`/`rgba()`, an unlisted named color, a gradient, or a typo'd hex — all fall through to `0x0000`. | Use `#rgb` / `#rrggbb` or one of the 13 [named colors](#4-colors). |
| **An element is missing / has zero size** | A non-px unit (`%`, `em`, `vh`, `calc()`, `auto`) on `left/top/width/height` was silently dropped, leaving that dimension `0`. | Use integer px values only. |
| **A label shows `?` characters** | Non-ASCII text (accents, CJK, emoji) was replaced with `?` at transpile and/or render time. | Restrict label text to ASCII. |
| **Text is the wrong size / won't go below 8px** | `font-size` is snapped to a `×8px` integer scale with an 8px floor. | Use multiples of 8px; sub-8px is impossible. |
| **Colors look inverted / swapped on hardware** (e.g. red↔blue) | RGB565 byte order or channel order mismatch between the engine (little-endian) and your panel. | On TFT_eSPI, toggle `tft.setSwapBytes(...)` and/or the `TFT_RGB_ORDER` (RGB vs BGR) build flag in `platformio.ini`. |
| **`htgl_load` returns negative** | The blob failed validation. | Look up the exact code in [§9](#9-htgl_load-return-codes); `-12` specifically means your `line_buf` is narrower than `screen_w`. |
| **Animation doesn't move** | You ticked but didn't re-layout/render, or `data-dur` is tiny, or the property/keyframe didn't parse. | After `htgl_tick`, always call `htgl_layout` then `htgl_render`. Verify the property name and that the keyframe defines the same property in both `from` and `to`. |
| **A CSS rule from my browser preview isn't showing** | It's outside the supported subset (border, shadow, flex, transform, image, percent…) and was dropped silently. | Check the [supported/NOT-supported table](#supported--not-supported). Preview the rendered `.uib`/PNG, not just the source HTML. |
| **A tap never fires** | `data-tap` missing/0/out of range, you didn't `htgl_layout` before hit-testing, or press and release landed on different elements. | Use `data-tap="1..255"`, layout before pointer events, and ensure press+release hit the same box. |

---

## 14. The `.uib` binary format

A flat, little-endian, **zero-copy** layout. Compile-time and runtime loading share identical
bytes. Section order: **Header · Node[] · Anim[] · String table**.

| Section | Size | Contents |
|---|---|---|
| **Header** | 16 B | magic `HTGL`, version (=1), flags, `node_count`, `screen_w`, `screen_h`, `strtab_off`, `anim_count`. |
| **Node[]** | 18 B each | `type` (SCREEN/BOX/TEXT), `font` byte (TEXT: scale; BOX: tap id), `parent` index, `x`/`y`/`w`/`h` (int16), `bg`/`fg` (RGB565), `text_ref`. |
| **Anim[]** | 10 B each | `node_idx`, `prop` (0=x 1=y 2=w 3=h 4=bg), `mode` byte (`loop = mode & 0x0F`, `ease = (mode >> 4) & 0x0F`), `from`/`to` (int16; for `bg`, the RGB565 value reinterpreted as int16), `dur_ms`. |
| **String table** | variable | length-prefixed ASCII (1-byte length + bytes), one entry per TEXT node, ≤ 255 bytes each. |
| **CRC32 trailer** | 4 B (optional) | Present **iff** header `flags` bit 0 is set (`--crc`). Little-endian CRC32 (IEEE/zlib, poly `0xEDB88320`) over **all** preceding bytes; verified on load (`-16` on mismatch). |

Animation-free blobs carry `anim_count = 0` and an empty `Anim[]`, and CRC is opt-in (`flags = 0`),
so a plain blob is byte-identical to the pre-animation format. The exact structs are in `engine/htgl_internal.h`
(`htgl_header`/`htgl_node`/`htgl_anim`) and the `struct` formats in `tool/htgl/uib.py`
(`HEADER_FMT`/`NODE_FMT`/`ANIM_FMT`).

---

*This document reflects the code in `tool/htgl/` and `engine/` as read directly from source. If a
behavior here disagrees with the code, the code is authoritative — please file an issue.*
