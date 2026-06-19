#pragma once
#include <stdint.h>

// =============================================================================
// Magic 8 Ball — all tunable constants. Nothing is a magic literal elsewhere.
// =============================================================================

// --- Display geometry ---
#define DISP_W          466
#define DISP_H          466
#define DISP_CX         233
#define DISP_CY         233
#define DISP_RADIUS     233

// --- Frame rate ---
#define TARGET_FPS      30
#define FRAME_MS        (1000 / TARGET_FPS)

// --- Timing (ms) ---
// Per-tick animation-time cap. A blocking call (notably voice audio capture) can
// stall the logic loop; without this cap the next tick's dt would fast-forward
// the whole answer animation in one frame. 50ms = one frame at 20fps.
#define DT_MAX_MS           50
#define THINK_MIN_MS        1000
#define SHAKE_DEBOUNCE_MS   250
#define IDLE_SLEEP_MS       30000
#define TUMBLE_MS           1400    // ST_TUMBLING duration (rise + spin)
#define LOCK_MS             350     // ST_LOCKING duration (snap face-on, glow bloom, text fade)
#define DISMISS_MS          300     // ST_DISMISSING duration (fade the answer out)
#define SHOW_TIMEOUT_MS     90000   // auto-dismiss: after this long showing an answer, fade it out as if tapped

// --- Voice (wake word + VAD listening) -------------------------------------
// The micro_wake_word component is a one-shot wake detector: it fires once on the
// phrase and gives no ongoing post-wake speech signal. So the live flow is "wake
// -> ponder for PONDER_MS -> answer" (PONDER_MS path). The speech-based end
// (MIN_SPEECH_MS + SILENCE_MS) and the LISTEN_MAX_MS backstop remain for a future
// VAD source that supplies speech_active; they are dormant while it stays false.
#define MIC_SAMPLE_RATE_HZ  16000  // capture rate the wake-word model expects (16 kHz mono int16)
// Mic input gain (dB). The codec default captured speech at RMS ~15/32767 -- far too
// quiet for Gemini. Lift it substantially; tune from the recorder's rms log.
#define MIC_GAIN_DB         42.0f
#define PONDER_MS       3000   // wake ponder / in-flight voice ask window (tune on hardware)
// Hard cap on a voice ask (record + Gemini round-trip). If the voice task hasn't
// produced text by now, the logic loop reveals a random fallback. Chosen so a real
// answer about to land always beats this timer. Tune once real latency is known.
// Must exceed the true worst-case ask: REC_MAX_MS (8s record) + the Gemini HTTP
// timeout (15s) + margin. If this is shorter than a real ask, the ponder times out
// mid-ask and reveals a canned answer, then the real answer lands as a 2nd reveal.
#define VOICE_ASK_MAX_MS 25000

// --- Recorder + energy VAD (post-wake question capture) ---------------------
#define REC_FRAME_SAMPLES   480     // 30ms @ 16kHz: one VAD/energy frame
#define REC_MAX_MS          8000    // hard cap on recording length (must terminate)
#define REC_MAX_SAMPLES     (MIC_SAMPLE_RATE_HZ / 1000 * REC_MAX_MS)  // 128000 = ~256KB PSRAM
#define VAD_START_MS        300     // need this much speech before silence can end the ask
#define VAD_SILENCE_MS      800     // trailing quiet (ms) that ends the ask
#define VAD_RMS_THRESHOLD   600     // per-frame RMS above this = speech (tune on hardware)
#define REC_MIN_SAMPLES     (MIC_SAMPLE_RATE_HZ / 2)  // <0.5s captured => treat as garbage, fallback
#define LISTEN_MAX_MS   8000   // hard backstop if speech_active never goes quiet
#define SILENCE_MS       700   // quiet duration (ms) that counts as "done asking"
#define MIN_SPEECH_MS    600   // silence can't end the ask until this much speech seen

// --- Listening starfield (particles flit as blue stars during a voice listen) ---
// While listening, the existing particles are re-tasked as a flitting starfield:
// each periodically re-rolls its velocity (random flitting) and draws brighter and
// blue. Cheap: ~PARTICLE_COUNT point writes per frame, no per-pixel work.
#define STAR_FLIT_MS      450     // how often each star re-rolls its drift direction
#define STAR_SPEED        70.0f   // px/s flitting speed while listening
#define STAR_SIZE         2       // small star square side in px (2 = 2x2)
#define STAR_SIZE_BIG     4       // large star square side in px (half the stars, 4x4)
#define STAR_FADE_MS      1200    // ms for the starfield to fade out once the answer rises
#define STAR_ALPHA_MIN    120     // listening stars are brighter than the faint idle motes
#define STAR_ALPHA_MAX    255
// Star blue (the themed glow blue, brighter than the faint idle particles).
#define COL_STAR_R        120
#define COL_STAR_G        170
#define COL_STAR_B        255
// Submitting starfield: once the user stops talking and the Gemini request is in
// flight, the stars turn white and speed up to read as "thinking/uploading".
#define STAR_SPEED_SUBMIT 140.0f   // px/s while submitting (faster than listening)
#define COL_STAR_SUBMIT_R 255
#define COL_STAR_SUBMIT_G 255
#define COL_STAR_SUBMIT_B 255

// --- IMU ---
// Acceleration magnitude in milli-g (1000 = 1g). A vigorous shake exceeds ~1500.
#define SHAKE_THRESHOLD_MG  1500

// --- Particles ---
#define PARTICLE_COUNT      24

// --- Pyramid geometry ---
// Circumradius (px) of the tetrahedron at pyr_scale=1. Sized so the locked
// front face fills most of the 233px-radius circle with a small dark liquid
// margin (the classic Magic 8 Ball die framed in fluid).
#define PYRAMID_RADIUS      196.0f
#define PYRAMID_FOCAL       600.0f   // perspective focal length (pixels)
#define PYRAMID_LIGHT_X     0.5f
#define PYRAMID_LIGHT_Y     0.8f
#define PYRAMID_LIGHT_Z     0.3f     // light direction (normalized in pyramid.cpp)
#define PYRAMID_AMBIENT     0.15f    // minimum face brightness
#define PYRAMID_DIFFUSE     0.85f

// Submerged start depth: pyramid center begins just below the visible circle and
// rises to DISP_CY during ST_TUMBLING.
#define PYRAMID_START_Y     ((float)(DISP_CY + DISP_RADIUS + 40))

// --- Wobble angular rates (radians/sec, per answer, seeded from answer index) ---
// The die stays mostly face-forward and wobbles gently to reveal its sides a
// little as it rises, rather than tumbling end-over-end. Small rates only.
#define TUMBLE_RX_MIN   0.30f
#define TUMBLE_RX_MAX   0.70f
#define TUMBLE_RY_MIN   0.40f
#define TUMBLE_RY_MAX   0.90f
#define TUMBLE_RZ_MIN  -0.20f
#define TUMBLE_RZ_MAX   0.20f
// Peak wobble tilt in radians (~20°): how far the die leans to show its sides
// during the rise before settling face-on.
#define WOBBLE_AMP      0.35f

// Positional shake as the triangle drifts in (bobbing through liquid). Peak
// jitter in px, decaying to 0 as it settles dead-center. Two sine components at
// these Hz give a jittery (non-uniform) shimmy rather than a clean oscillation.
#define JITTER_AMP      19.0f
#define JITTER_FREQ_X   6.5f
#define JITTER_FREQ_Y   8.3f

// The die starts small (distant, rising from the depths) and grows to full size
// as it reaches center: pyr_scale eases from TUMBLE_SCALE_START to 1.0.
#define TUMBLE_SCALE_START   0.15f

// The glow begins ramping up during the last part of the rise (before the
// formal lock) so it's already blooming as the die settles. Progress [0..1]
// through TUMBLING at which the pre-glow starts, and the glow level it reaches
// by the end of the rise (LOCKING then carries it to 1.0).
#define TUMBLE_GLOW_START_P  0.6f
#define TUMBLE_GLOW_MAX      0.65f

// --- Triangle face gradient (the lit blue die look) ---
// bary_min runs 0 at an edge to ~0.333 at the centroid.
#define GRAD_CENTER_BARY   0.34f   // bary_min at/after which the fill is full center color
#define GRAD_BEVEL_BARY    0.09f   // width (in bary_min) of the lighter edge bevel band
#define GRAD_BEVEL_STRENGTH 0.0f   // bevel off: the bright edge replaces the sheen

// --- Outer glow bloom (the die's light bleeding into the liquid) ---
// One smooth per-pixel falloff outside the triangle (draw_triangle_glow): no
// shells, no banding. The glow color matches the bright triangle edge and its
// peak alpha is high, so the rim blends seamlessly into the bloom (no dark
// seam). Strong, diffuse halo like the reference.
#define GLOW_DIST        130.0f    // how far (px) the bloom reaches past the edge
#define GLOW_PEAK_ALPHA  220       // bloom alpha right at the edge (0..255)
#define COL_GLOW_R       45
#define COL_GLOW_G      105
#define COL_GLOW_B      255

// --- Text layout ---
#define RENDER_MAX_LINES     4
#define RENDER_MAX_LINE_LEN  32
#define TRI_TEXT_MARGIN      16    // px inset from triangle edge to text bounding column
#define RENDER_LINE_GAP      2     // px between wrapped lines
// Answer-text vertical placement (apex-down triangle: top edge ~144, apex ~429).
// The block is centered at TEXT_IDEAL_CENTER_Y (≈ the triangle centroid / screen
// center, so a typical 1-3 line answer looks balanced in the die), but never lets
// its BOTTOM line fall below TEXT_MAX_BOTTOM_Y — past there the triangle has
// narrowed too much to fit text. Tall (4-line) answers therefore shift upward just
// enough to clear the apex, instead of every answer sitting fixed-high.
#define TEXT_IDEAL_CENTER_Y  233   // screen center; ~the apex-down triangle centroid (~239)
#define TEXT_MAX_BOTTOM_Y    300   // block bottom may not pass this (triangle still wide here)

// --- Network / custom message ---
// Max bytes (incl. NUL) of a custom message POSTed over the network and shown in
// place of a random answer. ~70 ASCII chars realistically fit the triangle (4
// lines, Montserrat Bold); 96 leaves headroom. Sized into sm_t.custom_text and
// the net_msg_t hand-off payload, so the two must agree (see net/net.h).
#define NET_MSG_MAX     96

// --- Gemini (voice answer) ---
// Audio + persona prompt go in one generateContent POST. The model name is a
// compiled default; the API key is provisioned at runtime (NVS, see provcfg).
#define GEMINI_HOST     "generativelanguage.googleapis.com"
#define GEMINI_MODEL    "gemini-2.5-flash"
#define GEMINI_API_KEY_MAX  64
#define GEMINI_PROMPT \
    "You are a Magic 8 Ball. The audio contains a yes/no or open question. " \
    "Answer in 5 words or fewer: cryptic, confident, oracular. " \
    "Output only the answer, no punctuation beyond a final period."

// --- Factory-reset gesture (hold screen during normal use) ---
// Hold the screen continuously. Past RESET_ARM_MS a "keep holding" countdown overlay
// appears (long enough that a normal tap never arms it); held to RESET_HOLD_MS total,
// the device clears saved Wi-Fi + API key and reboots into the setup AP. Release any
// time before RESET_HOLD_MS to cancel.
#define RESET_ARM_MS    1000
#define RESET_HOLD_MS   4000

// --- Colors (R, G, B — 0..255) ---
// Background liquid: near-black, faint blue lift toward the center.
#define COL_BG_EDGE_R    2
#define COL_BG_EDGE_G    3
#define COL_BG_EDGE_B   10
#define COL_BG_CTR_R     4
#define COL_BG_CTR_G     8
#define COL_BG_CTR_B    28
// Triangle die: the edge stays BRIGHT (near the glow color) so the rim melts
// into the outer bloom with no dark seam; the center is brightest (near-white
// blue). A gentle bright->brighter gradient, never diving dark at the edge.
#define COL_TRI_EDGE_R   45
#define COL_TRI_EDGE_G  105
#define COL_TRI_EDGE_B  255
#define COL_TRI_CTR_R    80
#define COL_TRI_CTR_G   130
#define COL_TRI_CTR_B   255
#define COL_TRI_BEVEL_R 140
#define COL_TRI_BEVEL_G 180
#define COL_TRI_BEVEL_B 255
#define COL_TRI_ALPHA   245
// Glow halo color in the liquid around the die.
#define COL_HALO_R       20
#define COL_HALO_G       45
#define COL_HALO_B      130
// Answer text: light grey-blue.
#define COL_TEXT_R     205
#define COL_TEXT_G     215
#define COL_TEXT_B     235
// Status overlay (Wi-Fi IP / setup hint) — dim grey-blue HUD line near the
// bottom, drawn with the small 8x8 font so it reads as utility, not the answer.
#define COL_STATUS_R   120
#define COL_STATUS_G   140
#define COL_STATUS_B   170
#define STATUS_ALPHA   160     // 0..255 blend; kept low so it doesn't fight the glow
#define STATUS_MARGIN_Y 18     // px from the bottom of the status text to DISP_H
