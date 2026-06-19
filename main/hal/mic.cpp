#include "mic.h"

// Stub until a later task wires the ES7210 I2S capture.
int mic_init(void) { return -1; }
size_t mic_read(int16_t *out, size_t max) { (void)out; (void)max; return 0; }
