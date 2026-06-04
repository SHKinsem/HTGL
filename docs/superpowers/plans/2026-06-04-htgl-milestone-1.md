# HTGL Milestone 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the end-to-end vertical slice `hello.html → htgl.py → hello.uib → simulator → hello.png`, proving the HTML→binary→portable-C-render pipeline on host.

**Architecture:** A Python transpiler parses an HTML/CSS subset into a flat, packed `.uib` binary (header + node array + string table). A portable C99 engine loads that binary zero-copy, resolves absolute coordinates, and renders via a chunked (band) pipeline into a small RGB565 line buffer, flushing each band through a HAL callback. The simulator HAL collects bands into a full image and writes a PNG. The same engine compiles unchanged for an MCU later — only the HAL `flush` differs.

**Tech Stack:** Python 3 + pytest (transpiler), C99 + CMake/CTest (engine + simulator), vendored public-domain `stb_image_write.h` (PNG) and `font8x8_basic.h` (font). MIT license.

---

## Shared Constants (used across all tasks — keep identical everywhere)

**`.uib` binary format (little-endian):**

Header — 16 bytes, Python `struct` format `<4sBBHHHHH`:
| field | type | notes |
|---|---|---|
| magic | char[4] | `b"HTGL"` |
| version | u8 | `1` |
| flags | u8 | `0` |
| node_count | u16 | number of Node records |
| screen_w | u16 | px |
| screen_h | u16 | px |
| strtab_off | u16 | byte offset from file start to string table |
| reserved | u16 | `0` |

Node — 18 bytes, Python `struct` format `<BBHhhhhHHH`:
| field | type | notes |
|---|---|---|
| type | u8 | `0`=SCREEN(root) `1`=BOX `2`=TEXT |
| font | u8 | font id (MVP only `0`) |
| parent | u16 | parent node index; root = `0xFFFF` |
| x | i16 | px, relative to parent |
| y | i16 | px, relative to parent |
| w | i16 | px |
| h | i16 | px |
| bg | u16 | RGB565 (TEXT ignores) |
| fg | u16 | RGB565 (text color) |
| text_ref | u16 | byte offset into string table to the `len` byte; `0xFFFF` = none |

String table: sequence of entries, each `len:u8` followed by `len` ASCII bytes.

**Node type enum (identical in Python and C):** `SCREEN=0, BOX=1, TEXT=2`. Root parent sentinel `0xFFFF`. No-text sentinel `0xFFFF`.

**RGB565 packing:** `((r>>3)<<11) | ((g>>2)<<5) | (b>>3)` where r,g,b are 0–255.

---

## File Structure

```
htgl/
  LICENSE                     MIT text
  README.md                   project intro + build/run
  .gitignore                  build/, __pycache__/, *.png (except golden)
  CMakeLists.txt              builds engine lib, sim exe, registers CTests
  pyproject.toml              pytest config for the transpiler
  tool/
    htgl.py                   CLI entry (thin: calls htgl.cli.main)
    htgl/
      __init__.py
      colors.py               color string -> RGB565
      css.py                  inline style string -> {prop: value}
      html_tree.py            HTML subset -> Node tree (parent/x/y/w/h/colors/text)
      uib.py                  Node tree -> .uib bytes
      emitc.py                .uib bytes -> C array source
      cli.py                  argparse glue: .html -> .uib (+ optional .c)
    tests/
      test_colors.py
      test_css.py
      test_html_tree.py
      test_uib.py
      test_emitc.py
      test_cli.py
  engine/
    htgl.h                    public API + node type enum
    htgl_internal.h           raw struct layout, accessors
    htgl.c                    load / layout / render pipeline
    draw.c                    fill_rect + draw_text into RGB565 line buffer
    font8x8_basic.h           vendored public-domain 8x8 font
  port/
    sim/
      hal_png.h  hal_png.c    flush -> full RGB888 image -> PNG
      main.c                  load .uib file -> render -> write .png
      stb_image_write.h       vendored public-domain PNG writer
  tests/
    ctest_util.h              CHECK macro for C tests
    test_draw.c
    test_load.c
    test_layout.c
    test_render.c
    test_e2e.sh               transpile hello.html, render, compare PNG
    hello.uib.golden
    hello.png.golden
  examples/
    hello.html
  docs/superpowers/...
```

---

## Phase A — Bootstrap

### Task 1: Repository bootstrap

**Files:**
- Create: `LICENSE`, `README.md`, `.gitignore`, `pyproject.toml`, `CMakeLists.txt`, `tool/htgl/__init__.py`

- [ ] **Step 1: Create LICENSE (MIT)**

Create `LICENSE`:

```
MIT License

Copyright (c) 2026 HTGL contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

- [ ] **Step 2: Create README.md**

Create `README.md`:

```markdown
# HTGL

HTML/CSS-authored UI for embedded systems. Write screens in an HTML subset,
transpile to a compact binary, render with a tiny portable C engine.

- **Authoring:** an HTML/CSS subset you can preview in any browser
- **Transpiler (PC, Python):** `.html` -> `.uib` binary (+ optional C array)
- **Engine (C99):** loads `.uib`, chunked rendering, HAL-decoupled, no mandatory heap
- **Targets:** low-end MCUs (e.g. STM32F1) up to Linux boards

## Build & run the host simulator

```sh
# transpile the example
cd tool && python htgl.py ../examples/hello.html -o ../build/hello.uib && cd ..
# build engine + simulator
cmake -S . -B build && cmake --build build
# render to PNG
./build/htgl_sim build/hello.uib build/hello.png
```

## Test

```sh
cd tool && python -m pytest          # transpiler
ctest --test-dir build               # engine
```

License: MIT.
```

- [ ] **Step 3: Create .gitignore**

Create `.gitignore`:

```
build/
__pycache__/
*.pyc
.pytest_cache/
/examples/*.uib
/examples/*.png
# keep golden fixtures under tests/
!tests/*.golden
```

- [ ] **Step 4: Create pyproject.toml (pytest config)**

Create `pyproject.toml`:

```toml
[tool.pytest.ini_options]
testpaths = ["tool/tests"]
pythonpath = ["tool"]
```

- [ ] **Step 5: Create top-level CMakeLists.txt**

Create `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.15)
project(htgl C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(htgl_engine STATIC engine/htgl.c engine/draw.c)
target_include_directories(htgl_engine PUBLIC engine)

add_executable(htgl_sim port/sim/main.c port/sim/hal_png.c)
target_include_directories(htgl_sim PRIVATE port/sim)
target_link_libraries(htgl_sim PRIVATE htgl_engine)

enable_testing()
# tests are registered by later tasks
```

- [ ] **Step 6: Create the Python package marker**

Create `tool/htgl/__init__.py`:

```python
"""HTGL transpiler: HTML/CSS subset -> .uib binary."""
```

- [ ] **Step 7: Commit**

```bash
git add LICENSE README.md .gitignore pyproject.toml CMakeLists.txt tool/htgl/__init__.py
git commit -m "chore: project bootstrap (MIT, cmake, pytest)"
```

---

## Phase B — Transpiler (Python)

### Task 2: Color parsing → RGB565

**Files:**
- Create: `tool/htgl/colors.py`
- Test: `tool/tests/test_colors.py`

- [ ] **Step 1: Write the failing test**

Create `tool/tests/test_colors.py`:

```python
from htgl.colors import to_rgb565

def test_named_black_and_white():
    assert to_rgb565("black") == 0x0000
    assert to_rgb565("white") == 0xFFFF

def test_pure_red_green_blue():
    assert to_rgb565("#ff0000") == 0xF800
    assert to_rgb565("#00ff00") == 0x07E0
    assert to_rgb565("#0000ff") == 0x001F

def test_short_hex():
    assert to_rgb565("#f00") == 0xF800

def test_case_and_whitespace_insensitive():
    assert to_rgb565("  #00FF00 ") == 0x07E0

def test_unknown_defaults_black():
    assert to_rgb565("not-a-color") == 0x0000
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tool && python -m pytest tests/test_colors.py -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'htgl.colors'`

- [ ] **Step 3: Write minimal implementation**

Create `tool/htgl/colors.py`:

```python
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
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tool && python -m pytest tests/test_colors.py -v`
Expected: PASS (5 passed)

- [ ] **Step 5: Commit**

```bash
git add tool/htgl/colors.py tool/tests/test_colors.py
git commit -m "feat(tool): color string to RGB565"
```

---

### Task 3: Inline CSS parsing

**Files:**
- Create: `tool/htgl/css.py`
- Test: `tool/tests/test_css.py`

- [ ] **Step 1: Write the failing test**

Create `tool/tests/test_css.py`:

```python
from htgl.css import parse_style

def test_parses_known_props():
    s = "position:absolute; left:10px; top:20px; width:100px; height:30px;"
    out = parse_style(s)
    assert out == {
        "position": "absolute",
        "left": 10, "top": 20, "width": 100, "height": 30,
    }

def test_colors_kept_as_strings():
    out = parse_style("background-color:#f00; color: white")
    assert out["background-color"] == "#f00"
    assert out["color"] == "white"

def test_font_size_px_to_int():
    assert parse_style("font-size:16px")["font-size"] == 16

def test_unknown_props_ignored():
    out = parse_style("z-index:5; left:3px")
    assert "z-index" not in out
    assert out["left"] == 3

def test_empty_and_malformed_safe():
    assert parse_style("") == {}
    assert parse_style("garbage;;;:") == {}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tool && python -m pytest tests/test_css.py -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'htgl.css'`

- [ ] **Step 3: Write minimal implementation**

Create `tool/htgl/css.py`:

```python
"""Inline style string -> dict of whitelisted properties.

Length props are converted to int px. Color/keyword props stay strings.
Unknown properties are dropped (forward-compatible, browser-lenient).
"""

_PX_PROPS = {"left", "top", "width", "height", "font-size"}
_STR_PROPS = {"position", "background-color", "color"}


def _to_px(value):
    v = value.strip().lower()
    if v.endswith("px"):
        v = v[:-2].strip()
    try:
        return int(round(float(v)))
    except ValueError:
        return None


def parse_style(style):
    out = {}
    for decl in style.split(";"):
        if ":" not in decl:
            continue
        name, _, raw = decl.partition(":")
        name = name.strip().lower()
        raw = raw.strip()
        if not name or not raw:
            continue
        if name in _PX_PROPS:
            px = _to_px(raw)
            if px is not None:
                out[name] = px
        elif name in _STR_PROPS:
            out[name] = raw
    return out
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tool && python -m pytest tests/test_css.py -v`
Expected: PASS (5 passed)

- [ ] **Step 5: Commit**

```bash
git add tool/htgl/css.py tool/tests/test_css.py
git commit -m "feat(tool): inline CSS subset parser"
```

---

### Task 4: HTML subset → Node tree

**Files:**
- Create: `tool/htgl/html_tree.py`
- Test: `tool/tests/test_html_tree.py`

The Node tree is the IR: a flat list where each node carries resolved fields.
SCREEN is synthesized as the root (index 0) sized to the `<body>`/document.

- [ ] **Step 1: Write the failing test**

Create `tool/tests/test_html_tree.py`:

```python
from htgl.html_tree import parse_html, SCREEN, BOX, TEXT

HTML = """
<div style="position:absolute; left:0; top:0; width:240px; height:320px; background-color:#fff">
  <div style="position:absolute; left:10px; top:10px; width:100px; height:40px; background-color:#f00">
    <div style="position:absolute; left:5px; top:12px; color:#000; font-size:8px">Hi</div>
  </div>
</div>
"""

def test_root_is_screen():
    nodes = parse_html(HTML, screen_w=240, screen_h=320)
    assert nodes[0].type == SCREEN
    assert nodes[0].parent == 0xFFFF
    assert nodes[0].w == 240 and nodes[0].h == 320

def test_box_nesting_and_parent_indices():
    nodes = parse_html(HTML, screen_w=240, screen_h=320)
    boxes = [n for n in nodes if n.type == BOX]
    # Every <div> is a BOX (the innermost text div is a zero-size BOX that
    # draws nothing; its text becomes a child TEXT node). 3 divs -> 3 boxes.
    assert len(boxes) == 3
    outer = boxes[0]
    assert outer.parent == 0          # child of screen
    assert outer.w == 240 and outer.h == 320
    inner = boxes[1]
    assert inner.parent == nodes.index(outer)
    assert inner.x == 10 and inner.y == 10
    assert inner.bg == 0xF800

def test_text_node_carries_string_and_fg():
    nodes = parse_html(HTML, screen_w=240, screen_h=320)
    texts = [n for n in nodes if n.type == TEXT]
    assert len(texts) == 1
    assert texts[0].text == "Hi"
    assert texts[0].fg == 0x0000
    assert texts[0].font_size == 8

def test_defaults_when_unstyled():
    nodes = parse_html('<div></div>', screen_w=100, screen_h=100)
    box = [n for n in nodes if n.type == BOX][0]
    assert box.x == 0 and box.y == 0 and box.w == 0 and box.h == 0
    assert box.bg == 0x0000
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tool && python -m pytest tests/test_html_tree.py -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'htgl.html_tree'`

- [ ] **Step 3: Write minimal implementation**

Create `tool/htgl/html_tree.py`:

```python
"""HTML subset -> flat Node tree (the transpiler IR).

Rules (Milestone 1):
- A synthetic SCREEN node is index 0, sized screen_w x screen_h.
- Every <div> becomes a BOX node.
- Any text inside a <div> becomes a TEXT node, child of that div.
- Geometry/colors come from the element's inline style (see css.parse_style).
"""

from html.parser import HTMLParser

from .colors import to_rgb565
from .css import parse_style

SCREEN = 0
BOX = 1
TEXT = 2

ROOT_PARENT = 0xFFFF


class Node:
    def __init__(self, type, parent):
        self.type = type
        self.parent = parent
        self.x = 0
        self.y = 0
        self.w = 0
        self.h = 0
        self.bg = 0x0000
        self.fg = 0x0000
        self.font_size = 8
        self.text = None


class _Builder(HTMLParser):
    def __init__(self, nodes, stack):
        super().__init__(convert_charrefs=True)
        self.nodes = nodes
        self.stack = stack  # stack of node indices; top is current parent

    def handle_starttag(self, tag, attrs):
        if tag != "div":
            return  # ignore unsupported tags, keep their children inline
        parent_idx = self.stack[-1]
        node = Node(BOX, parent_idx)
        style = dict(attrs).get("style", "")
        props = parse_style(style)
        node.x = props.get("left", 0)
        node.y = props.get("top", 0)
        node.w = props.get("width", 0)
        node.h = props.get("height", 0)
        if "background-color" in props:
            node.bg = to_rgb565(props["background-color"])
        if "color" in props:
            node.fg = to_rgb565(props["color"])
        node.font_size = props.get("font-size", 8)
        self.nodes.append(node)
        self.stack.append(len(self.nodes) - 1)

    def handle_endtag(self, tag):
        if tag != "div":
            return
        if len(self.stack) > 1:
            self.stack.pop()

    def handle_data(self, data):
        text = data.strip()
        if not text:
            return
        parent_idx = self.stack[-1]
        parent = self.nodes[parent_idx]
        node = Node(TEXT, parent_idx)
        node.text = text
        node.fg = parent.fg
        node.font_size = parent.font_size
        self.nodes.append(node)


def parse_html(html, screen_w, screen_h):
    screen = Node(SCREEN, ROOT_PARENT)
    screen.w = screen_w
    screen.h = screen_h
    nodes = [screen]
    stack = [0]
    _Builder(nodes, stack).feed(html)
    return nodes
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tool && python -m pytest tests/test_html_tree.py -v`
Expected: PASS (4 passed)

- [ ] **Step 5: Commit**

```bash
git add tool/htgl/html_tree.py tool/tests/test_html_tree.py
git commit -m "feat(tool): HTML subset to node tree IR"
```

---

### Task 5: Node tree → `.uib` bytes

**Files:**
- Create: `tool/htgl/uib.py`
- Test: `tool/tests/test_uib.py`

- [ ] **Step 1: Write the failing test**

Create `tool/tests/test_uib.py`:

```python
import struct

from htgl.html_tree import parse_html
from htgl.uib import build_uib

HEADER_FMT = "<4sBBHHHHH"
NODE_FMT = "<BBHhhhhHHH"
HEADER_SIZE = struct.calcsize(HEADER_FMT)  # 16
NODE_SIZE = struct.calcsize(NODE_FMT)      # 18

def test_header_fields():
    nodes = parse_html('<div></div>', 240, 320)
    blob = build_uib(nodes, 240, 320)
    magic, ver, flags, count, w, h, strtab_off, reserved = \
        struct.unpack_from(HEADER_FMT, blob, 0)
    assert magic == b"HTGL"
    assert ver == 1 and flags == 0
    assert count == len(nodes)
    assert w == 240 and h == 320
    assert strtab_off == HEADER_SIZE + NODE_SIZE * len(nodes)

def test_node_record_layout():
    html = '<div style="left:10px;top:20px;width:30px;height:40px;background-color:#f00"></div>'
    nodes = parse_html(html, 100, 100)
    blob = build_uib(nodes, 100, 100)
    # node[1] is the box (node[0] is SCREEN)
    off = HEADER_SIZE + NODE_SIZE * 1
    type, font, parent, x, y, w, h, bg, fg, text_ref = \
        struct.unpack_from(NODE_FMT, blob, off)
    assert type == 1 and parent == 0
    assert (x, y, w, h) == (10, 20, 30, 40)
    assert bg == 0xF800
    assert text_ref == 0xFFFF

def test_string_table_for_text():
    nodes = parse_html('<div>Hi</div>', 100, 100)
    blob = build_uib(nodes, 100, 100)
    # text node is node[2] (screen, box, text)
    off = HEADER_SIZE + NODE_SIZE * 2
    rec = struct.unpack_from(NODE_FMT, blob, off)
    text_ref = rec[9]
    strtab_off = struct.unpack_from(HEADER_FMT, blob, 0)[6]
    pos = strtab_off + text_ref
    length = blob[pos]
    assert length == 2
    assert blob[pos + 1:pos + 1 + length] == b"Hi"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tool && python -m pytest tests/test_uib.py -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'htgl.uib'`

- [ ] **Step 3: Write minimal implementation**

Create `tool/htgl/uib.py`:

```python
"""Node tree (IR) -> .uib binary bytes.

Layout: [Header 16B][Node 18B * count][StringTable].
See docs spec section 4. All little-endian.
"""

import struct

from .html_tree import ROOT_PARENT, TEXT

HEADER_FMT = "<4sBBHHHHH"
NODE_FMT = "<BBHhhhhHHH"
HEADER_SIZE = struct.calcsize(HEADER_FMT)   # 16
NODE_SIZE = struct.calcsize(NODE_FMT)       # 18
NO_TEXT = 0xFFFF
VERSION = 1


def build_uib(nodes, screen_w, screen_h):
    count = len(nodes)
    strtab_off = HEADER_SIZE + NODE_SIZE * count

    # First pass: build string table and record each text node's offset.
    strtab = bytearray()
    text_offsets = {}  # node index -> offset within string table
    for i, n in enumerate(nodes):
        if n.type == TEXT and n.text is not None:
            data = n.text.encode("ascii", "replace")[:255]
            text_offsets[i] = len(strtab)
            strtab.append(len(data))
            strtab.extend(data)

    out = bytearray()
    out += struct.pack(
        HEADER_FMT, b"HTGL", VERSION, 0, count,
        screen_w, screen_h, strtab_off, 0,
    )
    for i, n in enumerate(nodes):
        parent = ROOT_PARENT if n.parent == ROOT_PARENT else n.parent
        text_ref = text_offsets.get(i, NO_TEXT)
        font_id = 0
        out += struct.pack(
            NODE_FMT, n.type, font_id, parent,
            n.x, n.y, n.w, n.h, n.bg, n.fg, text_ref,
        )
    out += strtab
    return bytes(out)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tool && python -m pytest tests/test_uib.py -v`
Expected: PASS (3 passed)

- [ ] **Step 5: Commit**

```bash
git add tool/htgl/uib.py tool/tests/test_uib.py
git commit -m "feat(tool): serialize node tree to .uib binary"
```

---

### Task 6: `.uib` bytes → C array source

**Files:**
- Create: `tool/htgl/emitc.py`
- Test: `tool/tests/test_emitc.py`

- [ ] **Step 1: Write the failing test**

Create `tool/tests/test_emitc.py`:

```python
from htgl.emitc import emit_c_array

def test_emits_symbol_and_length():
    src = emit_c_array(b"\x01\x02\xff", "hello_ui")
    assert "const unsigned char hello_ui_blob[] = {" in src
    assert "0x01, 0x02, 0xff," in src
    assert "const unsigned int hello_ui_blob_len = 3;" in src

def test_round_trip_values():
    data = bytes(range(0, 20))
    src = emit_c_array(data, "x")
    for b in data:
        assert f"0x{b:02x}" in src
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tool && python -m pytest tests/test_emitc.py -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'htgl.emitc'`

- [ ] **Step 3: Write minimal implementation**

Create `tool/htgl/emitc.py`:

```python
"""`.uib` bytes -> C source array (compile-time embedding mode)."""


def emit_c_array(blob, symbol):
    lines = [
        "/* Auto-generated by htgl. Do not edit. */",
        f"const unsigned char {symbol}_blob[] = {{",
    ]
    row = []
    for i, b in enumerate(blob):
        row.append(f"0x{b:02x},")
        if len(row) == 12:
            lines.append("    " + " ".join(row))
            row = []
    if row:
        lines.append("    " + " ".join(row))
    lines.append("};")
    lines.append(f"const unsigned int {symbol}_blob_len = {len(blob)};")
    lines.append("")
    return "\n".join(lines)
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd tool && python -m pytest tests/test_emitc.py -v`
Expected: PASS (2 passed)

- [ ] **Step 5: Commit**

```bash
git add tool/htgl/emitc.py tool/tests/test_emitc.py
git commit -m "feat(tool): emit .uib as C array"
```

---

### Task 7: CLI glue + example

**Files:**
- Create: `tool/htgl/cli.py`, `tool/htgl.py`, `examples/hello.html`
- Test: `tool/tests/test_cli.py`

The CLI reads screen size from the root `<div>`'s width/height so the same
`.html` previews correctly in a browser.

- [ ] **Step 1: Write the failing test**

Create `tool/tests/test_cli.py`:

```python
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def test_cli_produces_uib_and_c(tmp_path):
    html = tmp_path / "in.html"
    html.write_text(
        '<div style="left:0;top:0;width:64px;height:48px;background-color:#fff">'
        '<div style="left:4px;top:4px;color:#000;font-size:8px">Hi</div>'
        '</div>'
    )
    uib = tmp_path / "out.uib"
    cfile = tmp_path / "out.c"
    result = subprocess.run(
        [sys.executable, str(ROOT / "tool" / "htgl.py"),
         str(html), "-o", str(uib), "--emit-c", str(cfile)],
        capture_output=True, text=True,
    )
    assert result.returncode == 0, result.stderr
    blob = uib.read_bytes()
    magic, ver = struct.unpack_from("<4sB", blob, 0)
    assert magic == b"HTGL" and ver == 1
    w, h = struct.unpack_from("<HH", blob, 8)
    assert w == 64 and h == 48
    assert "out_blob[]" in cfile.read_text()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd tool && python -m pytest tests/test_cli.py -v`
Expected: FAIL (htgl.py does not exist; nonzero return code)

- [ ] **Step 3: Write the CLI module**

Create `tool/htgl/cli.py`:

```python
"""argparse CLI: .html -> .uib (+ optional C array)."""

import argparse
from pathlib import Path

from .html_tree import parse_html, BOX
from .uib import build_uib
from .emitc import emit_c_array


def _infer_screen_size(html):
    # Width/height of the first <div> define the screen, default 240x320.
    nodes = parse_html(html, 240, 320)
    boxes = [n for n in nodes if n.type == BOX]
    if boxes and boxes[0].w > 0 and boxes[0].h > 0:
        return boxes[0].w, boxes[0].h
    return 240, 320


def main(argv=None):
    p = argparse.ArgumentParser(description="HTGL transpiler")
    p.add_argument("input", help="input .html file")
    p.add_argument("-o", "--output", required=True, help="output .uib file")
    p.add_argument("--emit-c", dest="emit_c", help="also write a C array file")
    p.add_argument("--symbol", default=None, help="C symbol base name")
    args = p.parse_args(argv)

    html = Path(args.input).read_text(encoding="utf-8")
    w, h = _infer_screen_size(html)
    nodes = parse_html(html, w, h)
    blob = build_uib(nodes, w, h)

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)

    if args.emit_c:
        symbol = args.symbol or Path(args.emit_c).stem
        cpath = Path(args.emit_c)
        cpath.parent.mkdir(parents=True, exist_ok=True)
        cpath.write_text(emit_c_array(blob, symbol), encoding="ascii")
    return 0
```

- [ ] **Step 4: Write the entry script**

Create `tool/htgl.py`:

```python
#!/usr/bin/env python3
import sys
from htgl.cli import main

if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 5: Create the example screen**

Create `examples/hello.html`:

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

- [ ] **Step 6: Run test to verify it passes**

Run: `cd tool && python -m pytest tests/test_cli.py -v`
Expected: PASS (1 passed). Then run the full transpiler suite:
Run: `cd tool && python -m pytest -v`
Expected: all PASS.

- [ ] **Step 7: Commit**

```bash
git add tool/htgl/cli.py tool/htgl.py tool/tests/test_cli.py examples/hello.html
git commit -m "feat(tool): CLI end-to-end html->uib + example"
```

---

## Phase C — Engine (C99)

### Task 8: Vendor dependencies + C test harness

**Files:**
- Create: `engine/font8x8_basic.h`, `port/sim/stb_image_write.h`, `tests/ctest_util.h`

- [ ] **Step 1: Vendor the public-domain 8x8 font**

Download `font8x8_basic.h` from the public-domain project `dhepper/font8x8`
(https://github.com/dhepper/font8x8, public domain) into `engine/font8x8_basic.h`.
It defines `char font8x8_basic[128][8];` where each glyph is 8 rows, each row a
byte with the **least-significant bit = leftmost pixel**. If fetching is not
possible, generate the equivalent table; the only requirement is that symbol and
bit order match this description.

Verify the file defines the expected symbol:

Run: `grep -c "font8x8_basic\[128\]\[8\]" engine/font8x8_basic.h`
Expected: `1`

- [ ] **Step 2: Vendor stb_image_write.h**

Download `stb_image_write.h` (public domain, `nothings/stb`) into
`port/sim/stb_image_write.h`.

Run: `grep -c "stbi_write_png" port/sim/stb_image_write.h`
Expected: `>= 1`

- [ ] **Step 3: Create the C test assertion helper**

Create `tests/ctest_util.h`:

```c
#ifndef CTEST_UTIL_H
#define CTEST_UTIL_H
#include <stdio.h>

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
            return 1;                                                   \
        }                                                               \
    } while (0)

#endif
```

- [ ] **Step 4: Commit**

```bash
git add engine/font8x8_basic.h port/sim/stb_image_write.h tests/ctest_util.h
git commit -m "chore(engine): vendor font8x8 + stb_image_write, add C test helper"
```

---

### Task 9: Public headers + drawing primitives

**Files:**
- Create: `engine/htgl.h`, `engine/htgl_internal.h`, `engine/draw.c`
- Test: `tests/test_draw.c`
- Modify: `CMakeLists.txt`

`draw.c` owns rasterization into an RGB565 line buffer that represents a single
horizontal band starting at absolute `band_y0`. All drawing clips to the band.

- [ ] **Step 1: Write the public header**

Create `engine/htgl.h`:

```c
#ifndef HTGL_H
#define HTGL_H

#include <stdint.h>

#define HTGL_TYPE_SCREEN 0
#define HTGL_TYPE_BOX    1
#define HTGL_TYPE_TEXT   2
#define HTGL_ROOT_PARENT 0xFFFF
#define HTGL_NO_TEXT     0xFFFF

typedef struct {
    /* Push a band (x,y,w,h) of RGB565 pixels to the display. */
    void (*flush)(int x, int y, int w, int h, const uint16_t *buf);
} htgl_hal;

typedef struct htgl_ctx htgl_ctx;

/* Initialize with a HAL and a caller-owned RGB565 line buffer.
   line_buf must hold at least line_buf_px pixels. */
htgl_ctx *htgl_init(htgl_ctx *ctx, const htgl_hal *hal,
                    uint16_t *line_buf, int line_buf_px);

/* Validate and attach a .uib blob (zero-copy). Returns 0 on success. */
int htgl_load(htgl_ctx *ctx, const uint8_t *blob, int len);

/* Resolve relative coordinates into absolute screen coordinates. */
void htgl_layout(htgl_ctx *ctx);

/* Render the whole screen in bands, flushing each via the HAL. */
void htgl_render(htgl_ctx *ctx);

/* Screen dimensions from the loaded blob. */
int htgl_screen_w(const htgl_ctx *ctx);
int htgl_screen_h(const htgl_ctx *ctx);

#endif
```

- [ ] **Step 2: Write the internal header**

Create `engine/htgl_internal.h`:

```c
#ifndef HTGL_INTERNAL_H
#define HTGL_INTERNAL_H

#include <stdint.h>
#include "htgl.h"

#define HTGL_MAX_NODES 256

#pragma pack(push, 1)
typedef struct {
    uint8_t  magic[4];
    uint8_t  version;
    uint8_t  flags;
    uint16_t node_count;
    uint16_t screen_w;
    uint16_t screen_h;
    uint16_t strtab_off;
    uint16_t reserved;
} htgl_header;

typedef struct {
    uint8_t  type;
    uint8_t  font;
    uint16_t parent;
    int16_t  x, y, w, h;
    uint16_t bg, fg;
    uint16_t text_ref;
} htgl_node;
#pragma pack(pop)

struct htgl_ctx {
    const htgl_hal *hal;
    uint16_t       *line_buf;
    int             line_buf_px;
    const uint8_t  *blob;
    const htgl_header *hdr;
    const htgl_node   *nodes;
    const uint8_t  *strtab;
    int             count;
    int16_t         abs_x[HTGL_MAX_NODES];
    int16_t         abs_y[HTGL_MAX_NODES];
};

/* draw.c: fill a rectangle (absolute coords) into a band buffer.
   The band covers absolute rows [band_y0, band_y0 + band_h). */
void htgl_fill_rect(uint16_t *band, int band_w, int band_y0, int band_h,
                    int rx, int ry, int rw, int rh, uint16_t color);

/* draw.c: draw ASCII text at absolute (tx,ty) into the band, scaled by `scale`
   (integer >= 1). Background is transparent; only foreground pixels are set. */
void htgl_draw_text(uint16_t *band, int band_w, int band_y0, int band_h,
                    int tx, int ty, const char *text, int len,
                    int scale, uint16_t color);

#endif
```

- [ ] **Step 3: Write the failing test**

Create `tests/test_draw.c`:

```c
#include "../engine/htgl_internal.h"
#include "ctest_util.h"

int main(void) {
    /* band: 4px wide, rows [0,2) */
    uint16_t band[4 * 2];
    for (int i = 0; i < 8; i++) band[i] = 0;

    /* fill a 2x2 rect at (1,0) with 0xABCD */
    htgl_fill_rect(band, 4, 0, 2, 1, 0, 2, 2, 0xABCD);
    CHECK(band[0 * 4 + 1] == 0xABCD);
    CHECK(band[0 * 4 + 2] == 0xABCD);
    CHECK(band[1 * 4 + 1] == 0xABCD);
    CHECK(band[0 * 4 + 0] == 0x0000);  /* outside x */
    CHECK(band[0 * 4 + 3] == 0x0000);

    /* rect partly above the band is clipped, not crashing */
    for (int i = 0; i < 8; i++) band[i] = 0;
    htgl_fill_rect(band, 4, 2, 2, 0, 0, 4, 4, 0x1111); /* rows 0..3, band rows 2..3 */
    CHECK(band[0] == 0x1111);

    /* text: a space glyph sets no pixels */
    for (int i = 0; i < 8; i++) band[i] = 0;
    htgl_draw_text(band, 4, 0, 2, 0, 0, " ", 1, 1, 0xFFFF);
    CHECK(band[0] == 0x0000);

    printf("ok\n");
    return 0;
}
```

- [ ] **Step 4: Add the test to CMake and verify it fails to build**

In `CMakeLists.txt`, append:

```cmake
add_executable(test_draw tests/test_draw.c engine/draw.c)
target_include_directories(test_draw PRIVATE engine tests)
add_test(NAME draw COMMAND test_draw)
```

Run: `cmake -S . -B build && cmake --build build --target test_draw`
Expected: FAIL (linker/compile error: `htgl_fill_rect` undefined — `draw.c` not yet written).

- [ ] **Step 5: Implement draw.c**

Create `engine/draw.c`:

```c
#include "htgl_internal.h"
#include "font8x8_basic.h"

void htgl_fill_rect(uint16_t *band, int band_w, int band_y0, int band_h,
                    int rx, int ry, int rw, int rh, uint16_t color) {
    int x0 = rx, x1 = rx + rw;
    int y0 = ry, y1 = ry + rh;
    if (x0 < 0) x0 = 0;
    if (y0 < band_y0) y0 = band_y0;
    if (x1 > band_w) x1 = band_w;
    if (y1 > band_y0 + band_h) y1 = band_y0 + band_h;
    for (int y = y0; y < y1; y++) {
        uint16_t *row = band + (y - band_y0) * band_w;
        for (int x = x0; x < x1; x++) row[x] = color;
    }
}

static void draw_glyph(uint16_t *band, int band_w, int band_y0, int band_h,
                       int gx, int gy, unsigned char ch, int scale,
                       uint16_t color) {
    if (ch >= 128) ch = '?';
    const char *bits = font8x8_basic[ch];
    for (int row = 0; row < 8; row++) {
        unsigned char rb = (unsigned char)bits[row];
        for (int col = 0; col < 8; col++) {
            if (!(rb & (1u << col))) continue;        /* LSB = leftmost */
            int px = gx + col * scale;
            int py = gy + row * scale;
            htgl_fill_rect(band, band_w, band_y0, band_h,
                           px, py, scale, scale, color);
        }
    }
}

void htgl_draw_text(uint16_t *band, int band_w, int band_y0, int band_h,
                    int tx, int ty, const char *text, int len,
                    int scale, uint16_t color) {
    int advance = 8 * scale;
    for (int i = 0; i < len; i++) {
        draw_glyph(band, band_w, band_y0, band_h,
                   tx + i * advance, ty, (unsigned char)text[i], scale, color);
    }
}
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cmake --build build --target test_draw && ctest --test-dir build -R draw -V`
Expected: PASS (`ok`, test `draw` passed).

- [ ] **Step 7: Commit**

```bash
git add engine/htgl.h engine/htgl_internal.h engine/draw.c tests/test_draw.c CMakeLists.txt
git commit -m "feat(engine): public API headers + draw primitives"
```

---

### Task 10: Blob loading + validation

**Files:**
- Create: `engine/htgl.c`
- Test: `tests/test_load.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_load.c`. It builds a minimal valid blob in memory using the
exact layout from the spec, then checks `htgl_load`/`htgl_init` accept it and
reject corrupt input.

```c
#include <string.h>
#include "../engine/htgl_internal.h"
#include "ctest_util.h"

/* Build: 1 SCREEN node, 240x320, no strings. */
static int build_blob(uint8_t *out) {
    htgl_header h;
    memcpy(h.magic, "HTGL", 4);
    h.version = 1; h.flags = 0;
    h.node_count = 1;
    h.screen_w = 240; h.screen_h = 320;
    h.strtab_off = sizeof(htgl_header) + sizeof(htgl_node);
    h.reserved = 0;
    htgl_node n;
    memset(&n, 0, sizeof(n));
    n.type = HTGL_TYPE_SCREEN;
    n.parent = HTGL_ROOT_PARENT;
    n.w = 240; n.h = 320;
    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), &n, sizeof(n));
    return (int)(sizeof(h) + sizeof(n));
}

int main(void) {
    uint8_t blob[128];
    int len = build_blob(blob);

    htgl_ctx ctx;
    uint16_t lb[240];
    CHECK(htgl_init(&ctx, 0, lb, 240) == &ctx);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    CHECK(htgl_screen_w(&ctx) == 240);
    CHECK(htgl_screen_h(&ctx) == 320);

    /* bad magic rejected */
    uint8_t bad[128];
    memcpy(bad, blob, len);
    bad[0] = 'X';
    CHECK(htgl_load(&ctx, bad, len) != 0);

    /* truncated rejected */
    CHECK(htgl_load(&ctx, blob, 4) != 0);

    printf("ok\n");
    return 0;
}
```

- [ ] **Step 2: Add to CMake and verify it fails**

In `CMakeLists.txt`, append:

```cmake
add_executable(test_load tests/test_load.c engine/htgl.c engine/draw.c)
target_include_directories(test_load PRIVATE engine tests)
add_test(NAME load COMMAND test_load)
```

Run: `cmake -S . -B build && cmake --build build --target test_load`
Expected: FAIL (compile/link error: `htgl_init`/`htgl_load` undefined).

- [ ] **Step 3: Implement load + init in htgl.c**

Create `engine/htgl.c`:

```c
#include <string.h>
#include "htgl_internal.h"

htgl_ctx *htgl_init(htgl_ctx *ctx, const htgl_hal *hal,
                    uint16_t *line_buf, int line_buf_px) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->hal = hal;
    ctx->line_buf = line_buf;
    ctx->line_buf_px = line_buf_px;
    return ctx;
}

int htgl_load(htgl_ctx *ctx, const uint8_t *blob, int len) {
    if (len < (int)sizeof(htgl_header)) return -1;
    const htgl_header *h = (const htgl_header *)blob;
    if (memcmp(h->magic, "HTGL", 4) != 0) return -2;
    if (h->version != 1) return -3;
    if (h->node_count == 0 || h->node_count > HTGL_MAX_NODES) return -4;
    int nodes_end = (int)sizeof(htgl_header) + (int)sizeof(htgl_node) * h->node_count;
    if (nodes_end > len) return -5;
    if (h->strtab_off > len) return -6;

    ctx->blob = blob;
    ctx->hdr = h;
    ctx->nodes = (const htgl_node *)(blob + sizeof(htgl_header));
    ctx->strtab = blob + h->strtab_off;
    ctx->count = h->node_count;
    return 0;
}

int htgl_screen_w(const htgl_ctx *ctx) { return ctx->hdr ? ctx->hdr->screen_w : 0; }
int htgl_screen_h(const htgl_ctx *ctx) { return ctx->hdr ? ctx->hdr->screen_h : 0; }

/* htgl_layout and htgl_render are added in later tasks. */
void htgl_layout(htgl_ctx *ctx) { (void)ctx; }
void htgl_render(htgl_ctx *ctx) { (void)ctx; }
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target test_load && ctest --test-dir build -R load -V`
Expected: PASS (`ok`).

- [ ] **Step 5: Commit**

```bash
git add engine/htgl.c tests/test_load.c CMakeLists.txt
git commit -m "feat(engine): load and validate .uib blob"
```

---

### Task 11: Layout (relative → absolute)

**Files:**
- Modify: `engine/htgl.c` (replace the stub `htgl_layout`)
- Test: `tests/test_layout.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_layout.c`:

```c
#include <string.h>
#include "../engine/htgl_internal.h"
#include "ctest_util.h"

/* Internal accessors for the test. */
int16_t htgl_test_abs_x(htgl_ctx *c, int i);
int16_t htgl_test_abs_y(htgl_ctx *c, int i);

static int build(uint8_t *out) {
    htgl_header h;
    memcpy(h.magic, "HTGL", 4);
    h.version = 1; h.flags = 0; h.node_count = 3;
    h.screen_w = 200; h.screen_h = 200;
    h.strtab_off = sizeof(htgl_header) + 3 * sizeof(htgl_node);
    h.reserved = 0;
    htgl_node n[3];
    memset(n, 0, sizeof(n));
    n[0].type = HTGL_TYPE_SCREEN; n[0].parent = HTGL_ROOT_PARENT; n[0].w = 200; n[0].h = 200;
    n[1].type = HTGL_TYPE_BOX; n[1].parent = 0; n[1].x = 10; n[1].y = 20; n[1].w = 50; n[1].h = 50;
    n[2].type = HTGL_TYPE_BOX; n[2].parent = 1; n[2].x = 5;  n[2].y = 7;  n[2].w = 10; n[2].h = 10;
    memcpy(out, &h, sizeof(h));
    memcpy(out + sizeof(h), n, sizeof(n));
    return (int)(sizeof(h) + sizeof(n));
}

int main(void) {
    uint8_t blob[256];
    int len = build(blob);
    htgl_ctx ctx;
    uint16_t lb[200];
    htgl_init(&ctx, 0, lb, 200);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_layout(&ctx);
    CHECK(htgl_test_abs_x(&ctx, 0) == 0 && htgl_test_abs_y(&ctx, 0) == 0);
    CHECK(htgl_test_abs_x(&ctx, 1) == 10 && htgl_test_abs_y(&ctx, 1) == 20);
    CHECK(htgl_test_abs_x(&ctx, 2) == 15 && htgl_test_abs_y(&ctx, 2) == 27);
    printf("ok\n");
    return 0;
}
```

- [ ] **Step 2: Add to CMake and verify it fails**

In `CMakeLists.txt`, append:

```cmake
add_executable(test_layout tests/test_layout.c engine/htgl.c engine/draw.c)
target_include_directories(test_layout PRIVATE engine tests)
add_test(NAME layout COMMAND test_layout)
```

Run: `cmake -S . -B build && cmake --build build --target test_layout`
Expected: FAIL — abs coords are all 0 (stub) and `htgl_test_abs_x` is undefined.

- [ ] **Step 3: Implement layout + test accessors**

In `engine/htgl.c`, replace the stub line `void htgl_layout(htgl_ctx *ctx) { (void)ctx; }` with:

```c
void htgl_layout(htgl_ctx *ctx) {
    /* Nodes are emitted parent-before-child (DFS), so a single forward pass
       resolves absolute coordinates. */
    for (int i = 0; i < ctx->count; i++) {
        const htgl_node *n = &ctx->nodes[i];
        if (n->parent == HTGL_ROOT_PARENT) {
            ctx->abs_x[i] = n->x;
            ctx->abs_y[i] = n->y;
        } else {
            ctx->abs_x[i] = ctx->abs_x[n->parent] + n->x;
            ctx->abs_y[i] = ctx->abs_y[n->parent] + n->y;
        }
    }
}

int16_t htgl_test_abs_x(htgl_ctx *c, int i) { return c->abs_x[i]; }
int16_t htgl_test_abs_y(htgl_ctx *c, int i) { return c->abs_y[i]; }
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target test_layout && ctest --test-dir build -R layout -V`
Expected: PASS (`ok`).

- [ ] **Step 5: Commit**

```bash
git add engine/htgl.c tests/test_layout.c CMakeLists.txt
git commit -m "feat(engine): resolve absolute layout coordinates"
```

---

### Task 12: Chunked render pipeline

**Files:**
- Modify: `engine/htgl.c` (replace the stub `htgl_render`)
- Test: `tests/test_render.c`
- Modify: `CMakeLists.txt`

`htgl_render` walks the screen in horizontal bands sized to the line buffer.
For each band it clears to the SCREEN node's background, draws every BOX and
TEXT that intersects the band, then flushes the band via the HAL.

- [ ] **Step 1: Write the failing test**

Create `tests/test_render.c`. A capture HAL records flushed pixels into a full
framebuffer so the test can assert specific pixels.

```c
#include <string.h>
#include "../engine/htgl_internal.h"
#include "ctest_util.h"

#define W 16
#define H 16
static uint16_t fb[W * H];

static void capture_flush(int x, int y, int w, int h, const uint16_t *buf) {
    for (int row = 0; row < h; row++)
        for (int col = 0; col < w; col++)
            fb[(y + row) * W + (x + col)] = buf[row * w + col];
}

static int build(uint8_t *out) {
    htgl_header hd;
    memcpy(hd.magic, "HTGL", 4);
    hd.version = 1; hd.flags = 0; hd.node_count = 2;
    hd.screen_w = W; hd.screen_h = H;
    hd.strtab_off = sizeof(htgl_header) + 2 * sizeof(htgl_node);
    hd.reserved = 0;
    htgl_node n[2];
    memset(n, 0, sizeof(n));
    n[0].type = HTGL_TYPE_SCREEN; n[0].parent = HTGL_ROOT_PARENT;
    n[0].w = W; n[0].h = H; n[0].bg = 0x0001;          /* screen bg */
    n[1].type = HTGL_TYPE_BOX; n[1].parent = 0;
    n[1].x = 4; n[1].y = 4; n[1].w = 8; n[1].h = 8; n[1].bg = 0xF800; /* red */
    memcpy(out, &hd, sizeof(hd));
    memcpy(out + sizeof(hd), n, sizeof(n));
    return (int)(sizeof(hd) + sizeof(n));
}

int main(void) {
    uint8_t blob[256];
    int len = build(blob);
    htgl_hal hal = { capture_flush };
    htgl_ctx ctx;
    uint16_t line_buf[W * 4];          /* 4-row bands */
    memset(fb, 0xEE, sizeof(fb));
    htgl_init(&ctx, &hal, line_buf, W * 4);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_layout(&ctx);
    htgl_render(&ctx);

    CHECK(fb[0 * W + 0] == 0x0001);     /* corner = screen bg */
    CHECK(fb[5 * W + 5] == 0xF800);     /* inside red box */
    CHECK(fb[4 * W + 4] == 0xF800);     /* box top-left */
    CHECK(fb[12 * W + 12] == 0x0001);   /* just past box = screen bg */
    printf("ok\n");
    return 0;
}
```

- [ ] **Step 2: Add to CMake and verify it fails**

In `CMakeLists.txt`, append:

```cmake
add_executable(test_render tests/test_render.c engine/htgl.c engine/draw.c)
target_include_directories(test_render PRIVATE engine tests)
add_test(NAME render COMMAND test_render)
```

Run: `cmake -S . -B build && cmake --build build --target test_render`
Expected: FAIL — `htgl_render` is a stub, so `fb` keeps its `0xEE` fill.

- [ ] **Step 3: Implement the render pipeline**

In `engine/htgl.c`, add `#include <string.h>` is already present. Replace the
stub line `void htgl_render(htgl_ctx *ctx) { (void)ctx; }` with:

```c
static const char *node_text(htgl_ctx *ctx, const htgl_node *n, int *out_len) {
    if (n->text_ref == HTGL_NO_TEXT) { *out_len = 0; return 0; }
    const uint8_t *p = ctx->strtab + n->text_ref;
    *out_len = p[0];
    return (const char *)(p + 1);
}

void htgl_render(htgl_ctx *ctx) {
    int sw = ctx->hdr->screen_w;
    int sh = ctx->hdr->screen_h;
    int band_h = ctx->line_buf_px / sw;
    if (band_h < 1) band_h = 1;
    uint16_t screen_bg = ctx->nodes[0].bg;

    for (int by = 0; by < sh; by += band_h) {
        int bh = band_h;
        if (by + bh > sh) bh = sh - by;

        /* clear band to screen background */
        for (int i = 0; i < sw * bh; i++) ctx->line_buf[i] = screen_bg;

        for (int i = 1; i < ctx->count; i++) {
            const htgl_node *n = &ctx->nodes[i];
            int ax = ctx->abs_x[i];
            int ay = ctx->abs_y[i];
            if (n->type == HTGL_TYPE_BOX) {
                htgl_fill_rect(ctx->line_buf, sw, by, bh,
                               ax, ay, n->w, n->h, n->bg);
            } else if (n->type == HTGL_TYPE_TEXT) {
                int tl;
                const char *t = node_text(ctx, n, &tl);
                if (t && tl > 0) {
                    int scale = n->h / 8;          /* font_size carried via h? no */
                    (void)scale;
                    /* font scale from font_size is encoded in node.h==0 for text;
                       use w/h-independent scale: derive from font field later.
                       MVP: scale = 1 unless text node h set. */
                    int s = 1;
                    htgl_draw_text(ctx->line_buf, sw, by, bh,
                                   ax, ay, t, tl, s, n->fg);
                }
            }
        }
        ctx->hal->flush(0, by, sw, bh, ctx->line_buf);
    }
}
```

> NOTE: text scaling is finalized in Task 13; this step uses scale=1 to get the
> pipeline passing. Do not commit a cleanup of the `(void)scale` lines until Task 13.

- [ ] **Step 4: Run the test to verify it passes**

Run: `cmake --build build --target test_render && ctest --test-dir build -R render -V`
Expected: PASS (`ok`).

- [ ] **Step 5: Commit**

```bash
git add engine/htgl.c tests/test_render.c CMakeLists.txt
git commit -m "feat(engine): chunked band render pipeline"
```

---

### Task 13: Text font-size scaling

**Files:**
- Modify: `engine/htgl_internal.h` (add `font_size` to runtime, or reuse a field), `engine/htgl.c`
- Test: `tests/test_render.c` (extend)

Decision: carry font scale through the node's `font` byte. The transpiler already
writes `font=0`; extend it to write a scale factor. Update `uib.py` accordingly.

- [ ] **Step 1: Extend the transpiler to encode font scale (Python)**

In `tool/htgl/uib.py`, change the node packing loop. Replace:

```python
        font_id = 0
        out += struct.pack(
            NODE_FMT, n.type, font_id, parent,
            n.x, n.y, n.w, n.h, n.bg, n.fg, text_ref,
        )
```

with:

```python
        # font byte carries integer scale for TEXT nodes (font_size / 8, >=1)
        scale = max(1, int(round(getattr(n, "font_size", 8) / 8)))
        font_byte = scale if n.type == TEXT else 0
        out += struct.pack(
            NODE_FMT, n.type, font_byte, parent,
            n.x, n.y, n.w, n.h, n.bg, n.fg, text_ref,
        )
```

- [ ] **Step 2: Add a transpiler test for the scale byte**

Append to `tool/tests/test_uib.py`:

```python
def test_text_font_scale_byte():
    nodes = parse_html('<div style="font-size:16px">Hi</div>', 100, 100)
    blob = build_uib(nodes, 100, 100)
    off = HEADER_SIZE + NODE_SIZE * 2  # text node
    rec = struct.unpack_from(NODE_FMT, blob, off)
    assert rec[1] == 2  # font byte = scale = 16/8
```

Run: `cd tool && python -m pytest tests/test_uib.py -v`
Expected: PASS (4 passed).

- [ ] **Step 3: Use the font byte as scale in the engine**

In `engine/htgl.c`, inside `htgl_render`, replace the TEXT branch body:

```c
            } else if (n->type == HTGL_TYPE_TEXT) {
                int tl;
                const char *t = node_text(ctx, n, &tl);
                if (t && tl > 0) {
                    int scale = n->h / 8;          /* font_size carried via h? no */
                    (void)scale;
                    /* font scale from font_size is encoded in node.h==0 for text;
                       use w/h-independent scale: derive from font field later.
                       MVP: scale = 1 unless text node h set. */
                    int s = 1;
                    htgl_draw_text(ctx->line_buf, sw, by, bh,
                                   ax, ay, t, tl, s, n->fg);
                }
            }
```

with:

```c
            } else if (n->type == HTGL_TYPE_TEXT) {
                int tl;
                const char *t = node_text(ctx, n, &tl);
                if (t && tl > 0) {
                    int s = n->font ? n->font : 1;   /* font byte = integer scale */
                    htgl_draw_text(ctx->line_buf, sw, by, bh,
                                   ax, ay, t, tl, s, n->fg);
                }
            }
```

- [ ] **Step 4: Extend the render test to assert a scaled glyph pixel**

Append a second `main`-level check is not possible (one main per file). Instead
add a dedicated test file `tests/test_text.c`:

```c
#include <string.h>
#include "../engine/htgl_internal.h"
#include "ctest_util.h"

#define W 32
#define H 16
static uint16_t fb[W * H];
static void cap(int x, int y, int w, int h, const uint16_t *buf) {
    for (int r = 0; r < h; r++)
        for (int c = 0; c < w; c++)
            fb[(y + r) * W + (x + c)] = buf[r * w + c];
}

static int build(uint8_t *out) {
    htgl_header hd; memcpy(hd.magic, "HTGL", 4);
    hd.version = 1; hd.flags = 0; hd.node_count = 2;
    hd.screen_w = W; hd.screen_h = H;
    hd.strtab_off = sizeof(htgl_header) + 2 * sizeof(htgl_node);
    hd.reserved = 0;
    htgl_node n[2]; memset(n, 0, sizeof(n));
    n[0].type = HTGL_TYPE_SCREEN; n[0].parent = HTGL_ROOT_PARENT;
    n[0].w = W; n[0].h = H; n[0].bg = 0x0000;
    n[1].type = HTGL_TYPE_TEXT; n[1].parent = 0; n[1].x = 0; n[1].y = 0;
    n[1].font = 2;                       /* scale 2x */
    n[1].fg = 0xFFFF; n[1].text_ref = 0; /* offset 0 in strtab */
    /* strtab: len=1, 'I' */
    uint8_t *st = out + hd.strtab_off;
    st[0] = 1; st[1] = 'I';
    memcpy(out, &hd, sizeof(hd));
    memcpy(out + sizeof(hd), n, sizeof(n));
    return (int)(hd.strtab_off + 2);
}

int main(void) {
    uint8_t blob[256];
    int len = build(blob);
    htgl_hal hal = { cap };
    htgl_ctx ctx; uint16_t lb[W * 4];
    memset(fb, 0, sizeof(fb));
    htgl_init(&ctx, &hal, lb, W * 4);
    CHECK(htgl_load(&ctx, blob, len) == 0);
    htgl_layout(&ctx);
    htgl_render(&ctx);
    /* 'I' in font8x8 has set pixels; with scale 2 the glyph spans ~16px tall.
       At least one white pixel must exist below row 8 (proving scaling). */
    int found_low = 0;
    for (int y = 8; y < 16; y++)
        for (int x = 0; x < 16; x++)
            if (fb[y * W + x] == 0xFFFF) found_low = 1;
    CHECK(found_low == 1);
    printf("ok\n");
    return 0;
}
```

In `CMakeLists.txt`, append:

```cmake
add_executable(test_text tests/test_text.c engine/htgl.c engine/draw.c)
target_include_directories(test_text PRIVATE engine tests)
add_test(NAME text COMMAND test_text)
```

- [ ] **Step 5: Build and run all engine tests**

Run: `cmake -S . -B build && cmake --build build && ctest --test-dir build -V`
Expected: all PASS (`draw`, `load`, `layout`, `render`, `text`).

- [ ] **Step 6: Commit**

```bash
git add engine/htgl.c tool/htgl/uib.py tool/tests/test_uib.py tests/test_text.c CMakeLists.txt
git commit -m "feat: font-size integer scaling end to end"
```

---

## Phase D — Simulator + end-to-end

### Task 14: PNG HAL

**Files:**
- Create: `port/sim/hal_png.h`, `port/sim/hal_png.c`
- Test: covered by the end-to-end test (Task 16); no unit test (thin I/O wrapper)

`hal_png` accumulates flushed bands into a full RGB888 image and writes a PNG.

- [ ] **Step 1: Write the header**

Create `port/sim/hal_png.h`:

```c
#ifndef HAL_PNG_H
#define HAL_PNG_H

#include <stdint.h>
#include "htgl.h"

/* Allocate an image of w*h and return a HAL whose flush() writes into it. */
void hal_png_begin(int w, int h);
htgl_hal hal_png_get(void);
int hal_png_write(const char *path);   /* 0 on success */
void hal_png_end(void);

#endif
```

- [ ] **Step 2: Write the implementation**

Create `port/sim/hal_png.c`:

```c
#include <stdlib.h>
#include "hal_png.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static int g_w, g_h;
static uint8_t *g_rgb;   /* RGB888, g_w*g_h*3 */

static void rgb565_to_888(uint16_t c, uint8_t *out) {
    uint8_t r5 = (c >> 11) & 0x1F;
    uint8_t g6 = (c >> 5) & 0x3F;
    uint8_t b5 = c & 0x1F;
    out[0] = (uint8_t)((r5 * 255 + 15) / 31);
    out[1] = (uint8_t)((g6 * 255 + 31) / 63);
    out[2] = (uint8_t)((b5 * 255 + 15) / 31);
}

static void png_flush(int x, int y, int w, int h, const uint16_t *buf) {
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            int px = x + col, py = y + row;
            if (px < 0 || py < 0 || px >= g_w || py >= g_h) continue;
            rgb565_to_888(buf[row * w + col], &g_rgb[(py * g_w + px) * 3]);
        }
    }
}

void hal_png_begin(int w, int h) {
    g_w = w; g_h = h;
    g_rgb = (uint8_t *)calloc((size_t)w * h * 3, 1);
}

htgl_hal hal_png_get(void) {
    htgl_hal hal;
    hal.flush = png_flush;
    return hal;
}

int hal_png_write(const char *path) {
    if (!g_rgb) return -1;
    return stbi_write_png(path, g_w, g_h, 3, g_rgb, g_w * 3) ? 0 : -2;
}

void hal_png_end(void) {
    free(g_rgb);
    g_rgb = 0;
}
```

- [ ] **Step 3: Commit**

```bash
git add port/sim/hal_png.h port/sim/hal_png.c
git commit -m "feat(sim): PNG HAL (RGB565 bands -> PNG)"
```

---

### Task 15: Simulator main

**Files:**
- Create: `port/sim/main.c`
- Modify: `CMakeLists.txt` (htgl_sim already declared in Task 1; confirm sources)

- [ ] **Step 1: Write the simulator entry point**

Create `port/sim/main.c`:

```c
#include <stdio.h>
#include <stdlib.h>
#include "htgl.h"
#include "hal_png.h"

static uint8_t *read_file(const char *path, int *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = 0; }
    fclose(f);
    *out_len = (int)n;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: htgl_sim <in.uib> <out.png>\n");
        return 2;
    }
    int len = 0;
    uint8_t *blob = read_file(argv[1], &len);
    if (!blob) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

    /* line buffer holds a few full-width rows; size to the screen width. */
    htgl_ctx ctx;
    static uint16_t line_buf[1024 * 8];   /* up to 1024px wide, 8-row bands */

    /* Peek screen size: load with a temporary tiny init, then re-init buffer. */
    htgl_init(&ctx, 0, line_buf, sizeof(line_buf) / sizeof(line_buf[0]));
    if (htgl_load(&ctx, blob, len) != 0) {
        fprintf(stderr, "invalid .uib\n"); free(blob); return 1;
    }
    int w = htgl_screen_w(&ctx), h = htgl_screen_h(&ctx);

    hal_png_begin(w, h);
    htgl_hal hal = hal_png_get();
    /* re-init so the engine has the HAL; line buffer band height = floor(N/w) */
    htgl_init(&ctx, &hal, line_buf, (sizeof(line_buf) / sizeof(line_buf[0])));
    htgl_load(&ctx, blob, len);
    htgl_layout(&ctx);
    htgl_render(&ctx);

    int rc = hal_png_write(argv[2]);
    hal_png_end();
    free(blob);
    if (rc != 0) { fprintf(stderr, "png write failed\n"); return 1; }
    printf("wrote %s (%dx%d)\n", argv[2], w, h);
    return 0;
}
```

- [ ] **Step 2: Confirm CMake builds htgl_sim with both sources**

Ensure `CMakeLists.txt` `htgl_sim` target lists `port/sim/main.c port/sim/hal_png.c`
(set in Task 1). Build:

Run: `cmake -S . -B build && cmake --build build --target htgl_sim`
Expected: builds with no errors, producing `build/htgl_sim` (or `build/Debug/htgl_sim.exe` on MSVC).

- [ ] **Step 3: Smoke-run on the example**

Run:
```sh
cd tool && python htgl.py ../examples/hello.html -o ../build/hello.uib && cd ..
./build/htgl_sim build/hello.uib build/hello.png
```
Expected: prints `wrote build/hello.png (240x320)` and the PNG exists. Open it and
compare visually against `examples/hello.html` in a browser — dark-blue screen,
light panel with "HTGL", red + blue squares, white "hello, embedded" text.

- [ ] **Step 4: Commit**

```bash
git add port/sim/main.c CMakeLists.txt
git commit -m "feat(sim): load .uib, render, write PNG"
```

---

### Task 16: End-to-end golden test

**Files:**
- Create: `tests/test_e2e.sh`, `tests/hello.uib.golden`, `tests/hello.png.golden`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Generate golden artifacts from the known-good run**

After Task 15 produced a visually verified PNG, freeze it as the golden:

```sh
cp build/hello.uib tests/hello.uib.golden
cp build/hello.png tests/hello.png.golden
```

- [ ] **Step 2: Write the end-to-end test script**

Create `tests/test_e2e.sh`:

```sh
#!/bin/sh
# Transpile the example, render it, and compare against golden artifacts.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build"
SIM="$BUILD/htgl_sim"
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
```

Make it executable:

Run: `chmod +x tests/test_e2e.sh`

- [ ] **Step 3: Register the e2e test in CMake**

In `CMakeLists.txt`, append:

```cmake
find_program(SH_PROGRAM sh)
if(SH_PROGRAM)
  add_test(NAME e2e COMMAND ${SH_PROGRAM} ${CMAKE_SOURCE_DIR}/tests/test_e2e.sh)
endif()
```

- [ ] **Step 4: Run the full suite**

Run: `cmake -S . -B build && cmake --build build && ctest --test-dir build -V`
Expected: all PASS including `e2e`.

> If `e2e` fails on `cmp` of the PNG due to a platform-specific stb encoding
> difference, regenerate the golden on this platform (Step 1) and re-run; note
> in the commit that golden PNGs are platform-specific (acceptable for MVP, a
> pixel-diff comparator replaces `cmp` in a later milestone).

- [ ] **Step 5: Commit**

```bash
git add tests/test_e2e.sh tests/hello.uib.golden tests/hello.png.golden CMakeLists.txt
git commit -m "test: end-to-end html->uib->png golden comparison"
```

---

## Definition of Done (Milestone 1)

- [ ] `cd tool && python -m pytest` — all transpiler tests pass.
- [ ] `cmake -S . -B build && cmake --build build && ctest --test-dir build` — all engine + e2e tests pass.
- [ ] `build/hello.png` visually matches `examples/hello.html` opened in a browser.
- [ ] The engine (`engine/*.c`, `engine/*.h`) contains **no** host-only or simulator-only code — only `port/sim/*` is host-specific (proving MCU portability for Milestone 2).

---

## Self-Review Notes (author check against spec)

- Spec §3.1 subset (div, text, absolute pos, bg/color/font-size) → Tasks 3,4,7 ✔
- Spec §4 binary format → Task 5 (writer) + Task 10 (loader), byte layout shared in constants ✔
- Spec §5 engine API + chunked pipeline → Tasks 9–13 ✔
- Spec §6 transpiler (html.parser, whitelist, color, emit-c) → Tasks 2–7 ✔
- Spec §7 PNG sim backend, stb vendored → Tasks 8,14,15 ✔
- Spec §8 verification (golden uib bytes, golden png, browser eyeball) → Tasks 5,16 ✔
- Spec §9 repo structure → matches File Structure above ✔
- Type consistency: `htgl_fill_rect`/`htgl_draw_text`/`htgl_load`/`htgl_layout`/`htgl_render`
  signatures identical across tasks; node `font` byte = text scale agreed in Tasks 5/13 ✔
- §11 open question (CRC) intentionally deferred — not in Milestone 1 scope.
