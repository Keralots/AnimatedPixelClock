/*
 * AnimatedPixelClock - Seasonal Holiday Overlays
 *
 * Date-driven effects drawn over the active clock style (never over PC
 * stats): snow through December, fireworks around New Year's midnight,
 * hearts on Feb 14, pumpkin + bats in late October. One master toggle.
 * The date window is re-checked every 30s, not per frame.
 */

#include "ambient.h"

#include "../clocks/clocks.h"
#include "../config/config.h"
#include "../display/display.h"

enum SeasonKind : uint8_t {
  SEASON_NONE = 0,
  SEASON_SNOW,
  SEASON_FIREWORKS,
  SEASON_HEARTS,
  SEASON_HALLOWEEN,
};

static SeasonKind cachedKind = SEASON_NONE;
static unsigned long lastSeasonCheck = 0;
static bool seasonChecked = false;

static SeasonKind computeSeason() {
  struct tm t;
  if (!getTimeWithTimeout(&t, 10)) return SEASON_NONE;

  // Fireworks window wins over December snow.
  if ((t.tm_mon == 11 && t.tm_mday == 31 && t.tm_hour == 23 && t.tm_min >= 50) ||
      (t.tm_mon == 0 && t.tm_mday == 1 && t.tm_hour == 0 && t.tm_min < 10)) {
    return SEASON_FIREWORKS;
  }
  if (t.tm_mon == 11) return SEASON_SNOW;
  if (t.tm_mon == 1 && t.tm_mday == 14) return SEASON_HEARTS;
  if (t.tm_mon == 9 && t.tm_mday >= 25) return SEASON_HALLOWEEN;
  return SEASON_NONE;
}

static SeasonKind currentSeason() {
  if (!settings.holidayOverlays) return SEASON_NONE;
  unsigned long now = millis();
  if (!seasonChecked || now - lastSeasonCheck > 30000) {
    cachedKind = computeSeason();
    lastSeasonCheck = now;
    seasonChecked = true;
  }
  return cachedKind;
}

bool seasonalOverlayActive() { return currentSeason() != SEASON_NONE; }

// ---------- Snow ----------
#define SNOW_FLAKES 36

struct Flake {
  float x, y;
  float vy;     // px/s
  float phase;  // sway phase
};

static Flake flakes[SNOW_FLAKES];
static bool snowInit = false;

static void drawSnow(float dt, float t) {
  if (!snowInit) {
    for (int i = 0; i < SNOW_FLAKES; i++) {
      flakes[i].x = random(0, SCREEN_WIDTH);
      flakes[i].y = random(0, SCREEN_HEIGHT);
      flakes[i].vy = random(8, 22);
      flakes[i].phase = random(0, 628) / 100.0f;
    }
    snowInit = true;
  }
  for (int i = 0; i < SNOW_FLAKES; i++) {
    Flake& f = flakes[i];
    f.y += f.vy * dt;
    if (f.y >= SCREEN_HEIGHT) {
      f.y = 0;
      f.x = random(0, SCREEN_WIDTH);
    }
    int x = (int)(f.x + 3.0f * sinf(t * 0.8f + f.phase));
    display.drawPixel((x + SCREEN_WIDTH) % SCREEN_WIDTH, (int)f.y,
                      DISPLAY_WHITE);
  }
}

// ---------- Fireworks ----------
#define FW_BURSTS 3
#define FW_PARTICLES 20

struct Burst {
  bool active;
  float px[FW_PARTICLES], py[FW_PARTICLES];
  float vx[FW_PARTICLES], vy[FW_PARTICLES];
  float life;  // seconds remaining
  uint16_t color;
};

static Burst bursts[FW_BURSTS];
static unsigned long nextBurstAt = 0;

static const uint16_t FW_COLORS[] = {0xF800, 0x07E0, 0x07FF,
                                     0xFFE0, 0xF81F, 0xFFFF};

static void drawFireworks(float dt) {
  unsigned long now = millis();
  if (now >= nextBurstAt) {
    for (int b = 0; b < FW_BURSTS; b++) {
      if (bursts[b].active) continue;
      Burst& bu = bursts[b];
      float cx = random(20, SCREEN_WIDTH - 20);
      float cy = random(8, SCREEN_HEIGHT / 2);
      bu.color = FW_COLORS[random(0, 6)];
      for (int i = 0; i < FW_PARTICLES; i++) {
        float a = i * (2.0f * PI / FW_PARTICLES);
        float v = random(14, 30);
        bu.px[i] = cx;
        bu.py[i] = cy;
        bu.vx[i] = cosf(a) * v;
        bu.vy[i] = sinf(a) * v;
      }
      bu.life = 1.3f;
      bu.active = true;
      break;
    }
    nextBurstAt = now + random(500, 1400);
  }

  for (int b = 0; b < FW_BURSTS; b++) {
    Burst& bu = bursts[b];
    if (!bu.active) continue;
    bu.life -= dt;
    if (bu.life <= 0) {
      bu.active = false;
      continue;
    }
    for (int i = 0; i < FW_PARTICLES; i++) {
      bu.px[i] += bu.vx[i] * dt;
      bu.py[i] += bu.vy[i] * dt;
      bu.vy[i] += 22.0f * dt;  // gravity
      int x = (int)bu.px[i], y = (int)bu.py[i];
      if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) continue;
      // Sparkle fade: particles blink out as the burst dies.
      if (bu.life < 0.5f && ((i + (int)(bu.life * 20)) % 3) == 0) continue;
      display.drawPixel(x, y, bu.color);
    }
  }
}

// ---------- Hearts ----------
#define HEART_COUNT 7

struct Heart {
  float x, y;
  float vy;
  float phase;
};

static Heart hearts[HEART_COUNT];
static bool heartsInit = false;

static const uint8_t HEART_ROWS[5] = {0x0A, 0x1F, 0x1F, 0x0E, 0x04};

static void drawHearts(float dt, float t) {
  if (!heartsInit) {
    for (int i = 0; i < HEART_COUNT; i++) {
      hearts[i].x = random(4, SCREEN_WIDTH - 8);
      hearts[i].y = random(0, SCREEN_HEIGHT);
      hearts[i].vy = random(5, 12);
      hearts[i].phase = random(0, 628) / 100.0f;
    }
    heartsInit = true;
  }
  for (int i = 0; i < HEART_COUNT; i++) {
    Heart& h = hearts[i];
    h.y -= h.vy * dt;  // hearts float up
    if (h.y < -5) {
      h.y = SCREEN_HEIGHT;
      h.x = random(4, SCREEN_WIDTH - 8);
    }
    int hx = (int)(h.x + 2.5f * sinf(t + h.phase));
    for (int r = 0; r < 5; r++) {
      for (int c = 0; c < 5; c++) {
        if (HEART_ROWS[r] & (0x10 >> c)) {
          display.drawPixel(hx + c, (int)h.y + r, 0xFB56);
        }
      }
    }
  }
}

// ---------- Halloween ----------
static float batX = -20;
static unsigned long batNextAt = 0;
static int batY = 14;
static bool batRight = true;

static void drawHalloween(float dt) {
  // Pumpkin in the bottom-left corner: body, stem, eyes and grin.
  int px = 3, py = 54;
  display.fillCircle(px + 3, py + 5, 4, 0xFC00);
  display.fillCircle(px + 7, py + 5, 4, 0xFC00);
  display.fillRect(px + 4, py - 1, 2, 2, 0x0560);  // stem
  display.drawPixel(px + 2, py + 4, DISPLAY_BLACK);
  display.drawPixel(px + 8, py + 4, DISPLAY_BLACK);
  display.drawFastHLine(px + 2, py + 7, 7, DISPLAY_BLACK);
  display.drawPixel(px + 3, py + 8, DISPLAY_BLACK);
  display.drawPixel(px + 7, py + 8, DISPLAY_BLACK);

  // A bat crosses the sky every so often.
  unsigned long now = millis();
  if (now >= batNextAt && (batX < -16 || batX > SCREEN_WIDTH + 16)) {
    batRight = random(0, 2) == 1;
    batX = batRight ? -10 : SCREEN_WIDTH + 10;
    batY = random(6, 28);
    batNextAt = now + random(6000, 14000);
  }
  if (batX >= -16 && batX <= SCREEN_WIDTH + 16) {
    batX += (batRight ? 34.0f : -34.0f) * dt;
    int bx = (int)batX;
    bool up = (now / 160) % 2 == 0;
    uint16_t c = 0x7BEF;  // gray, visible on black
    display.drawPixel(bx, batY, c);
    display.drawPixel(bx - 1, batY + (up ? -1 : 1), c);
    display.drawPixel(bx + 1, batY + (up ? -1 : 1), c);
    display.drawPixel(bx - 2, batY + (up ? -2 : 1), c);
    display.drawPixel(bx + 2, batY + (up ? -2 : 1), c);
  }
}

// ---------- Dispatcher ----------
void drawSeasonalOverlay() {
  SeasonKind kind = currentSeason();
  if (kind == SEASON_NONE) return;

  static unsigned long lastFrame = 0;
  unsigned long now = millis();
  float dt = (now - lastFrame) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  lastFrame = now;
  float t = now / 1000.0f;

  switch (kind) {
    case SEASON_SNOW: drawSnow(dt, t); break;
    case SEASON_FIREWORKS: drawFireworks(dt); break;
    case SEASON_HEARTS: drawHearts(dt, t); break;
    case SEASON_HALLOWEEN: drawHalloween(dt); break;
    default: break;
  }
}
