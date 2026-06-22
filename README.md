# Magic 8 Ball

Firmware for a physical, voice-activated **Magic 8 Ball** built on a round AMOLED
ESP32-S3 board. Ask it a question out loud — _"Hello Computer, will it rain
tomorrow?"_ — and a glowing blue answer triangle rises from dark liquid, jitters
into place, blooms, and reveals an answer. The answer is generated live by
**Google Gemini** (which transcribes your spoken question and replies in five
words or fewer, in the oracular voice of a Magic 8 Ball). Tap or shake for a
classic random answer instead.

Everything is drawn by a **custom software renderer** — no LVGL, no GPU — a pure
RGB565 pixel pipeline running on the ESP32-S3.

## Hardware

- **Board:** [Waveshare ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/esp32-s3-touch-amoled-1.75.htm)
  - 466×466 round AMOLED, **CO5300** panel driven over QSPI
  - **QMI8658** 6-axis IMU (shake to ask)
  - **CST9217** capacitive touch (tap to ask, hold to factory-reset)
  - On-board microphone + audio codec (wake word + question capture)
  - **AXP2101** PMIC for power management
- **Microcontroller:** ESP32-S3 (dual-core Xtensa LX7, 16 MB PSRAM)

## Features

- **Voice-activated answers** — say the **"Hello Computer"** wake word
  (on-device [microWakeWord](https://github.com/nathanstitt/micro_wake_word_standalone)
  model, ~36 KB), ask a question, and Gemini answers it in ≤5 words.
- **On-device wake-word detection** — runs locally; only the recorded question is
  sent to the cloud, and only after the wake word fires.
- **Record-until-silence** — an energy VAD ends recording when you stop talking,
  so asks are as short as your question.
- **One-shot multimodal Gemini call** — the recorded audio (WAV) and the persona
  prompt go to Gemini's `generateContent` in a single HTTPS request; Gemini both
  transcribes and answers.
- **Graceful fallback** — no Wi-Fi, no API key, or any API error falls back to one
  of the 20 classic Magic 8 Ball answers, so the ball always responds.
- **Tap & shake** — a screen tap or an IMU shake gives a classic random answer.
- **Animated reveal** — a 3-D triangle rises from the liquid, wobbles, blooms its
  glow, and the answer text fades in. The rise overlaps the Gemini round-trip
  (starts mid-wait, holds with a pulsing glow) to hide the latency.
- **Listening / thinking cues** — a white flitting starfield while it listens,
  switching to faster blue stars while Gemini is thinking.
- **Smart text layout** — a top-heavy best-fit word wrap fits answers to the
  apex-down triangle (more words on the wider upper rows).
- **Wi-Fi provisioning portal** — a captive-portal setup AP collects Wi-Fi
  credentials and the Gemini API key (stored in NVS); reachable at
  `http://magic8ball.local/` on the LAN, which also accepts custom messages via POST.
- **Hold-to-reset gesture** — hold the screen during normal use to clear saved
  Wi-Fi + API key and reboot into the setup AP (re-provision without a cable).
- **Power management** — sleeps the panel when idle.

## Architecture (`main/`)

- `config.h` — every tunable (geometry, timing, colors, animation rates, mic gain).
  No magic literals elsewhere.
- `gfx/` — pure pixel primitives: framebuffer, color (RGB565 + blend), draw
  (rasterizer, triangle gradient + glow), text (Montserrat Bold). Host-testable.
- `scene/` — pure C logic: state machine (IDLE→SHAKING→TUMBLING→LOCKING→SHOWING→SLEEP),
  animation, answers, the energy VAD, and the listen FSM. Host-testable.
- `render/` — `pyramid.cpp` (the 3-D triangle + glow, the only glm user), `fx.cpp`
  (background + starfield), `render.cpp` (compositor + text wrap).
- `hal/` — board drivers: display (CO5300), IMU, touch, mic, wake word, recorder,
  power. Never compiled in host tests.
- `net/` — Wi-Fi (STA-with-AP-fallback), captive-portal provisioning, HTTP server,
  mDNS, the Gemini client, and the WAV/base64 helpers.
- `app_main.cpp` — dual-core split: core 0 = logic/events/voice, core 1 = render+flush.

The renderer does a full redraw each frame. The background is precomputed once and
memcpy'd per frame; identical scenes are skipped, so idle/showing cost nothing.

## Performance

The renderer is CPU-only (no GPU) and pushes the full 466×466 round panel over QSPI
every frame. Measured on the device during the answer animation
(build with `-DDEBUG_FPS` and `-DDEBUG_FLUSH_PROFILE` to log this):

| Phase                | Frame rate | Render | Flush  |
|----------------------|-----------:|-------:|-------:|
| Answer animation     | ~12–16 fps | ~28–36 ms | ~32–35 ms |
| Idle / showing       | ~0 (static frames skipped) | — | — |

- The **hard floor is the ~23 ms QSPI transfer** for one full frame; the flush
  measures ~32–35 ms including the byte-swap pipelined under DMA.
- Identical scenes are skipped via a `memcmp`, so IDLE and SHOWING render nothing —
  the AMOLED just holds the last flushed frame and the CPU stays idle.
- The animation only runs during SHAKING→TUMBLING→LOCKING; the expensive glow is
  deferred to the lock/bloom so the rise stays smooth.

Getting here took a dedicated optimization pass: rendering directly in the panel's
byte order (eliminating a 33 ms per-frame byte-swap), caching the circle-clip row
spans (31 ms → 5.5 ms), incremental edge functions + a ramp LUT in the triangle
gradient (135 ms → 57 ms), and flushing in cache-line-aligned strips with the
byte-swap pipelined under DMA.

## Build / flash / test

Requires **ESP-IDF v5.5**. Source the toolchain first:

```bash
export IDF_PATH=/path/to/esp-idf-5.5
. "$IDF_PATH/export.sh"
```

**Firmware** (repo root, target esp32s3):

```bash
idf.py build
idf.py -p <PORT> flash monitor
```

The first configure needs network access (it fetches the Waveshare BSP, glm, and
the wake-word component).

**Host tests** (`host_test/`, linux target — runs the pure `gfx/`, `scene/`,
`render/`, and `net/` helpers under Unity):

```bash
cd host_test
idf.py build
printf '\n*\n' | ./build/magic8ball_host_test.elf
```

## First-time setup

1. Flash the firmware. On first boot (no saved credentials) the device starts a
   setup Wi-Fi AP named **`Magic-8-Ball-Setup`**.
2. Join that network; the captive portal opens. Enter your Wi-Fi name/password and
   your **Gemini API key** (from [Google AI Studio](https://aistudio.google.com)).
3. The device reboots, connects, and shows its IP. Say **"Hello Computer"** and ask
   away.

To re-provision later, **hold the screen** during normal use until the reset
countdown completes — it clears the saved Wi-Fi + key and returns to the setup AP.

## Changing the wake word

The wake word is an on-device [microWakeWord](https://github.com/nathanstitt/micro_wake_word_standalone)
TFLite model embedded in the firmware (`main/hal/<phrase>_model.h`, wired up in
`main/hal/wakeword.cpp`). Swapping it is a two-part job:

**Train + install in one command** (local, Apple-Silicon GPU, no Google Colab):

```bash
cd wakeword-train
./make-wakeword.sh "Hey Oracle" --phonetic "hey or-uhkul" --install
```

That synthesizes samples with Piper TTS, augments + trains on MPS, exports the
quantized `.tflite`, wires it into `wakeword.cpp`, and builds the firmware. Then
`idf.py -p <PORT> flash monitor`, say the phrase, and tune `WW_PROBABILITY_CUTOFF`
on-device if needed. The `wakeword-train/` dir is gitignored; see its README for the
step-by-step scripts and the shared-dataset caveat (~33 GB, downloaded once).

To install a model you already have (community or pre-trained):

```bash
scripts/install-wake-word.sh <model.tflite> <model.json> "Phrase"
```

This generates the C header, wires the 4 tuning constants + the model symbol/label
into `wakeword.cpp`, and removes the old model.

## Notes

- The renderer never calls LVGL. The Waveshare BSP pulls LVGL in transitively as a
  build dependency, but our code uses a narrow software RGB565 pipeline.
- The wake-word component is GPLv3 (a standalone ESPHome-derived port); see
  `main/idf_component.yml`.
