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
