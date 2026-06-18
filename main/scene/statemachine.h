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
} sm_t;

void     sm_init(sm_t *sm, rng_fn rng);
void     sm_tick(sm_t *sm, event_t ev, uint32_t dt_ms);
state_t  sm_state(const sm_t *sm);
const scene_t *sm_scene(const sm_t *sm);

#ifdef __cplusplus
}
#endif
