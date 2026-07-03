/*
 * AnimatedPixelClock - Audio Spectrum Visualizer
 *
 * Packets arrive at ~25 Hz; rendering runs at 60 Hz with exponential
 * smoothing toward the latest packet so the bars move fluidly instead of
 * stepping. Peak-hold dots fall with gravity. The bar color is a fixed
 * three-zone vertical gradient (classic EQ look) from three user-editable
 * color slots.
 */

#include "visualizer.h"

#include "../clocks/clocks.h"
#include "../config/config.h"
#include "../display/display.h"

#define VIZ_BAR_W 3          // lit pixels per bar (1px gap -> 32 * 4 = 128)
#define VIZ_MAX_H 56.0f      // px, leaves headroom for the corner clock
#define VIZ_SMOOTH 0.35f     // per-frame pull toward the packet value
#define VIZ_PEAK_GRAVITY 60.0f  // px/s^2

static uint8_t vizBands[VIZ_BANDS];
static unsigned long vizLastReceived = 0;
static bool vizEverReceived = false;

static float barH[VIZ_BANDS];
static float peakY[VIZ_BANDS];
static float peakVel[VIZ_BANDS];
static unsigned long lastVizFrame = 0;

bool vizIngest(const uint8_t* buf, int len) {
  if (len < VIZ_PACKET_LEN || memcmp(buf, "FFT1", 4) != 0) return false;
  memcpy(vizBands, buf + 4, VIZ_BANDS);
  vizLastReceived = millis();
  vizEverReceived = true;
  return true;
}

bool vizRecentEnough(unsigned long maxAgeMs) {
  return vizEverReceived && (millis() - vizLastReceived) <= maxAgeMs;
}

static unsigned long vizForcedAt = 0;

void vizNoteForced() { vizForcedAt = millis(); }

bool vizShouldDisplay() {
  return vizRecentEnough(10000) || (millis() - vizForcedAt) < 10000;
}

// Small HH:MM top-right, same idea as the ambient corner clock.
static void drawVizClock() {
  struct tm timeinfo;
  if (!getTimeWithTimeout(&timeinfo, 0)) return;
  int displayHour, displayMin;
  bool isPM;
  formatTimeForDisplay(timeinfo.tm_hour, timeinfo.tm_min, displayHour,
                       displayMin, isPM);
  char timeStr[6];
  sprintf(timeStr, "%02d%c%02d", displayHour, shouldShowColon() ? ':' : ' ',
          displayMin);
  display.fillRect(SCREEN_WIDTH - 34, 0, 34, 10, DISPLAY_BLACK);
  display.setTextSize(1);
  display.setTextColor(DISPLAY_WHITE);
  display.setCursor(SCREEN_WIDTH - 31, 1);
  display.print(timeStr);
}

void displayVisualizer() {
  unsigned long now = millis();
  float dt = (now - lastVizFrame) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  lastVizFrame = now;

  bool stale = !vizRecentEnough(2000);

  uint16_t cLow = SPRITE_COLOR(COL_VIZ_LOW);
  uint16_t cMid = SPRITE_COLOR(COL_VIZ_MID);
  uint16_t cPeak = SPRITE_COLOR(COL_VIZ_PEAK);

  // Fixed zone boundaries (from the bottom), so the gradient stays put and
  // the bars sweep through it - the classic EQ look.
  const int lowZone = 28;
  const int midZone = 45;

  for (int i = 0; i < VIZ_BANDS; i++) {
    float target = stale ? 0.0f : vizBands[i] * (VIZ_MAX_H / 255.0f);
    barH[i] += (target - barH[i]) * VIZ_SMOOTH;

    if (barH[i] >= peakY[i]) {
      peakY[i] = barH[i];
      peakVel[i] = 0;
    } else {
      peakVel[i] += VIZ_PEAK_GRAVITY * dt;
      peakY[i] -= peakVel[i] * dt;
      if (peakY[i] < 0) peakY[i] = 0;
    }

    int h = (int)barH[i];
    int x = i * 4;
    if (h > 0) {
      int hLow = h < lowZone ? h : lowZone;
      display.fillRect(x, SCREEN_HEIGHT - hLow, VIZ_BAR_W, hLow, cLow);
      if (h > lowZone) {
        int hMid = (h < midZone ? h : midZone) - lowZone;
        display.fillRect(x, SCREEN_HEIGHT - lowZone - hMid, VIZ_BAR_W, hMid, cMid);
      }
      if (h > midZone) {
        display.fillRect(x, SCREEN_HEIGHT - h, VIZ_BAR_W, h - midZone, cPeak);
      }
    }
    int py = SCREEN_HEIGHT - 1 - (int)peakY[i];
    if (peakY[i] > 1) {
      display.drawFastHLine(x, py, VIZ_BAR_W, cPeak);
    }
  }

  if (stale) {
    display.setTextSize(1);
    display.setTextColor(DISPLAY_WHITE);
    display.setCursor(25, 28);
    display.print("No audio data...");
  }

  if (settings.vizShowClock) {
    drawVizClock();
  }
}
