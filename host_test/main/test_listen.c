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
