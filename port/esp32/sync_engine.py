"""PlatformIO pre-build hook.

Refreshes the vendored HTGL engine sources under lib/htgl/ from the repo's
single source of truth at /engine before every build, so this port can never
drift. (We vendor rather than symlink because Windows PlatformIO symlinks are
unreliable; this hook keeps the copy honest.)
"""
import glob
import os
import shutil

Import("env")  # noqa: F821  (injected by PlatformIO)

proj = env["PROJECT_DIR"]  # noqa: F821
src = os.path.normpath(os.path.join(proj, "..", "..", "engine"))
dst = os.path.join(proj, "lib", "htgl")

if os.path.isdir(src):
    os.makedirs(dst, exist_ok=True)
    n = 0
    for f in glob.glob(os.path.join(src, "*.c")) + glob.glob(os.path.join(src, "*.h")):
        shutil.copy2(f, dst)
        n += 1
    print("htgl: synced %d engine source(s) from %s" % (n, src))
else:
    print("htgl: WARNING engine dir not found at %s — using vendored copy as-is" % src)
