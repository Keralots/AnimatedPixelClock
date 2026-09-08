#pragma once
#include <stdint.h>

// Smoothed levels drive flight; raw FFT packets drive immediate bass onsets.
void drawStarfieldOverdrive(const float* levels, const uint8_t* rawBands,
                           uint32_t packetSerial, float dt, bool reset);
