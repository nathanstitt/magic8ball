#pragma once
#include "scene.h"
#include "answers.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EV_NONE,
    EV_TAP,
    EV_SHAKE,
} event_t;

typedef struct {
    scene_t scene;
    answers_picker_t picker;

    uint32_t state_ms;       // ms elapsed in the current state
    uint32_t quiet_ms;       // ms since last shake while in SHAKING
    bool     answer_chosen;  // answer index picked for this ask
    int      answer_index;

    // Custom inbound message (from the network). When has_custom is set, the next
    // ask shows custom_text instead of a random answer. custom_text is owned here
    // (lifetime = the state machine), so scene.text may point at it safely for the
    // whole animation. Filled by sm_trigger_message().
    char     custom_text[NET_MSG_MAX];
    bool     has_custom;
} sm_t;

void     sm_init(sm_t *sm, rng_fn rng);
void     sm_tick(sm_t *sm, event_t ev, uint32_t dt_ms);
state_t  sm_state(const sm_t *sm);
const scene_t *sm_scene(const sm_t *sm);

// Arm a custom-text ask: copy `text` into the state machine's own buffer
// (truncating to NET_MSG_MAX-1) and force entry to ST_SHAKING so the existing
// rise->jitter->bloom->show animation plays with this text instead of a random
// answer. The caller MUST only invoke this from a restful state (ST_IDLE /
// ST_SHOWING / ST_SLEEP) so a running animation is never aborted.
void     sm_trigger_message(sm_t *sm, const char *text);

#ifdef __cplusplus
}
#endif
