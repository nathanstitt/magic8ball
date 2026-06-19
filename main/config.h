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
#define PONDER_MS       3500   // wake-only ponder: fire this long after wake if no speech seen
#define LISTEN_MAX_MS   8000   // hard backstop if speech_active never goes quiet
#define SILENCE_MS       700   // quiet duration (ms) that counts as "done asking"
#define MIN_SPEECH_MS    600   // silence can't end the ask until this much speech seen

// --- Listening swirl (animated blue cloud background during a voice listen) ---
#define SWIRL_LAYERS      3       // domain-warped sine layers summed per pixel
#define SWIRL_SCALE       0.018f  // spatial frequency (1/px); smaller = larger blobs
#define SWIRL_SPEED       0.6f    // radians/sec the pattern drifts (slow = dreamy)
#define SWIRL_WARP        2.2f    // domain-warp strength (swirliness)
#define SWIRL_CONTRAST    0.55f   // 0..1 softness of the blue field (low = soft)
// Swirl blue (blended over the dark base). Reuses the themed glow blue.
#define COL_SWIRL_R       30
#define COL_SWIRL_G       90
#define COL_SWIRL_B       235

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
// Vertical center of the answer text block. Sits HIGH in the apex-down triangle
// (top edge ~144, apex ~429) so a tall multi-line answer stays in the wide upper
// band and its lower lines don't run into the narrowing apex (where they'd be
// clipped). The apex stays empty, like the reference.
#define TEXT_CENTER_Y        196

// --- Network / custom message ---
// Max bytes (incl. NUL) of a custom message POSTed over the network and shown in
// place of a random answer. ~70 ASCII chars realistically fit the triangle (4
// lines, Montserrat Bold); 96 leaves headroom. Sized into sm_t.custom_text and
// the net_msg_t hand-off payload, so the two must agree (see net/net.h).
#define NET_MSG_MAX     96

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
