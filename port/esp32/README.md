# HTGL ESP32 + ST7789 port

Runs the HTGL embedded UI engine on an ESP32 driving a 240×240 ST7789 panel
via [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI).

## Wiring (default pins — edit in `platformio.ini`)

| Signal | ESP32 GPIO | Notes |
|--------|-----------|-------|
| MOSI   | 23        | SPI data out |
| SCLK   | 18        | SPI clock |
| CS     | 15        | Chip-select, active LOW |
| DC     | 2         | Data/Command select |
| RST    | 4         | Reset, active LOW |
| BL     | 27        | Backlight, active HIGH |
| GND    | GND       | Common ground |
| VCC    | 3.3 V     | Most ST7789 modules are 3.3 V |

> All pin numbers are configurable via the `-DTFT_*` `build_flags` in
> `platformio.ini` — no `User_Setup.h` editing required.

## Changing the panel resolution

Edit the `build_flags` in `platformio.ini`:

```ini
-D TFT_WIDTH=240
-D TFT_HEIGHT=240
```

And adjust `line_buf` in `src/main.cpp` if the width changes:

```cpp
static uint16_t line_buf[YOUR_WIDTH * 16];
```

## Regenerate the embedded UI after editing `ui.html`

```sh
# from port/esp32/
python ../../tool/htgl.py ui.html -o /tmp/ui.uib --emit-c src/ui_blob.c --symbol ui
```

## Build, flash, and monitor

```sh
# Compile only (no hardware needed)
pio run

# Compile + flash to connected ESP32
pio run -t upload

# Open serial monitor (115200 baud)
pio device monitor
```

## Touch / tap input

The tap API (`data-tap` in the UI, `htgl_pointer_down` / `htgl_pointer_up` in
C++) is wired and ready.  Most bare ST7789 breakout boards do **not** include a
touch controller, so `tap_handler()` in `src/main.cpp` will not fire unless you
connect a touch IC (e.g. XPT2046) and call `htgl_pointer_down/up` from its
interrupt handler.

Animation runs regardless of whether a touch controller is present.

## Engine source

The HTGL engine sources are vendored from `../../engine/` into `lib/htgl/`.
They are kept in sync manually — update them when the engine changes.
The vendored files carry the comment `/* vendored from /engine — keep in sync */`.
