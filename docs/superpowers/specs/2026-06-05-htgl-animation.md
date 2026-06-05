# HTGL Animation Runtime — design (Phase 1)

- Date: 2026-06-05
- Status: in progress on branch `feat/anim-runtime`
- Approach chosen by user: **two-phase** — Phase 1 a compact custom `data-anim` attribute to get the runtime/tick/interpolation working; Phase 2 (later) maps a CSS `@keyframes`/`transition` subset onto the same runtime.

## Goal
Make HTGL screens animate. Author motion with a small declarative attribute, transpile it
into the `.uib`, and have the C engine interpolate it over time via a `htgl_tick(now_ms)` call.
Visible deliverable: the host simulator renders an animation to a GIF.

## Authoring syntax (Phase 1)
On any `<div>`:
```html
<div style="position:absolute; left:10px; top:60px; width:30px; height:30px; background-color:#ff5050"
     data-anim="x" data-from="10" data-to="200" data-dur="1000" data-loop="pingpong"></div>
```
- `data-anim`: which property animates — `x` | `y` | `w` | `h` (one property per node in Phase 1)
- `data-from`, `data-to`: integer px endpoints
- `data-dur`: duration in ms for one pass
- `data-loop`: `once` (clamp at `to`) | `loop` (restart) | `pingpong` (reverse each pass); default `once`

The element's static `left/top/width/height` still define its frame-0 / no-tick appearance, so the
file previews (statically) in a browser. Animation play is a runtime feature.

## `.uib` format extension (backward compatible, version stays 1)
The header's previously-`reserved` u16 becomes **`anim_count`**. New section order:
```
Header (16B)         ... reserved-slot now = anim_count
Node[]   (18B each)
Anim[]   (10B each)  <-- NEW, starts at HEADER_SIZE + NODE_SIZE*count
StrTab               <-- strtab_off (in header) now points PAST the anim table
```
Anim record — little-endian `<HBBhhH` (10 bytes):
| field | type | notes |
|---|---|---|
| node_idx | u16 | which node this animates |
| prop | u8 | 0=x 1=y 2=w 3=h |
| loop | u8 | 0=once 1=loop 2=pingpong |
| from | i16 | px |
| to | i16 | px |
| dur_ms | u16 | one-pass duration |

Old (animation-free) blobs have `anim_count=0`, an empty Anim section, and the same `strtab_off`
as before → byte-identical, so existing goldens and the current v1 engine keep working (the engine
simply ignores the anim table and renders the static frame). Graceful degradation by construction.

## Engine runtime (Phase 1)
- Add per-node runtime state `cur_x/cur_y/cur_w/cur_h` (initialized from the node's static fields).
- `int htgl_tick(htgl_ctx*, uint32_t now_ms)`: for each anim, compute pass progress `p∈[0,1]` from
  `now_ms`/`dur` per loop mode, set `cur_<prop>[node_idx] = from + (to-from)*p` (integer math, no FPU).
  Returns 1 if any value changed (so the caller knows to re-render).
- `htgl_layout` resolves absolute coords from `cur_x/cur_y`; `htgl_render` uses `cur_w/cur_h`.
  (Phase 1 re-runs layout + full render each tick; dirty-rect optimization is a later phase.)
- No HAL change: time is passed in explicitly (`now_ms`), so the simulator and an MCU both just call tick.

## Simulator
`htgl_sim <in.uib> <out.png>` keeps its single static-frame behavior (e2e golden unaffected).
Add an optional frame mode: `htgl_sim <in.uib> <out_prefix> --frames <N> --ms <total>` writes
`out_prefixNNN.png` ticking across `[0,total)`. A Python helper (Pillow) stitches them into a GIF.

## Out of scope (Phase 1)
CSS `@keyframes`/`transition` parsing (Phase 2), easing curves (linear only), multi-property per node,
color/opacity animation, dirty-rectangle redraw. Keep it small; prove the runtime end to end first.

## Test plan
- Transpiler: `data-anim` parsing → anim records; golden bytes for header `anim_count`, anim layout,
  and `strtab_off` shifted past the anim table; back-compat (no anim → byte-identical to before).
- Engine: `htgl_tick` interpolation at p=0/0.5/1 for each loop mode; layout/render reflect `cur_*`.
- Demo: a `data-anim` bouncing box rendered to a GIF.
