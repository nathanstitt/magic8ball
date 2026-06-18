#include "unity.h"
#include "answers.h"
#include <string.h>

// Counter-based RNG so we can drive specific pick sequences deterministically.
static uint32_t s_seq;
static uint32_t rng_seq(void) { return s_seq++; }

// Stuck RNG: always returns the same value (exercises the reroll bound).
static uint32_t rng_stuck(void) { return 7; }

TEST_CASE("answers_count is the classic 20", "[answers]")
{
    TEST_ASSERT_EQUAL_INT(20, answers_count());
}

TEST_CASE("answers_get returns text in range and empty out of range", "[answers]")
{
    TEST_ASSERT_EQUAL_STRING("It is certain", answers_get(0));
    TEST_ASSERT_TRUE(strlen(answers_get(19)) > 0);
    TEST_ASSERT_EQUAL_STRING("", answers_get(-1));
    TEST_ASSERT_EQUAL_STRING("", answers_get(20));
}

TEST_CASE("picker never returns the same index twice in a row", "[answers]")
{
    s_seq = 0;
    answers_picker_t p;
    answers_picker_init(&p, rng_seq);
    int prev = -1;
    for (int i = 0; i < 200; i++) {
        int idx = answers_pick(&p);
        TEST_ASSERT_TRUE(idx >= 0 && idx < answers_count());
        TEST_ASSERT_NOT_EQUAL(prev, idx);
        prev = idx;
    }
}

TEST_CASE("a stuck RNG does not hang and still yields a valid index", "[answers]")
{
    answers_picker_t p;
    answers_picker_init(&p, rng_stuck);
    // First pick is fine; second would want to repeat but the bounded reroll
    // must return (accepting the repeat) rather than loop forever.
    int a = answers_pick(&p);
    int b = answers_pick(&p);
    TEST_ASSERT_TRUE(a >= 0 && a < answers_count());
    TEST_ASSERT_TRUE(b >= 0 && b < answers_count());
}
