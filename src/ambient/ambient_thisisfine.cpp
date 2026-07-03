/*
 * AnimatedPixelClock - Ambient: Burning Room ("This is fine")
 *
 * Faithful to the comic panel: a golden dog with floppy black ears sits at
 * its table with a white mug while yellow flames consume a khaki room under
 * a drifting gray smoke ceiling. Layers back-to-front: wall + smoke + a
 * picture frame, tall doom-fire tongues (own buffer, yellow palette),
 * furniture and dog, then a low band of the same fire re-drawn in front so
 * the flames surround the scene. The dog blinks and bobs; a white speech
 * bubble declares THIS IS FINE. on a 9 second cycle.
 */

#include "ambient.h"

#include "../config/config.h"
#include "../display/display.h"

// Back fire strip: bottom 44 rows, tongues reach about half the screen.
#define TIF_W SCREEN_WIDTH
#define TIF_H 44
#define TIF_Y0 (SCREEN_HEIGHT - TIF_H)
#define TIF_LEVELS 37
#define TIF_FRONT_ROWS 11  // rows of fire re-drawn in front of the scene

static uint8_t tifHeat[TIF_W * TIF_H];
static uint16_t tifPal[TIF_LEVELS];
static bool tifInit = false;

// Scene palette (from the panel)
#define TIF_WALL   0x5A44  // dark khaki wall
#define TIF_SMOKE1 0x4208  // smoke base
#define TIF_SMOKE2 0x630C  // smoke highlight blobs
#define TIF_GOLD   0xF5A5  // dog
#define TIF_TABLE  0x9240  // table / chair
#define TIF_FRAME  0x8A80  // picture frame
#define TIF_FRAMEI 0x5140  // frame inlay

static inline uint32_t tifRnd() {
  static uint32_t s = 0x9E3779B9;
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

// Yellow-dominant flame ramp (the comic's fire is yellow with orange roots),
// unlike the red-orange shared Doom palette. Levels 0-1 stay transparent so
// the wall shows through between tongues.
static void tifBuildPalette() {
  for (int i = 0; i < TIF_LEVELS; i++) {
    uint16_t c;
    if (i < 2)       c = 0x0000;
    else if (i < 9)  c = 0x50A0;  // ember brown-red
    else if (i < 17) c = 0xDAC0;  // orange
    else if (i < 27) c = 0xFD00;  // orange-yellow
    else if (i < 34) c = 0xFEE0;  // yellow
    else             c = 0xFFF2;  // pale hot core
    tifPal[i] = c;
  }
}

static void tifFireStep() {
  // Ignition row at full heat; decay averages ~1.5/row so the tongues top
  // out around 24 rows with flickering gaps between them.
  memset(&tifHeat[(TIF_H - 1) * TIF_W], TIF_LEVELS - 1, TIF_W);

  static unsigned long lastPropagate = 0;
  unsigned long now = millis();
  if (now - lastPropagate < 33) return;
  lastPropagate = now;

  for (int y = 0; y < TIF_H - 1; y++) {
    uint8_t* dstRow = &tifHeat[y * TIF_W];
    uint8_t* srcRow = &tifHeat[(y + 1) * TIF_W];
    for (int x = 0; x < TIF_W; x++) {
      uint32_t r = tifRnd();
      int wind = (int)(r % 3) - 1;
      int dx = x + wind;
      if (dx < 0) dx = 0;
      if (dx >= TIF_W) dx = TIF_W - 1;
      int v = srcRow[x] - (int)((r >> 2) % 4);
      dstRow[dx] = v < 0 ? 0 : (uint8_t)v;
    }
  }
}

static void tifDrawFire(int fromRow, int xShift) {
  for (int y = fromRow; y < TIF_H; y++) {
    uint8_t* row = &tifHeat[y * TIF_W];
    for (int x = 0; x < TIF_W; x++) {
      uint8_t h = row[(x + xShift) % TIF_W];
      if (h > 1) display.drawPixel(x, TIF_Y0 + y, tifPal[h]);
    }
  }
}

static void tifDrawRoom(float t) {
  // Khaki wall behind everything.
  display.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, TIF_WALL);

  // Smoke ceiling: dark band with a slow wavy fringe and drifting highlights.
  display.fillRect(0, 0, SCREEN_WIDTH, 9, TIF_SMOKE1);
  for (int x = 0; x < SCREEN_WIDTH; x++) {
    int fringe = 9 + (int)(2.5f * sinf(x * 0.11f + t * 0.5f) +
                           1.5f * sinf(x * 0.23f - t * 0.3f));
    display.drawFastVLine(x, 9, fringe - 9 > 0 ? fringe - 9 : 0, TIF_SMOKE1);
  }
  for (int i = 0; i < 3; i++) {
    int bx = (int)(fmodf(t * (4.0f + i * 1.5f) + i * 47.0f, SCREEN_WIDTH + 24)) - 12;
    display.fillCircle(bx, 4 + i, 4, TIF_SMOKE2);
  }

  // Picture frame on the right wall.
  display.fillRect(100, 16, 16, 22, TIF_FRAME);
  display.fillRect(102, 18, 12, 18, TIF_FRAMEI);
  display.drawFastHLine(102, 27, 12, TIF_FRAME);
}

static void tifDrawDog(unsigned long now) {
  // 1px head bob on a slow beat; blink every ~4.2s.
  int bob = ((now / 1200) % 2) ? 1 : 0;
  bool blink = (now % 4200) < 180;
  int hx = 56, hy = 26 + bob;  // head center

  // Chair back, then body behind the table.
  display.fillRect(42, 34, 3, 18, TIF_TABLE);
  display.fillRect(48, 38, 16, 12, TIF_GOLD);

  // Floppy black ears hang beside the head.
  display.fillRect(hx - 10, hy - 4, 4, 9, DISPLAY_BLACK);
  display.fillRect(hx + 6, hy - 4, 4, 9, DISPLAY_BLACK);

  // Round golden head with the muzzle pushed right-of-center.
  display.fillCircle(hx, hy, 7, TIF_GOLD);
  display.fillRect(hx - 2, hy + 2, 10, 5, TIF_GOLD);

  // White eye patches with pupils (shut to a line while blinking).
  display.fillRect(hx - 4, hy - 3, 3, 4, DISPLAY_WHITE);
  display.fillRect(hx + 1, hy - 3, 3, 4, DISPLAY_WHITE);
  if (blink) {
    display.drawFastHLine(hx - 4, hy - 1, 3, DISPLAY_BLACK);
    display.drawFastHLine(hx + 1, hy - 1, 3, DISPLAY_BLACK);
  } else {
    display.drawPixel(hx - 3, hy - 2, DISPLAY_BLACK);
    display.drawPixel(hx + 2, hy - 2, DISPLAY_BLACK);
  }

  // Black nose tip and the calm mouth.
  display.fillRect(hx + 6, hy + 3, 3, 3, DISPLAY_BLACK);
  display.drawFastHLine(hx + 1, hy + 6, 5, DISPLAY_BLACK);

  // Table with the arm resting toward the mug.
  display.fillRect(40, 46, 56, 4, TIF_TABLE);
  display.fillRect(60, 43, 14, 3, TIF_GOLD);  // arm on the tabletop

  // White mug (steam wiggles above it).
  display.fillRect(76, 39, 6, 7, DISPLAY_WHITE);
  display.drawPixel(83, 40, DISPLAY_WHITE);
  display.drawPixel(83, 42, DISPLAY_WHITE);
  bool phase = (now / 400) % 2 == 0;
  display.drawPixel(phase ? 77 : 78, 36, TIF_SMOKE2);
  display.drawPixel(phase ? 79 : 78, 34, TIF_SMOKE2);
}

static void tifDrawBubble(unsigned long now) {
  if ((now % 9000) >= 4000) return;
  // White comic bubble with black text, tail toward the dog.
  display.fillRoundRect(4, 2, 52, 21, 4, DISPLAY_WHITE);
  display.fillTriangle(40, 22, 46, 22, 50, 27, DISPLAY_WHITE);
  display.setTextSize(1);
  display.setTextColor(DISPLAY_BLACK);
  display.setCursor(9, 5);
  display.print("THIS IS");
  display.setCursor(9, 13);
  display.print("FINE.");
  display.setTextColor(DISPLAY_WHITE);
}

void ambientThisIsFineFrame() {
  if (!tifInit) {
    tifBuildPalette();
    memset(tifHeat, 0, sizeof(tifHeat));
    tifInit = true;
  }

  unsigned long now = millis();
  float t = now / 1000.0f;

  tifFireStep();
  tifDrawRoom(t);
  tifDrawFire(0, 0);        // flames behind the furniture
  tifDrawDog(now);
  tifDrawFire(TIF_H - TIF_FRONT_ROWS, 41);  // foreground flames, decorrelated
  tifDrawBubble(now);
}
