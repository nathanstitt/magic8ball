#include "unity.h"
#include "statemachine.h"
#include "scene.h"
#include "config.h"

// Deterministic RNG always returning a fixed value -> answer index 3.
static uint32_t rng_fixed(void) { return 3; }

static sm_t make_sm(void)
{
    sm_t sm;
    sm_init(&sm, rng_fixed);
    return sm;
}

// Drive the machine all the way to SHOWING and return the final sm.
static void run_to_showing(sm_t *sm)
{
    sm_tick(sm, EV_TAP, 16);                 // IDLE -> SHAKING
    sm_tick(sm, EV_NONE, THINK_MIN_MS + 100);// quiet past think-min -> TUMBLING
    sm_tick(sm, EV_NONE, TUMBLE_MS + 50);    // -> LOCKING
    sm_tick(sm, EV_NONE, LOCK_MS + 50);      // -> SHOWING
}

TEST_CASE("starts idle", "[sm]")
{
    sm_t sm = make_sm();
    TEST_ASSERT_EQUAL_INT(ST_IDLE, sm_state(&sm));
}

TEST_CASE("tap and shake from idle enter shaking", "[sm]")
{
    sm_t a = make_sm();
    sm_tick(&a, EV_TAP, 16);
    TEST_ASSERT_EQUAL_INT(ST_SHAKING, sm_state(&a));
    sm_t b = make_sm();
    sm_tick(&b, EV_SHAKE, 16);
    TEST_ASSERT_EQUAL_INT(ST_SHAKING, sm_state(&b));
}

TEST_CASE("reveal (tumble) does not start before think minimum", "[sm]")
{
    sm_t sm = make_sm();
    sm_tick(&sm, EV_TAP, 16);
    sm_tick(&sm, EV_NONE, 500);   // < THINK_MIN_MS
    TEST_ASSERT_EQUAL_INT(ST_SHAKING, sm_state(&sm));
}

TEST_CASE("tumble starts after think minimum once shaking stops", "[sm]")
{
    sm_t sm = make_sm();
    sm_tick(&sm, EV_TAP, 16);
    sm_tick(&sm, EV_NONE, THINK_MIN_MS + 100);
    TEST_ASSERT_EQUAL_INT(ST_TUMBLING, sm_state(&sm));
}

TEST_CASE("ongoing shake holds in shaking beyond think minimum", "[sm]")
{
    sm_t sm = make_sm();
    sm_tick(&sm, EV_SHAKE, 16);
    sm_tick(&sm, EV_SHAKE, 2000);
    TEST_ASSERT_EQUAL_INT(ST_SHAKING, sm_state(&sm));
}

TEST_CASE("a lone tap does not count as shaking for the debounce", "[sm]")
{
    sm_t sm = make_sm();
    sm_tick(&sm, EV_SHAKE, 16);
    // state_ms past think-min; EV_TAP is not EV_SHAKE, so quiet accumulates.
    sm_tick(&sm, EV_TAP, THINK_MIN_MS + 100);
    TEST_ASSERT_EQUAL_INT(ST_TUMBLING, sm_state(&sm));
}

TEST_CASE("tumble advances to locking then showing on its timers", "[sm]")
{
    sm_t sm = make_sm();
    sm_tick(&sm, EV_TAP, 16);
    sm_tick(&sm, EV_NONE, THINK_MIN_MS + 100);
    TEST_ASSERT_EQUAL_INT(ST_TUMBLING, sm_state(&sm));
    sm_tick(&sm, EV_NONE, TUMBLE_MS + 50);
    TEST_ASSERT_EQUAL_INT(ST_LOCKING, sm_state(&sm));
    sm_tick(&sm, EV_NONE, LOCK_MS + 50);
    TEST_ASSERT_EQUAL_INT(ST_SHOWING, sm_state(&sm));
}

TEST_CASE("answer text is set by the time we are showing", "[sm]")
{
    sm_t sm = make_sm();
    run_to_showing(&sm);
    const scene_t *sc = sm_scene(&sm);
    TEST_ASSERT_NOT_NULL(sc->text);
    TEST_ASSERT_EQUAL_STRING("Yes definitely", sc->text);   // index 3
}

TEST_CASE("entering tumbling seeds deterministic non-zero spin rates", "[sm]")
{
    sm_t sm = make_sm();
    sm_tick(&sm, EV_TAP, 16);
    sm_tick(&sm, EV_NONE, THINK_MIN_MS + 100);   // -> TUMBLING, rates seeded
    const scene_t *sc = sm_scene(&sm);
    // Rates must fall inside their configured ranges.
    TEST_ASSERT_TRUE(sc->pyr_rx_rate >= TUMBLE_RX_MIN && sc->pyr_rx_rate <= TUMBLE_RX_MAX);
    TEST_ASSERT_TRUE(sc->pyr_ry_rate >= TUMBLE_RY_MIN && sc->pyr_ry_rate <= TUMBLE_RY_MAX);
    TEST_ASSERT_TRUE(sc->pyr_rz_rate >= TUMBLE_RZ_MIN && sc->pyr_rz_rate <= TUMBLE_RZ_MAX);

    // Same answer index -> identical seed (determinism).
    sm_t sm2 = make_sm();
    sm_tick(&sm2, EV_TAP, 16);
    sm_tick(&sm2, EV_NONE, THINK_MIN_MS + 100);
    const scene_t *sc2 = sm_scene(&sm2);
    TEST_ASSERT_EQUAL_FLOAT(sc->pyr_rx_rate, sc2->pyr_rx_rate);
    TEST_ASSERT_EQUAL_FLOAT(sc->pyr_ry_rate, sc2->pyr_ry_rate);
    TEST_ASSERT_EQUAL_FLOAT(sc->pyr_rz_rate, sc2->pyr_rz_rate);
}

TEST_CASE("idle long enough goes to sleep; shake wakes it", "[sm]")
{
    sm_t sm = make_sm();
    sm_tick(&sm, EV_NONE, IDLE_SLEEP_MS + 100);
    TEST_ASSERT_EQUAL_INT(ST_SLEEP, sm_state(&sm));
    sm_tick(&sm, EV_SHAKE, 16);
    TEST_ASSERT_EQUAL_INT(ST_SHAKING, sm_state(&sm));
}

TEST_CASE("tap while showing dismisses the answer back to idle", "[sm]")
{
    sm_t sm = make_sm();
    run_to_showing(&sm);
    sm_tick(&sm, EV_TAP, 16);
    TEST_ASSERT_EQUAL_INT(ST_DISMISSING, sm_state(&sm));
    sm_tick(&sm, EV_NONE, DISMISS_MS + 50);   // fade completes
    TEST_ASSERT_EQUAL_INT(ST_IDLE, sm_state(&sm));
}

TEST_CASE("shake while showing starts a new ask", "[sm]")
{
    sm_t sm = make_sm();
    run_to_showing(&sm);
    sm_tick(&sm, EV_SHAKE, 16);
    TEST_ASSERT_EQUAL_INT(ST_SHAKING, sm_state(&sm));
}

TEST_CASE("showing auto-dismisses after the show timeout", "[sm]")
{
    sm_t sm = make_sm();
    run_to_showing(&sm);
    // Just before the timeout it is still showing...
    sm_tick(&sm, EV_NONE, SHOW_TIMEOUT_MS - 100);
    TEST_ASSERT_EQUAL_INT(ST_SHOWING, sm_state(&sm));
    // ...crossing it begins the same fade a tap would, ending back at idle.
    sm_tick(&sm, EV_NONE, 200);
    TEST_ASSERT_EQUAL_INT(ST_DISMISSING, sm_state(&sm));
    sm_tick(&sm, EV_NONE, DISMISS_MS + 50);
    TEST_ASSERT_EQUAL_INT(ST_IDLE, sm_state(&sm));
}

TEST_CASE("shake during dismiss restarts immediately", "[sm]")
{
    sm_t sm = make_sm();
    run_to_showing(&sm);
    sm_tick(&sm, EV_TAP, 16);                  // -> DISMISSING
    sm_tick(&sm, EV_SHAKE, 16);                // shake interrupts the fade
    TEST_ASSERT_EQUAL_INT(ST_SHAKING, sm_state(&sm));
}

TEST_CASE("showing does not auto-sleep after long idle", "[sm]")
{
    sm_t sm = make_sm();
    run_to_showing(&sm);
    sm_tick(&sm, EV_NONE, IDLE_SLEEP_MS + 100);
    TEST_ASSERT_EQUAL_INT(ST_SHOWING, sm_state(&sm));
}

TEST_CASE("tap during tumbling does not abort the animation", "[sm]")
{
    sm_t sm = make_sm();
    sm_tick(&sm, EV_TAP, 16);
    sm_tick(&sm, EV_NONE, THINK_MIN_MS + 100);   // -> TUMBLING
    sm_tick(&sm, EV_TAP, 16);                     // tap mid-tumble
    TEST_ASSERT_EQUAL_INT(ST_TUMBLING, sm_state(&sm));
}

TEST_CASE("tap during locking does not abort the snap", "[sm]")
{
    sm_t sm = make_sm();
    sm_tick(&sm, EV_TAP, 16);
    sm_tick(&sm, EV_NONE, THINK_MIN_MS + 100);   // -> TUMBLING
    sm_tick(&sm, EV_NONE, TUMBLE_MS + 50);        // -> LOCKING
    sm_tick(&sm, EV_TAP, 16);                     // tap mid-lock
    TEST_ASSERT_EQUAL_INT(ST_LOCKING, sm_state(&sm));
}
