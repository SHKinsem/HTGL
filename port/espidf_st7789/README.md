# ESP-IDF: ESP32-S3 + 240x280 ST7789

This is a standalone ESP-IDF example for the exact display wiring used by the
DragonSoul box: an ESP32-S3 N8R8 driving a write-only 240x280 ST7789 over
`SPI3_HOST` at 80 MHz. It uses two 240x8 RGB565 line buffers: while SPI DMA
transmits one band, HTGL composes the next. It is still a band renderer, not a
framebuffer renderer.

## Wiring

| ST7789 signal | ESP32-S3 GPIO | Notes |
|---|---:|---|
| LCD enable / rail | 17 | Active low |
| Backlight | 45 | Active high |
| D/C | 38 | Command/data select |
| CS | 39 | SPI chip select |
| SCLK | 40 | SPI3 clock |
| MOSI | 41 | Write-only data |
| RST | 47 | Panel reset |
| MISO | - | Not connected |

The panel is configured with a vertical offset of 20 pixels and inverted
colours, as required by this 240x280 module. Do not use this table unchanged
for a different ST7789 breakout.

## Build and flash

Install ESP-IDF 5.5 or newer, then choose the S3 target and build from this
directory:

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p COM8 flash monitor
```

For a PlatformIO-backed ESP-IDF build of the same project, run `pio run` (and
`pio run -t upload --upload-port COM8`) in this directory.

`sdkconfig.defaults` enables the N8R8 board's 8 MB octal PSRAM and permits the
8 KiB UI task stack to live there. The line buffers remain in internal DMA RAM.

## Regenerate the embedded UI

Run this from the repository root after editing `ui.html`:

```sh
python tool/htgl.py port/espidf_st7789/ui.html \
  --emit-c port/espidf_st7789/main/ui_blob.c --symbol htgl_espidf_blob \
  --crc --strict
```

The sample demonstrates rounded translucent cards, band-local backdrop blur,
and a Q24.8 animated orb. The generic HTGL API and binary format are described
in the repository [README](../../README.md) and [usage reference](../../docs/USAGE.md).
