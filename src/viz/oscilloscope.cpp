/* CRT-style scope: graticule, trigger-aligned trace, phosphor trail. */
#include "oscilloscope.h"
#include "visualizer.h"
#include "../display/display.h"
#include "../config/config.h"
#include <math.h>
#include <string.h>

namespace {
uint8_t trail[SCOPE_TRAIL_MAX][VIZ_WAVE_POINTS];
uint8_t previousWave[VIZ_WAVE_POINTS];
bool trailUsed[SCOPE_TRAIL_MAX] = {};
int trailHead = 0;
uint32_t seenSerial = 0;
bool everSeen = false;

uint16_t rgb(int r, int g, int b) {
  if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
  if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}
void unpack(uint16_t c, int& r, int& g, int& b) {
  r = ((c >> 11) & 0x1f) * 255 / 31;
  g = ((c >> 5) & 0x3f) * 255 / 63;
  b = (c & 0x1f) * 255 / 31;
}

struct Palette { int traceR, traceG, traceB, peakR, peakG, peakB; };

void drawTrace(const uint8_t* wave, float cy, float halfHeight, float gain,
               float light, const Palette& p) {
  int prevX = 0, prevY = 0;
  int centre = (int)lroundf(cy);
  for (int i = 0; i < VIZ_WAVE_POINTS; i++) {
    float deflect = (wave[i] - 128) / 128.0f * gain;
    if (deflect > 1.0f) deflect = 1.0f;
    if (deflect < -1.0f) deflect = -1.0f;
    int y = (int)lroundf(cy - deflect * halfHeight);
    float hot = settings.scopeFlat ? 0.0f : fabsf(deflect);
    uint16_t color = rgb((int)((p.traceR + (p.peakR - p.traceR) * hot) * light),
                         (int)((p.traceG + (p.peakG - p.traceG) * hot) * light),
                         (int)((p.traceB + (p.peakB - p.traceB) * hot) * light));
    if (settings.scopeFill) {
      uint16_t body = rgb((int)((p.traceR + (p.peakR - p.traceR) * hot) * light * 0.45f),
                          (int)((p.traceG + (p.peakG - p.traceG) * hot) * light * 0.45f),
                          (int)((p.traceB + (p.peakB - p.traceB) * hot) * light * 0.45f));
      display.drawLine(i, centre, i, y, body);
    }
    if (i > 0) display.drawLine(prevX, prevY, i, y, color);
    else display.drawPixel(i, y, color);
    prevX = i; prevY = y;
  }
}
}

void drawOscilloscope(const uint8_t* wave, uint32_t serial, bool stale,
                      float dt, bool reset) {
  (void)dt;
  if (reset || wave == nullptr || stale) {
    memset(trailUsed, 0, sizeof(trailUsed));
    trailHead = 0;
    seenSerial = serial;
    everSeen = false;
  }

  // Centre of rows 0..63 is 31.5, not 32, and full deflection must reach the
  // outermost row. With the clock on, the trace starts below its 10px band.
  const float cy = settings.vizShowClock ? 36.5f : 31.5f;
  const float halfHeight = settings.vizShowClock ? 26.5f : 31.5f;
  const int top = (int)lroundf(cy - halfHeight);
  const int bottom = (int)lroundf(cy + halfHeight);
  const int centre = (int)lroundf(cy);

  int gridR, gridG, gridB;
  unpack(SPRITE_COLOR(COL_SCOPE_GRID), gridR, gridG, gridB);
  Palette p;
  unpack(SPRITE_COLOR(COL_SCOPE_TRACE), p.traceR, p.traceG, p.traceB);
  unpack(SPRITE_COLOR(COL_SCOPE_PEAK), p.peakR, p.peakG, p.peakB);

  if (settings.scopeGrid) {
    uint16_t grid = rgb(gridR / 5, gridG / 5, gridB / 5);
    uint16_t axis = rgb(gridR / 3, gridG / 3, gridB / 3);
    for (int x = 16; x < SCREEN_WIDTH; x += 16)
      for (int y = top; y <= bottom; y += 2) display.drawPixel(x, y, grid);
    for (int step = -2; step <= 2; step++) {
      if (step == 0) continue;
      int y = centre + (int)lroundf(step * halfHeight / 2.5f);
      for (int x = 0; x < SCREEN_WIDTH; x += 2) display.drawPixel(x, y, grid);
    }
    for (int x = 0; x < SCREEN_WIDTH; x++) display.drawPixel(x, centre, axis);
    for (int x = 4; x < SCREEN_WIDTH; x += 4) {
      display.drawPixel(x, centre - 1, axis);
      display.drawPixel(x, centre + 1, axis);
    }
  }

  if (wave == nullptr) {
    display.setTextSize(1);
    display.setTextColor(rgb(p.peakR, p.peakG, p.peakB));
    display.setCursor(4, centre - 20);
    display.print("Update PC companion");
    display.setCursor(4, centre + 12);
    display.print("for the waveform");
    return;
  }

  if (stale) return;

  const int depth = settings.scopeTrail > SCOPE_TRAIL_MAX ? SCOPE_TRAIL_DEFAULT
                                                          : settings.scopeTrail;
  if (serial != seenSerial || !everSeen) {
    if (everSeen && depth > 0) {
      trailHead = (trailHead + SCOPE_TRAIL_MAX - 1) % SCOPE_TRAIL_MAX;
      memcpy(trail[trailHead], previousWave, VIZ_WAVE_POINTS);
      trailUsed[trailHead] = true;
    }
    memcpy(previousWave, wave, VIZ_WAVE_POINTS);
    seenSerial = serial;
    everSeen = true;
  }

  const float gain = settings.scopeGain / 100.0f;
  for (int age = depth - 1; age >= 0; age--) {
    int slot = (trailHead + age) % SCOPE_TRAIL_MAX;
    if (!trailUsed[slot]) continue;
    drawTrace(trail[slot], cy, halfHeight, gain, 0.38f - age * 0.10f, p);
  }
  if (!stale) drawTrace(wave, cy, halfHeight, gain, 1.0f, p);
}
