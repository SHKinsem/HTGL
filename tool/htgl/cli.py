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
