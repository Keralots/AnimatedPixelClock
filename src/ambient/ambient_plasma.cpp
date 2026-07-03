/*
 * AnimatedPixelClock - Ambient: Plasma
 *
 * Three summed sine waves indexed through a rainbow palette. Integer LUT
 * math throughout - no per-pixel float trig.
 */

#include "ambient.h"

#include "../display/display.h"

static uint8_t sinLUT[256];
static uint16_t rainbow[256];
static bool plasmaInit = false;

// HSV (h 0-255, full s/v) to RGB565, enough for a palette build.
static uint16_t hueToRgb565(uint8_t h) {
  uint8_t region = h / 43;
  uint8_t rem = (h - region * 43) * 6;
  uint8_t q = 255 - rem;
  uint8_t r = 0, g = 0, b = 0;
  switch (region) {
    case 0: r = 255; g = rem; break;
    case 1: r = q; g = 255; break;
    case 2: g = 255; b = rem; break;
    case 3: g = q; b = 255; break;
    case 4: r = rem; b = 255; break;
    default: r = 255; b = q; break;
  }
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

static void initPlasma() {
  for (int i = 0; i < 256; i++) {
    sinLUT[i] = (uint8_t)(128.0f + 127.0f * sinf(i * (2.0f * PI / 256.0f)));
    rainbow[i] = hueToRgb565((uint8_t)i);
  }
  plasmaInit = true;
}

void ambientPlasmaFrame() {
  if (!plasmaInit) initPlasma();

  uint32_t t = millis() / 16;
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    uint8_t sy = sinLUT[(uint8_t)(y * 3 + (t >> 1))];
    for (int x = 0; x < SCREEN_WIDTH; x++) {
      uint8_t v = (uint8_t)((sinLUT[(uint8_t)(x * 2 + t)] +
                             sinLUT[(uint8_t)(x + y * 2 - t)] + sy) /
                            3);
      display.drawPixel(x, y, rainbow[(uint8_t)(v + t)]);
    }
  }
}
