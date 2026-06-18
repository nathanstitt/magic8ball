#include "answers.h"

static const char *const ANSWERS[] = {
    // Affirmative (10)
    "It is certain", "It is decidedly so", "Without a doubt",
    "Yes definitely", "You may rely on it", "As I see it, yes",
    "Most likely", "Outlook good", "Yes", "Signs point to yes",
    // Non-committal (5)
    "Reply hazy try again", "Ask again later", "Better not tell you now",
    "Cannot predict now", "Concentrate and ask again",
    // Negative (5)
    "Don't count on it", "My reply is no", "My sources say no",
    "Outlook not so good", "Very doubtful",
};

#define N_ANSWERS ((int)(sizeof(ANSWERS) / sizeof(ANSWERS[0])))

int answers_count(void) { return N_ANSWERS; }

const char *answers_get(int index)
{
    if (index < 0 || index >= N_ANSWERS) {
        return "";
    }
    return ANSWERS[index];
}

void answers_picker_init(answers_picker_t *p, rng_fn rng)
{
    p->rng = rng;
    p->last = -1;
}

int answers_pick(answers_picker_t *p)
{
    int idx = (int)(p->rng() % (uint32_t)N_ANSWERS);
    // Reroll to avoid immediate repeats, but bound the loop: a degenerate
    // (stuck/constant) injected RNG must never hang the caller. After the
    // bound we accept the value rather than loop forever.
    for (int tries = 0; idx == p->last && tries < 64; tries++) {
        idx = (int)(p->rng() % (uint32_t)N_ANSWERS);
    }
    p->last = idx;
    return idx;
}
