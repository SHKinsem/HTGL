"""Stitch a sequence of PNG frames into a looping GIF.

Usage:
    python stitch_gif.py <frames_dir> <output.gif> [frame_duration_ms]

Globs <frames_dir>/*.png in sorted order (lexicographic, matching the NNN naming).
Default frame duration is 83ms (~12fps display for 24 frames over ~2s).
"""

import sys
import glob
import os
from PIL import Image


def main():
    if len(sys.argv) < 3:
        print("usage: stitch_gif.py <frames_dir> <output.gif> [frame_ms]")
        sys.exit(1)

    frames_dir = sys.argv[1]
    out_path = sys.argv[2]
    frame_ms = int(sys.argv[3]) if len(sys.argv) >= 4 else 83

    pattern = os.path.join(frames_dir, "*.png")
    paths = sorted(glob.glob(pattern))
    if not paths:
        print(f"no PNG files found in {frames_dir!r}")
        sys.exit(1)

    frames = [Image.open(p).convert("RGBA") for p in paths]
    first = frames[0]
    first.save(
        out_path,
        save_all=True,
        append_images=frames[1:],
        loop=0,          # loop forever
        duration=frame_ms,
        optimize=False,
    )
    print(f"wrote {out_path} ({len(frames)} frames, {frame_ms}ms/frame)")


if __name__ == "__main__":
    main()
