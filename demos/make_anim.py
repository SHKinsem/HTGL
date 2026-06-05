"""Animation probe: HTGL has NO animation support (no @keyframes / transition /
timeline / runtime tick). The HTML itself cannot express motion. To get an
animation we must drive it EXTERNALLY: a Python loop computes each frame's
geometry, re-transpiles, and re-renders. This script proves the engine can
render frames fast, while showing that 'animation' is entirely our own loop."""
import subprocess, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tool"))
from htgl.html_tree import parse_html
from htgl.uib import build_uib
from PIL import Image

W, H, N = 240, 200, 16
SIM = os.path.join("build", "htgl_sim.exe")
os.makedirs("build/frames", exist_ok=True)

PALETTE = ["#ff5050", "#ffb020", "#2dd47f", "#2d6cdf", "#a050ff"]

def tri(t):            # 0..1..0 triangle wave
    return 2 * t if t < 0.5 else 2 * (1 - t)

frames = []
for i in range(N):
    t = i / (N - 1)
    ball_x = int(10 + (W - 50) * tri(t))     # bounce L<->R
    ball_y = int(60 + 40 * tri((t * 2) % 1)) # little vertical bob
    bar_w = int((W - 24) * t)                 # progress 0->100%
    color = PALETTE[i % len(PALETTE)]
    html = f'''
<div style="position:absolute; left:0; top:0; width:{W}px; height:{H}px; background-color:#11151f">
  <div style="position:absolute; left:10px; top:10px; color:#7f8aa0; font-size:8px">frame {i+1}/{N}  (driven by python, not by HTML)</div>
  <div style="position:absolute; left:{ball_x}px; top:{ball_y}px; width:30px; height:30px; background-color:{color}"></div>
  <div style="position:absolute; left:12px; top:170px; width:{W-24}px; height:14px; background-color:#1e2636">
    <div style="position:absolute; left:0px; top:0px; width:{bar_w}px; height:14px; background-color:#2dd47f"></div>
  </div>
</div>'''
    nodes = parse_html(html, W, H)
    blob = build_uib(nodes, W, H)
    uib = f"build/frames/f{i:02d}.uib"
    png = f"build/frames/f{i:02d}.png"
    open(uib, "wb").write(blob)
    subprocess.run([SIM, uib, png], check=True, stdout=subprocess.DEVNULL)
    frames.append(Image.open(png).convert("RGB"))

# 1) animated GIF for the user to actually play
frames[0].save("demos/anim.gif", save_all=True, append_images=frames[1:],
               duration=90, loop=0)

# 2) montage (4x4) so the motion is visible inline as a strip
cols, rows, pad = 4, 4, 4
cw, ch = W, H
sheet = Image.new("RGB", (cols*cw + (cols+1)*pad, rows*ch + (rows+1)*pad), "#000000")
for idx, fr in enumerate(frames):
    r, c = divmod(idx, cols)
    sheet.paste(fr, (pad + c*(cw+pad), pad + r*(ch+pad)))
sheet.save("demos/anim_montage.png")
print(f"rendered {N} frames -> demos/anim.gif + demos/anim_montage.png")
print(f"each frame: build/htgl_sim re-rendered a fresh .uib in a python loop")
