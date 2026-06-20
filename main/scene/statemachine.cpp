#include "statemachine.h"
#include "animation.h"
#include "config.h"
#include <math.h>
#include <stddef.h>   // size_t

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

void sm_trigger_message(sm_t *sm, const char *text)
{
    // Copy by hand (mirrors sm_init's manual loop; avoids depending on libc here)
    // and hard-truncate to the buffer. net.cpp already sanitized + truncated, so
    // this is a safety net.
    size_t i = 0;
    if (text) {
        for (; text[i] && i < (size_t)(NET_MSG_MAX - 1); i++) {
            sm->custom_text[i] = text[i];
        }
    }
    sm->custom_text[i] = '\0';
    sm->has_custom = true;
    sm->text_pending = false;
    enter(sm, ST_SHAKING);   // resets state_ms/quiet_ms/answer_chosen
}

void sm_start_pending_rise(sm_t *sm)
{
    // Speculative rise: no answer yet. Pick an answer_index only to seed the tumble
    // motion (it is never shown). text stays empty until sm_set_pending_text().
    sm->answer_index = answers_pick(&sm->picker);
    sm->answer_chosen = true;
    sm->has_custom = true;        // the eventual text comes from custom_text
    sm->custom_text[0] = '\0';
    sm->text_pending = true;
    sm->scene.text = sm->custom_text;
    sm->scene.text_seq++;
    enter(sm, ST_TUMBLING);       // straight to the rise; skip SHAKING/ponder
}

void sm_set_pending_text(sm_t *sm, const char *text)
{
    size_t i = 0;
    if (text) {
        for (; text[i] && i < (size_t)(NET_MSG_MAX - 1); i++) {
            sm->custom_text[i] = text[i];
        }
    }
    sm->custom_text[i] = '\0';
    sm->has_custom = true;
    bool was_pending = sm->text_pending;
    sm->text_pending = false;     // releases a held ST_LOCKING -> ST_SHOWING

    // If the die was already holding locked-and-blank, restart the LOCKING fade so
    // the text fades in cleanly (state_ms had run past LOCK_MS during the hold, which
    // would otherwise pop the text in at full alpha). If it's still rising (TUMBLING)
    // the text simply becomes ready before it locks -- no restart needed.
    if (was_pending && sm->scene.state == ST_LOCKING) {
        sm->state_ms = 0;
    }
}

void sm_tick(sm_t *sm, event_t ev, uint32_t dt_ms)
{
    sm->state_ms += dt_ms;
    scene_t *sc = &sm->scene;

    switch (sc->state) {
    case ST_IDLE:
        if (ev == EV_TAP || ev == EV_SHAKE) {
            sm->has_custom = false;
            enter(sm, ST_SHAKING);
        } else if (sm->state_ms >= IDLE_SLEEP_MS) {
            enter(sm, ST_SLEEP);
        }
        break;

    case ST_SLEEP:
        if (ev == EV_TAP || ev == EV_SHAKE) {
            sm->has_custom = false;
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
            // A custom (network) message shows its own text; otherwise reveal the
            // answer picked above. answers_pick still ran for a custom ask, which
            // is harmless and keeps the per-ask tumble seed (hashed from
            // answer_index in enter(ST_TUMBLING)) varied.
            if (sm->has_custom) {
                sc->text = sm->custom_text;
            } else {
                sc->text = answers_get(sm->answer_index);
            }
            sc->text_seq++;   // distinguish this ask from the last (see scene.h)
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
        // Ignores tap/shake — does not abort. If the answer for a speculative rise
        // hasn't arrived yet, HOLD here (die locked face-on, blank, glow pulsing)
        // instead of advancing; sm_set_pending_text() clears text_pending to release.
        if (sm->state_ms >= LOCK_MS && !sm->text_pending) {
            enter(sm, ST_SHOWING);
        }
        break;

    case ST_SHOWING:
        // A tap dismisses the answer (fade back to dark liquid); a shake asks
        // again (the natural "shake the 8 ball" gesture starts a new answer).
        // After SHOW_TIMEOUT_MS with no interaction, auto-dismiss the same way a
        // tap would — fade the answer out and return to the dark idle liquid.
        if (ev == EV_TAP) {
            enter(sm, ST_DISMISSING);
        } else if (ev == EV_SHAKE) {
            sm->has_custom = false;
            enter(sm, ST_SHAKING);
        } else if (sm->state_ms >= SHOW_TIMEOUT_MS) {
            enter(sm, ST_DISMISSING);
        }
        break;

    case ST_DISMISSING:
        // Fading out. A shake restarts immediately; otherwise fade then idle.
        if (ev == EV_SHAKE) {
            sm->has_custom = false;
            enter(sm, ST_SHAKING);
        } else if (sm->state_ms >= DISMISS_MS) {
            enter(sm, ST_IDLE);
        }
        break;
    }

    // Mirror the deferred-text flag into the scene so anim_apply can pulse the glow
    // and hold the text fade during a speculative rise.
    sc->text_pending = sm->text_pending;

    // Particles only drift while the answer is churning/rising; at IDLE,
    // SHOWING and SLEEP they hold still so the render can skip static frames.
    if (sc->state == ST_SHAKING || sc->state == ST_TUMBLING || sc->state == ST_LOCKING) {
        anim_step_particles(&sm->scene, dt_ms, (int)sc->state);
    }
    anim_apply(sc, sm->state_ms, dt_ms);
}

state_t sm_state(const sm_t *sm) { return sm->scene.state; }
const scene_t *sm_scene(const sm_t *sm) { return &sm->scene; }
