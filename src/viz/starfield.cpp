/* Audio-reactive adaptation of ambient_stars.cpp's perspective starfield. */
#include "starfield.h"
#include "../display/display.h"
#include "../config/config.h"
#include <math.h>

namespace {
struct MusicStar { float x, y, z; };
MusicStar stars[96];
float drive = 0, boost = 0, cooldown = 0, phase = 0;
float previousBass[8] = {}, fluxAverage = 0, packetAge = 0;
uint32_t seenPacket = 0;
float trailBaseline[32] = {};

uint16_t rgb(int r, int g, int b) {
  return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}
void spawn(MusicStar& s) {
  s.x = random(-1000, 1001) / 1000.0f;
  s.y = random(-1000, 1001) / 1000.0f;
  if (fabsf(s.x) + fabsf(s.y) < 0.08f) s.x = 0.12f;
  s.z = random(650, 1001) / 1000.0f;
}
}

void drawStarfieldOverdrive(const float* levels, const uint8_t* rawBands,
                           uint32_t packetSerial, float dt, bool reset) {
  float bass = 0, mids = 0;
  for (int i = 0; i < 8; i++) bass += levels[i] / 8.0f;
  for (int i = 8; i < 24; i++) mids += levels[i] / 16.0f;
  if (reset) {
    for (auto& s : stars) {
      spawn(s);
      s.z = random(100, 1001) / 1000.0f;
    }
    for (int i = 0; i < 8; i++) previousBass[i] = rawBands[i] / 255.0f;
    seenPacket = packetSerial;
    fluxAverage = packetAge = 0;
    drive = bass;
    boost = cooldown = phase = 0;
    for (int i = 0; i < 32; i++) trailBaseline[i] = levels[i];
  }
  cooldown = fmaxf(0.0f, cooldown - dt);
  boost *= expf(-dt * 12.0f); // Sharp attack, ~190ms to decay below 10%.
  packetAge += dt;
  if (packetSerial != seenPacket) {
    float flux = 0, rawBass = 0, oldBass = 0;
    for (int i = 0; i < 8; i++) {
      float value = rawBands[i] / 255.0f;
      flux += fmaxf(0.0f, value - previousBass[i]) / 8.0f;
      rawBass += value / 8.0f;
      oldBass += previousBass[i] / 8.0f;
      previousBass[i] = value;
    }
    // Compare packet-to-packet rises, not a smoothed amplitude threshold.
    // Each packet is evaluated once: held notes cannot retrigger a burst.
    float threshold = fmaxf(0.035f, fluxAverage * 1.8f);
    if (packetAge < 0.25f && rawBass > 0.12f && rawBass > oldBass + 0.025f &&
        flux > threshold && cooldown <= 0.0f) {
      boost = fminf(1.0f, 0.40f + (flux - threshold) * 4.0f);
      cooldown = 0.16f;
    }
    if (packetAge >= 0.25f) fluxAverage = 0; // Rebaseline after a stream gap.
    else fluxAverage += (flux - fluxAverage) * (1.0f - expf(-packetAge * 2.0f));
    packetAge = 0;
    seenPacket = packetSerial;
  }
  drive += (bass - drive) * (1.0f - expf(-dt * 6.0f));
  phase = fmodf(phase + dt * 0.22f, 6.2831853f);
  float cx = 63.5f + sinf(phase) * (2.0f + mids * 4.0f);
  float cy = (settings.vizShowClock ? 36.0f : 31.5f) + cosf(phase * 2) * 2.0f;
  float speed = 0.18f + drive * 0.65f + boost * 1.60f;
  float accents[32];
  const float trailSmooth = 1.0f - expf(-dt * 3.0f);
  for (int i = 0; i < 32; i++) {
    // Follow musical changes rather than adding an unrelated oscillation.
    trailBaseline[i] += (levels[i] - trailBaseline[i]) * trailSmooth;
    accents[i] = fminf(1.0f, fmaxf(0.0f, levels[i] - trailBaseline[i]) * 3.0f);
  }

  for (int i = 0; i < 96; i++) {
    MusicStar& s = stars[i];
    s.z -= speed * dt;
    if (s.z <= 0.06f) { spawn(s); continue; }
    float px = cx + s.x / s.z * 44.0f;
    float py = cy + s.y / s.z * 26.0f;
    if (px < 0 || px >= SCREEN_WIDTH || py < 0 || py >= SCREEN_HEIGHT) {
      spawn(s); continue;
    }
    float level = levels[i % 32];
    float depth = 1.0f - s.z;
    float accent = accents[i % 32];
    float light = fminf(1.0f, 0.16f + depth * 0.36f + level * 0.28f +
                             accent * 0.10f + boost * 0.15f);
    int r, g, b;
    if (i % 32 < 10) { r = 190; g = 110; b = 255; }
    else if (i % 32 < 24) { r = 100; g = 220; b = 255; }
    else { r = 255; g = 215; b = 145; }
    r = (int)(r * light); g = (int)(g * light); b = (int)(b * light);
    // Persistent 2-6px radial trails breathe with each star's frequency bin.
    // Only trail length changes: star positions retain the smooth flight.
    float dx = px-cx, dy = py-cy;
    float distance = sqrtf(dx*dx + dy*dy);
    float length = 2.0f + depth * 1.5f + level * 1.5f + accent + boost * 12.0f;
    length = fminf(length, distance * 0.8f); // Never cross the vanishing point.
    float fraction = distance > 0.001f ? length / distance : 0.0f;
    float tx = px - dx * fraction, ty = py - dy * fraction;
    int x = (int)px, y = (int)py;
    int mx = (int)((tx+px)*0.5f), my = (int)((ty+py)*0.5f);
    display.drawLine((int)tx, (int)ty, mx, my, rgb(r/4, g/4, b/4));
    display.drawLine(mx, my, x, y, rgb(r/2, g/2, b/2));
    // Stars approach white at the head; high frequencies add tiny glints.
    float white = 0.5f + boost * 0.4f;
    display.drawPixel(x, y, rgb(r + (int)((255-r)*white),
                              g + (int)((255-g)*white), b + (int)((255-b)*white)));
    if (depth > 0.60f && level > 0.70f) {
      display.drawPixel(x-1, y, rgb(r/3, g/3, b/3));
      display.drawPixel(x+1, y, rgb(r/3, g/3, b/3));
      display.drawPixel(x, y-1, rgb(r/3, g/3, b/3));
      display.drawPixel(x, y+1, rgb(r/3, g/3, b/3));
    }
  }
}
