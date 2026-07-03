/*
 * AnimatedPixelClock - Ambient: Lava Lamp
 *
 * Five metaballs on a half-resolution 64x32 field, drawn as 2x2 blocks.
 * Two thresholds give a solid core with a dimmer glow rim.
 */

#include "ambient.h"

#include "../display/display.h"

#define LAVA_W (SCREEN_WIDTH / 2)
#define LAVA_H (SCREEN_HEIGHT / 2)
#define LAVA_BLOBS 5

struct LavaBlob {
  float phaseX, phaseY;   // independent oscillation phases
  float speedX, speedY;   // radians per second
  int r2;                 // radius^2 (field strength)
};

static const LavaBlob BLOBS[LAVA_BLOBS] = {
    {0.0f, 1.1f, 0.21f, 0.34f, 90},
    {2.1f, 4.0f, 0.33f, 0.19f, 60},
    {4.2f, 2.6f, 0.16f, 0.27f, 110},
    {1.3f, 5.3f, 0.28f, 0.22f, 45},
    {5.0f, 0.4f, 0.24f, 0.31f, 75},
};

// Warm lava on a near-black blue background.
#define LAVA_CORE 0xF9E0   // orange-red
#define LAVA_GLOW 0x7940   // dim ember
#define LAVA_BG   0x0004   // faint deep blue

void ambientLavaFrame() {
  float t = millis() / 1000.0f;

  float bx[LAVA_BLOBS], by[LAVA_BLOBS];
  for (int i = 0; i < LAVA_BLOBS; i++) {
    bx[i] = LAVA_W / 2.0f + (LAVA_W / 2.0f - 8) * sinf(t * BLOBS[i].speedX + BLOBS[i].phaseX);
    by[i] = LAVA_H / 2.0f + (LAVA_H / 2.0f - 5) * sinf(t * BLOBS[i].speedY + BLOBS[i].phaseY);
  }

  for (int y = 0; y < LAVA_H; y++) {
    for (int x = 0; x < LAVA_W; x++) {
      int field = 0;
      for (int i = 0; i < LAVA_BLOBS; i++) {
        float dx = x - bx[i];
        float dy = y - by[i];
        int d2 = (int)(dx * dx + dy * dy) + 1;
        field += BLOBS[i].r2 * 256 / d2;
      }
      uint16_t c = (field > 1400) ? LAVA_CORE : (field > 900) ? LAVA_GLOW : LAVA_BG;
      display.fillRect(x * 2, y * 2, 2, 2, c);
    }
  }
}
