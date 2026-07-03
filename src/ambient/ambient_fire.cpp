/*
 * AnimatedPixelClock - Ambient: Doom Fire
 *
 * The classic PSX Doom fire: a heat buffer decays upward with random lateral
 * wind, mapped through a 37-step palette. Full 128x64 resolution; the heat
 * buffer costs 8KB of static RAM.
 */

#include "ambient.h"

#include "../config/config.h"
#include "../display/display.h"

#define FIRE_W SCREEN_WIDTH
#define FIRE_H SCREEN_HEIGHT
#define FIRE_LEVELS 37

static uint8_t heat[FIRE_W * FIRE_H];
static uint16_t firePalette[FIRE_LEVELS];
static uint8_t builtPaletteId = 0xFF;
static bool fireSeeded = false;

// Classic Doom fire palette (RGB888 triplets, cold to hot).
static const uint8_t FIRE_RGB[FIRE_LEVELS][3] = {
    {0x07, 0x07, 0x07}, {0x1F, 0x07, 0x07}, {0x2F, 0x0F, 0x07},
    {0x47, 0x0F, 0x07}, {0x57, 0x17, 0x07}, {0x67, 0x1F, 0x07},
    {0x77, 0x1F, 0x07}, {0x8F, 0x27, 0x07}, {0x9F, 0x2F, 0x07},
    {0xAF, 0x3F, 0x07}, {0xBF, 0x47, 0x07}, {0xC7, 0x47, 0x07},
    {0xDF, 0x4F, 0x07}, {0xDF, 0x57, 0x07}, {0xDF, 0x57, 0x07},
    {0xD7, 0x5F, 0x07}, {0xD7, 0x5F, 0x07}, {0xD7, 0x67, 0x0F},
    {0xCF, 0x6F, 0x0F}, {0xCF, 0x77, 0x0F}, {0xCF, 0x7F, 0x0F},
    {0xCF, 0x87, 0x17}, {0xC7, 0x87, 0x17}, {0xC7, 0x8F, 0x17},
    {0xC7, 0x97, 0x1F}, {0xBF, 0x9F, 0x1F}, {0xBF, 0x9F, 0x1F},
    {0xBF, 0xA7, 0x27}, {0xBF, 0xA7, 0x27}, {0xBF, 0xAF, 0x2F},
    {0xB7, 0xAF, 0x2F}, {0xB7, 0xB7, 0x2F}, {0xB7, 0xB7, 0x37},
    {0xCF, 0xCF, 0x6F}, {0xDF, 0xDF, 0x9F}, {0xEF, 0xEF, 0xC7},
    {0xFF, 0xFF, 0xFF},
};

// Cheap xorshift PRNG - esp_random() per pixel would dominate the frame time.
static inline uint32_t fireRnd() {
  static uint32_t s = 0x2545F491;
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

static void buildFirePalette() {
  for (int i = 0; i < FIRE_LEVELS; i++) {
    uint8_t r = FIRE_RGB[i][0], g = FIRE_RGB[i][1], b = FIRE_RGB[i][2];
    uint8_t r2 = r, g2 = g, b2 = b;
    switch (settings.ambientFirePalette) {
      case 1: r2 = b; g2 = g; b2 = r; break;  // blue flame
      case 2: r2 = g; g2 = r; b2 = b; break;  // green flame
      case 3: r2 = r; g2 = b; b2 = g; break;  // purple flame
      default: break;                          // classic orange
    }
    firePalette[i] =
        ((r2 >> 3) << 11) | ((g2 >> 2) << 5) | (b2 >> 3);
  }
  builtPaletteId = settings.ambientFirePalette;
}

void ambientFireFrame() {
  if (builtPaletteId != settings.ambientFirePalette) buildFirePalette();
  if (!fireSeeded) {
    memset(heat, 0, sizeof(heat));
    // Bottom row burns at max heat forever.
    memset(&heat[(FIRE_H - 1) * FIRE_W], FIRE_LEVELS - 1, FIRE_W);
    fireSeeded = true;
  }

  // Propagate upward with 1px random wind and random decay.
  for (int y = 0; y < FIRE_H - 1; y++) {
    uint8_t* dstRow = &heat[y * FIRE_W];
    uint8_t* srcRow = &heat[(y + 1) * FIRE_W];
    for (int x = 0; x < FIRE_W; x++) {
      uint32_t r = fireRnd();
      int wind = (int)(r % 3) - 1;
      int dx = x + wind;
      if (dx < 0) dx = 0;
      if (dx >= FIRE_W) dx = FIRE_W - 1;
      int v = srcRow[x] - (int)(r & 1);
      dstRow[dx] = v < 0 ? 0 : (uint8_t)v;
    }
  }

  for (int y = 0; y < FIRE_H; y++) {
    for (int x = 0; x < FIRE_W; x++) {
      display.drawPixel(x, y, firePalette[heat[y * FIRE_W + x]]);
    }
  }
}
