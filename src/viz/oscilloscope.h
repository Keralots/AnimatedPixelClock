#pragma once
#include <stdint.h>

// wave: 128 samples centred on 128, or null when the companion is too old to
// send one. serial changes once per packet and advances the phosphor trail.
void drawOscilloscope(const uint8_t* wave, uint32_t serial, bool stale,
                      float dt, bool reset);
