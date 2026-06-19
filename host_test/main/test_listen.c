#include "unity.h"
#include "listen.h"
#include "config.h"

TEST_CASE("starts idle, emits nothing", "[listen]")
{
    listen_t l;
    listen_init(&l);
    TEST_ASSERT_EQUAL_INT(LISTEN_IDLE, l.state);
    TEST_ASSERT_EQUAL_INT(EV_NONE, listen_tick(&l, false, false, 16));
    TEST_ASSERT_EQUAL_INT(LISTEN_IDLE, l.state);
}

TEST_CASE("wake word starts pondering", "[listen]")
{
    listen_t l;
    listen_init(&l);
    event_t ev = listen_tick(&l, true, false, 16);
    TEST_ASSERT_EQUAL_INT(EV_SHAKE, ev);
    TEST_ASSERT_EQUAL_INT(LISTEN_LISTENING, l.state);
}

TEST_CASE("silence after min-speech ends the ask", "[listen]")
{
    listen_t l;
    listen_init(&l);
    listen_tick(&l, true, true, 16);                 // wake + talking
    listen_tick(&l, false, true, MIN_SPEECH_MS);     // keep talking past min-speech
    event_t ev = listen_tick(&l, false, false, SILENCE_MS);
    TEST_ASSERT_EQUAL_INT(EV_NONE, ev);
    TEST_ASSERT_EQUAL_INT(LISTEN_IDLE, l.state);
}

TEST_CASE("silence before min-speech does NOT end the ask", "[listen]")
{
    listen_t l;
    listen_init(&l);
    listen_tick(&l, true, false, 16);                // wake, no speech yet
    event_t ev = listen_tick(&l, false, false, SILENCE_MS);
    TEST_ASSERT_EQUAL_INT(EV_SHAKE, ev);
    TEST_ASSERT_EQUAL_INT(LISTEN_LISTENING, l.state);
}

TEST_CASE("speech resets the silence timer", "[listen]")
{
    listen_t l;
    listen_init(&l);
    listen_tick(&l, true, true, MIN_SPEECH_MS + 16); // wake + talk past min-speech
    listen_tick(&l, false, false, SILENCE_MS - 100); // almost-silent (not enough)
    listen_tick(&l, false, true, 50);                // talks again -> resets
    event_t ev = listen_tick(&l, false, false, SILENCE_MS - 100); // not enough again
    TEST_ASSERT_EQUAL_INT(EV_SHAKE, ev);
    TEST_ASSERT_EQUAL_INT(LISTEN_LISTENING, l.state);
}

TEST_CASE("endless talking fires after the max window", "[listen]")
{
    listen_t l;
    listen_init(&l);
    listen_tick(&l, true, true, 16);   // wake + talking
    event_t ev = EV_SHAKE;
    uint32_t elapsed = 16;
    while (elapsed < LISTEN_MAX_MS + 200 && ev == EV_SHAKE) {
        ev = listen_tick(&l, false, true, 100);
        elapsed += 100;
    }
    TEST_ASSERT_EQUAL_INT(EV_NONE, ev);
    TEST_ASSERT_EQUAL_INT(LISTEN_IDLE, l.state);
}

TEST_CASE("a fresh wake re-arms after a completed ask", "[listen]")
{
    listen_t l;
    listen_init(&l);
    listen_tick(&l, true, true, MIN_SPEECH_MS + 16);     // ask 1: wake + talk
    event_t end = listen_tick(&l, false, false, SILENCE_MS); // ask 1 ends
    TEST_ASSERT_EQUAL_INT(EV_NONE, end);
    TEST_ASSERT_EQUAL_INT(LISTEN_IDLE, l.state);
    event_t ev2 = listen_tick(&l, true, false, 16);
    TEST_ASSERT_EQUAL_INT(EV_SHAKE, ev2);
    TEST_ASSERT_EQUAL_INT(LISTEN_LISTENING, l.state);
}

TEST_CASE("idle ignores stray speech without a wake", "[listen]")
{
    listen_t l;
    listen_init(&l);
    TEST_ASSERT_EQUAL_INT(EV_NONE, listen_tick(&l, false, true, 100));
    TEST_ASSERT_EQUAL_INT(LISTEN_IDLE, l.state);
}

TEST_CASE("wake with no speech fires after the fixed ponder", "[listen]")
{
    listen_t l;
    listen_init(&l);
    listen_tick(&l, true, false, 16);   // wake, and no speech ever follows
    // Hold through the ponder window; it must release right around PONDER_MS,
    // well before the LISTEN_MAX_MS backstop.
    event_t ev = EV_SHAKE;
    uint32_t elapsed = 16;
    while (elapsed < PONDER_MS + 200 && ev == EV_SHAKE) {
        ev = listen_tick(&l, false, false, 100);
        elapsed += 100;
    }
    TEST_ASSERT_EQUAL_INT(EV_NONE, ev);
    TEST_ASSERT_EQUAL_INT(LISTEN_IDLE, l.state);
    TEST_ASSERT_TRUE(elapsed < LISTEN_MAX_MS);   // fired via ponder, not the backstop
}

TEST_CASE("wake holds (ponders) before the fixed window elapses", "[listen]")
{
    listen_t l;
    listen_init(&l);
    listen_tick(&l, true, false, 16);
    event_t ev = listen_tick(&l, false, false, PONDER_MS - 500);
    TEST_ASSERT_EQUAL_INT(EV_SHAKE, ev);   // still pondering
    TEST_ASSERT_EQUAL_INT(LISTEN_LISTENING, l.state);
}
