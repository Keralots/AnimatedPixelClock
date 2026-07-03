/*
 * AnimatedPixelClock - Ambient: Burning Room ("This is fine")
 *
 * A calm dog sits at a table with its coffee while the room burns around
 * it. Flames are a 128x32 strip of the Doom-fire algorithm (own buffer,
 * faster decay so they lick the lower half); the scene is drawn on top so
 * the furniture sits in front of the fire. A speech bubble fades in and
 * out on a 9 second cycle.
 */

#include "ambient.h"

#include "../config/config.h"
#include "../display/display.h"

#define TIF_W SCREEN_WIDTH
#define TIF_H 32
#define TIF_Y0 (SCREEN_HEIGHT - TIF_H)
#define TIF_LEVELS 37

static uint8_t tifHeat[TIF_W * TIF_H];
static bool tifSeeded = false;

// Scene colors
#define TIF_TAN    0xE5AF  // dog body
#define TIF_BROWN  0xA145  // ears, table, chair
#define TIF_MUG    0xFFFF
#define TIF_SMOKE  0x630C  // dark gray

static inline uint32_t tifRnd() {
  static uint32_t s = 0x9E3779B9;
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}

static void tifFireStrip() {
  const uint16_t* pal = ambientFirePalette37();

  if (!tifSeeded) {
    memset(tifHeat, 0, sizeof(tifHeat));
    tifSeeded = true;
  }
  // Ignition row at full heat; faster decay (avg 1/row vs the full-screen
  // fire's 0.5) keeps the flames to roughly the strip height.
  memset(&tifHeat[(TIF_H - 1) * TIF_W], TIF_LEVELS - 1, TIF_W);

  static unsigned long lastPropagate = 0;
  unsigned long now = millis();
  if (now - lastPropagate >= 33) {
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
        int v = srcRow[x] - (int)((r >> 2) % 3);
        dstRow[dx] = v < 0 ? 0 : (uint8_t)v;
      }
    }
  }

  for (int y = 0; y < TIF_H; y++) {
    uint8_t* row = &tifHeat[y * TIF_W];
    for (int x = 0; x < TIF_W; x++) {
      if (row[x] > 1) display.drawPixel(x, TIF_Y0 + y, pal[row[x]]);
    }
  }
}

static void tifScene() {
  unsigned long now = millis();

  // Chair (backrest + seat; the legs are lost in the flames anyway).
  display.fillRect(36, 28, 2, 18, TIF_BROWN);
  display.fillRect(38, 44, 14, 2, TIF_BROWN);

  // Dog: slouched body, boxy head with ears, snout toward the table.
  display.fillRect(40, 34, 10, 12, TIF_TAN);            // body
  display.fillRect(42, 23, 11, 9, TIF_TAN);             // head
  display.fillRect(42, 20, 3, 4, TIF_BROWN);            // left ear
  display.fillRect(50, 20, 3, 4, TIF_BROWN);            // right ear
  display.fillRect(53, 26, 5, 4, TIF_TAN);              // snout
  display.drawPixel(57, 27, DISPLAY_BLACK);             // nose
  display.drawPixel(47, 26, DISPLAY_BLACK);             // eyes
  display.drawPixel(51, 26, DISPLAY_BLACK);
  display.drawFastHLine(53, 29, 4, DISPLAY_BLACK);      // content smile
  display.fillRect(50, 35, 16, 2, TIF_TAN);             // arm resting on table

  // Table with two legs and the coffee mug.
  display.fillRect(64, 37, 40, 3, TIF_BROWN);
  display.fillRect(68, 40, 2, 18, TIF_BROWN);
  display.fillRect(98, 40, 2, 18, TIF_BROWN);
  display.fillRect(82, 31, 5, 6, TIF_MUG);
  display.drawPixel(88, 32, TIF_MUG);                   // handle
  display.drawPixel(88, 34, TIF_MUG);

  // Steam over the mug, alternating pixels so it wiggles.
  bool phase = (now / 400) % 2 == 0;
  display.drawPixel(phase ? 83 : 84, 28, TIF_SMOKE);
  display.drawPixel(phase ? 85 : 84, 26, TIF_SMOKE);

  // Speech bubble, 4s on / 5s off.
  if ((now % 9000) < 4000) {
    display.drawRoundRect(4, 2, 52, 21, 4, DISPLAY_WHITE);
    display.drawLine(42, 23, 46, 26, DISPLAY_WHITE);    // tail toward the dog
    display.setTextSize(1);
    display.setTextColor(DISPLAY_WHITE);
    display.setCursor(9, 5);
    display.print("THIS IS");
    display.setCursor(9, 13);
    display.print("FINE.");
  }
}

void ambientThisIsFineFrame() {
  tifFireStrip();
  tifScene();
}
