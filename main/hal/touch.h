#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int  touch_init(void);   // 0 on success, -1 on failure
// Read the controller ONCE per logic tick and latch the current level + tap
// edge. Call this exactly once each tick before touch_was_tapped()/is_down();
// those are pure getters over what this latched. Reading the CST9217 more than
// once per tick returns stale/empty frames (each INT cycle yields one report),
// which made the reset gesture flicker down/up on release.
void touch_poll(void);
// True exactly once per fresh touch-down (edge-triggered tap). Getter over the
// last touch_poll(); does not read the controller.
bool touch_was_tapped(void);
// True while a finger is currently on the panel (level, not edge). Getter over
// the last touch_poll(); does not read the controller.
bool touch_is_down(void);

#ifdef __cplusplus
}
#endif
