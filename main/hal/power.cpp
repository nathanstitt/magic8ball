#include "power.h"
#include "display.h"
#include "esp_log.h"

static const char *TAG = "power";

int power_init(void)
{
    // Probe/configure AXP2101 (enable display rail, set charge params). Port from demo.
    ESP_LOGI(TAG, "power_init: AXP2101");
    return 0; // non-fatal if it can't be configured; display path still works on USB power
}

void power_display_sleep(bool sleep)
{
    display_set_brightness(sleep ? 0 : 255);
}
