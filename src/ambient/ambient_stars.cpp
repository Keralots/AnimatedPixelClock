/*
 * AnimatedPixelClock - Ambient: Starfield Warp
 *
 * Perspective starfield flying toward the viewer. dt-based so the speed is
 * refresh-rate independent.
 */

#include "ambient.h"

#include "../display/display.h"

#define STAR_COUNT 96

struct Star {
  float x, y, z;  // x,y in [-1,1], z in (0.05, 1]
};

static Star stars[STAR_COUNT];
static bool starsInit = false;
static unsigned long lastStarUpdate = 0;

static void respawnStar(Star& s) {
  s.x = random(-1000, 1001) / 1000.0f;
  s.y = random(-1000, 1001) / 1000.0f;
  s.z = random(400, 1001) / 1000.0f;
}

void ambientStarsFrame() {
  unsigned long now = millis();
  if (!starsInit) {
    for (int i = 0; i < STAR_COUNT; i++) {
      respawnStar(stars[i]);
      stars[i].z = random(50, 1001) / 1000.0f;  // spread initial depth
    }
    lastStarUpdate = now;
    starsInit = true;
  }

  float dt = (now - lastStarUpdate) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  lastStarUpdate = now;

  for (int i = 0; i < STAR_COUNT; i++) {
    Star& s = stars[i];
    s.z -= 0.55f * dt;
    if (s.z <= 0.05f) {
      respawnStar(s);
      continue;
    }
    int px = SCREEN_WIDTH / 2 + (int)(s.x / s.z * 44);
    int py = SCREEN_HEIGHT / 2 + (int)(s.y / s.z * 26);
    if (px < 0 || px >= SCREEN_WIDTH || py < 0 || py >= SCREEN_HEIGHT) {
      respawnStar(s);
      continue;
    }
    // Closer stars are brighter and bigger.
    if (s.z > 0.6f) {
      display.drawPixel(px, py, 0x4208);           // dim gray
    } else if (s.z > 0.3f) {
      display.drawPixel(px, py, 0x8410);           // mid gray
    } else {
      display.drawPixel(px, py, DISPLAY_WHITE);
      display.drawPixel(px + 1, py, 0x8410);       // slight streak
    }
  }
}
