# Third-party components

HTGL itself is [MIT-licensed](LICENSE). It vendors two single-file, third-party headers, both
under public-domain / permissive terms compatible with redistribution under MIT. They are checked
into the tree (not fetched at build time) so a clone builds with no network access.

| File | Component | Author | License |
|---|---|---|---|
| `engine/font8x8_basic.h` (and the synced copy in `port/esp32/lib/htgl/`) | font8x8 — 8×8 monochrome bitmap font (ASCII U+0000–U+007F) | Daniel Hepper, based on Marcel Sondaar / IBM public-domain VGA fonts | **Public Domain** |
| `port/sim/stb_image_write.h` | stb_image_write v1.16 — PNG/etc. encoder (used only by the host simulator) | Sean Barrett (nothings.org) | **Dual: MIT _or_ Public Domain (Unlicense)** — your choice |

Notes:

- **font8x8** — Public Domain. Upstream:
  <http://dimensionalrift.homelinux.net/combuster/mos3/?p=viewsource&file=/modules/gfx/font8_8.asm>
  and Daniel Hepper's C port (`dhepper/font8x8`). The full notice is preserved in the file header.
- **stb_image_write** — released by Sean Barrett under two alternatives (you may use either): the
  MIT License (Copyright © 2017 Sean Barrett) or a Public-Domain dedication (Unlicense). The
  complete dual-license text is preserved at the end of the file. It is compiled **only** into the
  host PNG simulator (`port/sim`), never into device firmware.

No part of the third-party code carries a copyleft obligation, so HTGL and anything built from it
remain freely usable for commercial and embedded purposes. New vendored dependencies must be
public-domain or permissively licensed (MIT/BSD/Apache-2.0/zlib) and added to this file.
