#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t (*rng_fn)(void);

typedef struct {
    rng_fn rng;
    int last;   // index of previous pick, -1 if none
} answers_picker_t;

int         answers_count(void);
const char *answers_get(int index);

void answers_picker_init(answers_picker_t *p, rng_fn rng);
int  answers_pick(answers_picker_t *p);   // returns index, never == previous

#ifdef __cplusplus
}
#endif
