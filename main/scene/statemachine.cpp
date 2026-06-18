#include "statemachine.h"
#include "animation.h"
#include "config.h"
#include <math.h>

// Distance beyond the visible circle where the triangle's entry point sits, so
// it slides in fully from off-screen.
#define ENTRY_MARGIN   60.0f

static void enter(sm_t *sm, state_t s)
{
    sm->scene.state = s;
    sm->state_ms = 0;
    if (s == ST_SHAKING) {
        sm->quiet_ms = 0;
        sm->answer_chosen = false;
    } else if (s == ST_TUMBLING) {
        // Seed the three tumble rates deterministically from the answer index
        // (pure function of answer_index — no rand). Each byte of the hash
        // drives one axis, scaled into its MIN..MAX range.
        uint32_t h = (uint32_t)(sm->answer_index * 2654435761u);
        sm->scene.pyr_rx_rate = TUMBLE_RX_MIN +
            ((h >> 8) & 0xFF) / 255.0f * (TUMBLE_RX_MAX - TUMBLE_RX_MIN);
        sm->scene.pyr_ry_rate = TUMBLE_RY_MIN +
            ((h >> 16) & 0xFF) / 255.0f * (TUMBLE_RY_MAX - TUMBLE_RY_MIN);
        sm->scene.pyr_rz_rate = TUMBLE_RZ_MIN +
            ((h >> 24) & 0xFF) / 255.0f * (TUMBLE_RZ_MAX - TUMBLE_RZ_MIN);
        mat3_identity(sm->scene.pyr_rot);

        // Pick a RANDOM entry direction for this ask (hardware RNG), so the
        // triangle slides in from a different edge each time. The start point is
        // just beyond the visible circle along that angle.
        uint32_t r = sm->picker.rng ? sm->picker.rng() : 0;
        float angle = (float)(r % 36000u) * (3.14159265f / 18000.0f);  // 0..2pi
        float dist = (float)DISP_RADIUS + ENTRY_MARGIN;
        sm->scene.pyr_start_x = (float)DISP_CX + dist * cosf(angle);
        sm->scene.pyr_start_y = (float)DISP_CY + dist * sinf(angle);
        // Vary the positional-shake phase per ask too.
        sm->scene.pyr_jit_phase = (float)((r >> 16) % 36000u) * (3.14159265f / 18000.0f);
    }
}

void sm_init(sm_t *sm, rng_fn rng)
{
    for (unsigned i = 0; i < sizeof(*sm); i++) {
        ((char *)sm)[i] = 0;
    }
    answers_picker_init(&sm->picker, rng);
    sm->scene.state = ST_IDLE;
    sm->scene.text = 0;
    sm->scene.pyr_alpha = 0;
    sm->scene.text_alpha = 0;
    mat3_identity(sm->scene.pyr_rot);
    anim_seed_particles(&sm->scene);
}

void sm_tick(sm_t *sm, event_t ev, uint32_t dt_ms)
{
    sm->state_ms += dt_ms;
    scene_t *sc = &sm->scene;

    switch (sc->state) {
    case ST_IDLE:
        if (ev == EV_TAP || ev == EV_SHAKE) {
            enter(sm, ST_SHAKING);
        } else if (sm->state_ms >= IDLE_SLEEP_MS) {
            enter(sm, ST_SLEEP);
        }
        break;

    case ST_SLEEP:
        if (ev == EV_TAP || ev == EV_SHAKE) {
            enter(sm, ST_SHAKING);
        }
        break;

    case ST_SHAKING:
        // Choose the (hidden) answer once, at the start of the ask.
        if (!sm->answer_chosen) {
            sm->answer_index = answers_pick(&sm->picker);
            sm->answer_chosen = true;
        }
        // Only a real shake resets the quiet timer; a lone tap must not.
        if (ev == EV_SHAKE) {
            sm->quiet_ms = 0;
        } else {
            sm->quiet_ms += dt_ms;
        }
        // Tumble when shaking has stopped (debounced) AND think minimum elapsed.
        if (sm->state_ms >= THINK_MIN_MS && sm->quiet_ms >= SHAKE_DEBOUNCE_MS) {
            sc->text = answers_get(sm->answer_index);
            enter(sm, ST_TUMBLING);
        }
        break;

    case ST_TUMBLING:
        // Ignores tap/shake — does not abort.
        if (sm->state_ms >= TUMBLE_MS) {
            enter(sm, ST_LOCKING);
        }
        break;

    case ST_LOCKING:
        // Ignores tap/shake — does not abort.
        if (sm->state_ms >= LOCK_MS) {
            enter(sm, ST_SHOWING);
        }
        break;

    case ST_SHOWING:
        if (ev == EV_TAP || ev == EV_SHAKE) {
            enter(sm, ST_SHAKING);
        }
        break;
    }

    // Particles only drift while the answer is churning/rising; at IDLE,
    // SHOWING and SLEEP they hold still so the render can skip static frames.
    if (sc->state == ST_SHAKING || sc->state == ST_TUMBLING || sc->state == ST_LOCKING) {
        anim_step_particles(&sm->scene, dt_ms, (int)sc->state);
    }
    anim_apply(sc, sm->state_ms, dt_ms);
}

state_t sm_state(const sm_t *sm) { return sm->scene.state; }
const scene_t *sm_scene(const sm_t *sm) { return &sm->scene; }
