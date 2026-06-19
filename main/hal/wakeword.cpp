#include "wakeword.h"

// Stub until a later task wires microWakeWord. init returns -1 so app_main treats
// voice as unavailable (shake/tap still work).
int  wakeword_init(void) { return -1; }
void wakeword_update(void) {}
bool wakeword_detected(void) { return false; }
bool wakeword_speech_active(void) { return false; }
