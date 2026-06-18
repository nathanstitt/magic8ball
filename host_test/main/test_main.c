#include "unity.h"
#include "scene.h"

TEST_CASE("harness runs", "[harness]")
{
    TEST_ASSERT_EQUAL_INT(2, 1 + 1);
}

TEST_CASE("scene_t has expected enum order", "[scene]")
{
    TEST_ASSERT_EQUAL_INT(0, ST_IDLE);
    TEST_ASSERT_EQUAL_INT(1, ST_SHAKING);
    TEST_ASSERT_EQUAL_INT(2, ST_TUMBLING);
    TEST_ASSERT_EQUAL_INT(3, ST_LOCKING);
    TEST_ASSERT_EQUAL_INT(4, ST_SHOWING);
    TEST_ASSERT_EQUAL_INT(5, ST_SLEEP);
}

void app_main(void)
{
    unity_run_menu();
}
