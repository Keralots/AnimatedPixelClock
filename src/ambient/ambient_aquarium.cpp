/*
 * AnimatedPixelClock - Ambient: Aquarium
 *
 * Fish cruising at different depths and speeds, rising bubbles, swaying
 * kelp and a sandy floor. All motion is dt-based.
 */

#include "ambient.h"

#include "../display/display.h"

#define AQ_FISH 5
#define AQ_BUBBLES 6
#define AQ_KELP 3
#define AQ_FLOOR_Y (SCREEN_HEIGHT - 2)

struct AqFish {
  float x;
  int y;
  float speed;      // px/s, sign = direction
  uint16_t color;
};

struct AqBubble {
  float x, y;
  float speed;      // px/s upward
  float sway;       // phase
};

static AqFish fish[AQ_FISH] = {
    {10, 12, 14.0f, 0xFC00},    // orange
    {60, 22, -10.0f, 0x07FF},   // cyan
    {100, 34, 8.0f, 0xFFE0},    // yellow
    {30, 44, -16.0f, 0xF81F},   // magenta
    {80, 52, 11.0f, 0x87F0},    // light green
};
static AqBubble bubbles[AQ_BUBBLES];
static bool aqInit = false;
static unsigned long lastAqUpdate = 0;

static const int KELP_X[AQ_KELP] = {14, 58, 108};
static const int KELP_H[AQ_KELP] = {26, 18, 30};

static void respawnBubble(AqBubble& b) {
  b.x = random(4, SCREEN_WIDTH - 4);
  b.y = AQ_FLOOR_Y;
  b.speed = random(8, 18);
  b.sway = random(0, 628) / 100.0f;
}

// A small fish: oval body, flapping triangle tail, one eye pixel.
static void drawFish(const AqFish& f, bool tailUp) {
  int x = (int)f.x;
  bool right = f.speed > 0;
  int bodyX = x;
  display.fillCircle(bodyX, f.y, 2, f.color);
  display.fillCircle(bodyX + (right ? 2 : -2), f.y, 1, f.color);
  int tailBase = bodyX + (right ? -3 : 3);
  int tailTip = tailBase + (right ? -3 : 3);
  display.fillTriangle(tailBase, f.y, tailTip, f.y - (tailUp ? 3 : 1),
                       tailTip, f.y + (tailUp ? 1 : 3), f.color);
  display.drawPixel(bodyX + (right ? 3 : -3), f.y - 1, DISPLAY_BLACK);
}

void ambientAquariumFrame() {
  unsigned long now = millis();
  if (!aqInit) {
    for (int i = 0; i < AQ_BUBBLES; i++) {
      respawnBubble(bubbles[i]);
      bubbles[i].y = random(10, AQ_FLOOR_Y);
    }
    lastAqUpdate = now;
    aqInit = true;
  }

  float dt = (now - lastAqUpdate) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  lastAqUpdate = now;
  float t = now / 1000.0f;

  // Sandy floor with a few darker speckles.
  display.drawFastHLine(0, AQ_FLOOR_Y, SCREEN_WIDTH, 0xCDAC);
  display.drawFastHLine(0, AQ_FLOOR_Y + 1, SCREEN_WIDTH, 0x8B44);
  for (int x = 6; x < SCREEN_WIDTH; x += 17) {
    display.drawPixel(x, AQ_FLOOR_Y, 0x8B44);
  }

  // Kelp: wavy vertical stalks with leaf nubs, swaying in sync with depth.
  for (int k = 0; k < AQ_KELP; k++) {
    for (int i = 0; i < KELP_H[k]; i++) {
      int y = AQ_FLOOR_Y - 1 - i;
      int x = KELP_X[k] + (int)(2.5f * sinf(t * 1.3f + k * 2.0f + i * 0.28f));
      display.drawPixel(x, y, 0x0560);
      if (i % 5 == 3) {
        display.drawPixel(x + ((i % 2) ? 1 : -1), y, 0x0560);
      }
    }
  }

  // Bubbles rise with a gentle sway and pop at the surface.
  for (int i = 0; i < AQ_BUBBLES; i++) {
    AqBubble& b = bubbles[i];
    b.y -= b.speed * dt;
    if (b.y < 2) {
      respawnBubble(b);
      continue;
    }
    int bx = (int)(b.x + 2.0f * sinf(t * 2.0f + b.sway));
    display.drawPixel(bx, (int)b.y, 0x861F);
  }

  // Fish cruise and wrap around off-screen.
  for (int i = 0; i < AQ_FISH; i++) {
    AqFish& f = fish[i];
    f.x += f.speed * dt;
    if (f.speed > 0 && f.x > SCREEN_WIDTH + 8) f.x = -8;
    if (f.speed < 0 && f.x < -8) f.x = SCREEN_WIDTH + 8;
    bool tailUp = ((now / 250) + i) % 2 == 0;
    drawFish(f, tailUp);
  }
}
