# CLAUDE.md

Firmware for a physical **Magic 8 Ball** on the **Waveshare ESP32-S3-Touch-AMOLED-1.75**
(466×466 round AMOLED, CO5300 panel over QSPI). Shake (IMU) or tap (touch) → a glowing
blue answer triangle rises from dark liquid, jitters into place, blooms, and shows one of
the 20 classic answers. **Custom software renderer — no LVGL.** ESP-IDF v5.5, C + C++.

## Build / flash / test

Source the toolchain first (the venv override is required — `export.sh` otherwise picks an
incomplete env):

```bash
export IDF_PATH=/Users/nas/code/vendor/esp-idf-5.5
export IDF_PYTHON_ENV_PATH=/Users/nas/.espressif/python_env/idf5.5_py3.9_env
. "$IDF_PATH/export.sh"
```

- **Firmware** (repo root, target esp32s3): `idf.py build`, `idf.py -p <PORT> flash`.
  First configure needs network (fetches the Waveshare BSP, glm, and — transitively — LVGL).
- **Host tests** (`host_test/`, linux target): `idf.py build`, then run
  `printf '\n*\n' | ./build/magic8ball_host_test.elf` (Unity; `*` runs all).
- **FPS/flush profiling:** build with `-DDEBUG_FPS` and/or `-DDEBUG_FLUSH_PROFILE`
  (`idf.py build -DCMAKE_CXX_FLAGS="-DDEBUG_FPS"`); off by default.

## Architecture (`main/`)

- `config.h` — every tunable (geometry, timing, colors, animation rates). No magic literals elsewhere.
- `gfx/` — pure pixel primitives: `framebuffer`, `color` (RGB565 + `div255` blend), `draw`
  (rasterizer, `draw_triangle_gradient`, `draw_triangle_glow`), `text` (Montserrat Bold). Host-testable.
- `scene/` — pure C logic: `statemachine` (IDLE→SHAKING→TUMBLING→LOCKING→SHOWING→SLEEP),
  `animation` (drives the scene; plain-C `mat3` helpers — **no glm**), `answers`. Host-testable.
- `render/` — `pyramid.cpp` (the **only** glm user; draws the symmetric triangle + glow),
  `fx.cpp` (background), `render.cpp` (compositor + text wrap).
- `hal/` — board-only: `display` (CO5300 via esp_lcd), `imu` (QMI8658), `touch` (CST9217),
  `power`. **Never compiled in host tests.**
- `app_main.cpp` — dual-core split: core 0 = logic/events, core 1 = render+flush.

## Invariants (don't break these)

- **No LVGL in our code** — never `#include "lvgl.h"`, `bsp/esp-bsp.h`, or the umbrella BSP
  header (they pull LVGL). Use the narrow `bsp/display.h` / `bsp/touch.h`; forward-declare BSP
  symbols (`bsp_i2c_get_handle`, etc.) when only the umbrella header declares them. The BSP
  drags LVGL in transitively as a build dep — that's fine; we just never call `lv_*`.
- **glm only in `render/pyramid.cpp`** — keeps `scene/`+`gfx/` host-testable.
- One statement per line (`-Werror=misleading-indentation`). Clean `-Werror` build.
- Commits: `git -c commit.gpgsign=false commit`.

## Hardware gotchas (learned the hard way — see git log)

- **Byte order:** the CO5300 wants big-endian RGB565; our framebuffer is little-endian.
  `display.cpp` byte-swaps each strip before flushing.
- **Flush in strips + cache sync:** a full-frame 434 KB QSPI transfer exhausts the SPI DMA
  pool (`ESP_ERR_NO_MEM`), so flush in 8-row strips, each `esp_cache_msync`'d (PSRAM cache
  coherency — else diagonal corruption). `STRIP_ROWS` must stay an exact cache-line multiple
  (there's a `static_assert`); the flush pipelines the swap under the DMA.
- **No screen rotation:** every MADCTL rotation combo leaves a ~6px unaddressed-RAM strip on
  this panel; abandoned (see `display_init` note).

## Performance model

Full-redraw each frame. Background is precomputed once (`fx_init`) → per-frame memcpy.
Identical scenes are skipped (`memcmp`), so IDLE/SHOWING cost nothing. The expensive glow is
deferred to the lock/bloom. The hard floor is the ~23 ms QSPI transfer per frame.
