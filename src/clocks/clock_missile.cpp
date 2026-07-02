/*
 * AnimatedPixelClock - Missile Command Clock (clockStyle 13)
 *
 * A Missile Command homage. Enemy missiles fall from the top of the screen
 * as dotted trails aimed at the ground between a row of city silhouettes;
 * a central cannon answers with counter-missiles and expanding, flickering
 * explosion rings knock the attackers out of the sky. The cities are never
 * hit - the defense always holds, and stray missiles only ever strike the
 * gaps between them.
 *
 * At the top of each minute an enemy missile dives onto each changed digit
 * and detonates: the ring grows until it covers the digit, the value swaps
 * underneath at peak radius, and the new digit is revealed as the ring
 * shrinks away. Change missiles are never intercepted - they are supposed
 * to hit.
 *
 * Everything is dt-based (no fixed tick, nothing beats against the 16 ms
 * render frame grid). All state is file-local. resetMissileAnimation()
 * (called from resetClockAnimationState) returns everything to baseline.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "clocks.h"
#include "clock_globals.h"

#include <math.h>

// ========== Layout / tuning ==========
#define MC_TIME_Y_TOP 16         // digit top when the date row is shown
#define MC_TIME_Y_CENTER 21      // digit top when centred (date off)
#define MC_TRIGGER_SECOND 56
#define MC_DIGIT_W 16            // size-3 digit box width
#define MC_DIGIT_H 21            // size-3 digit box height
#define MC_GROUND_Y 62           // top of the 2px ground strip
#define MC_CANNON_X 64
#define MC_MUZZLE_Y 52           // counter-missiles launch from the barrel tip
#define MC_MAX_ENEMIES 6
#define MC_MAX_COUNTERS 3
#define MC_MAX_BOOMS 6
#define MC_ENEMY_BASE_SPEED 9.0f   // px/s at speed setting 1.0
#define MC_COUNTER_SPEED_MULT 3.0f // counter speed = enemy base speed x this
#define MC_CHANGE_SPEED 60.0f      // px/s for digit-change missiles
#define MC_DEFENSE_Y 34.0f         // enemy crossing this line draws counter fire
#define MC_BOOM_RATE 24.0f         // ring grow/shrink speed, px/s
#define MC_BOOM_HOLD 0.25f         // seconds at full radius
#define MC_BOOM_R_IDLE 9
#define MC_BOOM_R_CHANGE 14        // covers the 16x21 digit box

struct McMissile {
  bool active;
  bool isChange;      // digit-change missile (never intercepted)
  int8_t slot;        // digit slot for change missiles, -1 otherwise
  uint8_t newVal;     // value the digit swaps to at ring peak
  bool counterFired;  // idle missiles: counter already launched at this one
  float launchDelay;  // seconds until it actually starts falling
  float x0, y0;       // trail origin
  float x, y;         // head
  float vx, vy;
  float tx, ty;       // impact point
};

struct McCounter {
  bool active;
  float x, y;
  float vx, vy;
  float tx, ty;
};

struct McBoom {
  bool active;
  bool isChange;
  bool swapped;       // change booms: digit already swapped at peak
  int8_t slot;
  uint8_t newVal;
  uint8_t phase;      // 0=grow 1=hold 2=shrink
  float r, maxR, holdT;
  float x, y;
};

// City x-centres (3 per side of the cannon) and the ground gaps between
// them that idle missiles are allowed to strike.
static const int MC_CITY_X[6] = {10, 28, 46, 82, 100, 118};
static const int MC_GAP_X[6] = {19, 37, 55, 73, 91, 109};

static McMissile mc_enemies[MC_MAX_ENEMIES];
static McCounter mc_counters[MC_MAX_COUNTERS];
static McBoom mc_booms[MC_MAX_BOOMS];
static float mc_volley_timer = 0.0f;

// Minute-change bookkeeping
static int last_minute_mc = -1;
static bool mc_triggered = false;

static unsigned long last_mc_update = 0;
static bool mc_init_done = false;

// ========== Helpers ==========
static float mcRandf(float lo, float hi) {
  return lo + (hi - lo) * (random(0, 1001) / 1000.0f);
}

static int mcTimeY() {
  return settings.mcShowDate ? MC_TIME_Y_TOP : MC_TIME_Y_CENTER;
}

static float mcEnemySpeed() {
  int v = constrain((int)settings.mcMissileSpeed, 5, 30);
  return MC_ENEMY_BASE_SPEED * (v / 10.0f);
}

static float mcVolleyDelay() {
  switch (settings.mcMissileFreq) {
    case 0: return mcRandf(12.0f, 20.0f);  // Rare
    case 2: return mcRandf(2.0f, 6.0f);    // Frequent
    default: return mcRandf(6.0f, 12.0f);  // Normal
  }
}

static bool mcChangeActive() {
  for (int i = 0; i < MC_MAX_ENEMIES; i++) {
    if (mc_enemies[i].active && mc_enemies[i].isChange) return true;
  }
  for (int i = 0; i < MC_MAX_BOOMS; i++) {
    if (mc_booms[i].active && mc_booms[i].isChange) return true;
  }
  return false;
}

// Free enemy slot; change missiles may evict an idle attacker (trail simply
// vanishes) so a minute change is never starved by a busy sky.
static int mcFindEnemySlot(bool forChange) {
  for (int i = 0; i < MC_MAX_ENEMIES; i++) {
    if (!mc_enemies[i].active) return i;
  }
  if (forChange) {
    for (int i = 0; i < MC_MAX_ENEMIES; i++) {
      if (!mc_enemies[i].isChange) return i;
    }
  }
  return -1;
}

static void mcSpawnBoom(float x, float y, float maxR, bool isChange,
                        int8_t slot, uint8_t newVal) {
  for (int i = 0; i < MC_MAX_BOOMS; i++) {
    if (mc_booms[i].active) continue;
    mc_booms[i].active = true;
    mc_booms[i].isChange = isChange;
    mc_booms[i].swapped = false;
    mc_booms[i].slot = slot;
    mc_booms[i].newVal = newVal;
    mc_booms[i].phase = 0;
    mc_booms[i].r = 0.0f;
    mc_booms[i].maxR = maxR;
    mc_booms[i].holdT = MC_BOOM_HOLD;
    mc_booms[i].x = x;
    mc_booms[i].y = y;
    return;
  }
}

static void mcLaunchEnemy(int idx, float x0, float tx, float ty, float speed,
                          float delay, bool isChange, int8_t slot,
                          uint8_t newVal) {
  McMissile &m = mc_enemies[idx];
  m.active = true;
  m.isChange = isChange;
  m.slot = slot;
  m.newVal = newVal;
  m.counterFired = false;
  m.launchDelay = delay;
  m.x0 = m.x = x0;
  m.y0 = m.y = 0.0f;
  m.tx = tx;
  m.ty = ty;
  float dx = tx - x0, dy = ty - 0.0f;
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 1.0f) len = 1.0f;
  m.vx = dx / len * speed;
  m.vy = dy / len * speed;
}

// ========== Reset ==========
void resetMissileAnimation() {
  for (int i = 0; i < MC_MAX_ENEMIES; i++) mc_enemies[i].active = false;
  for (int i = 0; i < MC_MAX_COUNTERS; i++) mc_counters[i].active = false;
  for (int i = 0; i < MC_MAX_BOOMS; i++) mc_booms[i].active = false;
  mc_volley_timer = mcVolleyDelay() * 0.5f;  // first volley comes sooner
  last_minute_mc = -1;
  mc_triggered = false;
  last_mc_update = 0;
  mc_init_done = true;
}

// ========== Update ==========
static void mcFireCounter(const McMissile &enemy) {
  int slot = -1;
  for (int i = 0; i < MC_MAX_COUNTERS; i++) {
    if (!mc_counters[i].active) { slot = i; break; }
  }
  if (slot < 0) return;  // battery busy; the ground explosion catches it

  // Two-iteration intercept solve: project the enemy to where it will be
  // when the counter can reach it, then refine once.
  float cSpeed = mcEnemySpeed() * MC_COUNTER_SPEED_MULT;
  float tx = enemy.x, ty = enemy.y;
  for (int it = 0; it < 2; it++) {
    float dx = tx - MC_CANNON_X, dy = ty - MC_MUZZLE_Y;
    float t = sqrtf(dx * dx + dy * dy) / cSpeed;
    tx = enemy.x + enemy.vx * t;
    ty = enemy.y + enemy.vy * t;
  }
  if (ty > MC_GROUND_Y - 6) ty = MC_GROUND_Y - 6;  // detonate above ground

  McCounter &c = mc_counters[slot];
  c.active = true;
  c.x = MC_CANNON_X;
  c.y = MC_MUZZLE_Y;
  c.tx = tx;
  c.ty = ty;
  float dx = tx - c.x, dy = ty - c.y;
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 1.0f) len = 1.0f;
  c.vx = dx / len * cSpeed;
  c.vy = dy / len * cSpeed;
}

static void updateMissileAnimation(struct tm *timeinfo) {
  unsigned long now = millis();
  float dt = (now - last_mc_update) / 1000.0f;
  if (dt > 0.1f || last_mc_update == 0) dt = 0.016f;
  last_mc_update = now;

  int gy = mcTimeY();

  // ----- Minute-change trigger (same scheme as Snake/Asteroids/Matrix) -----
  int seconds = timeinfo->tm_sec;
  int minute = timeinfo->tm_min;
  if (minute != last_minute_mc) {
    last_minute_mc = minute;
    mc_triggered = false;
  }
  if (seconds >= MC_TRIGGER_SECOND && !mc_triggered && !mcChangeActive()) {
    mc_triggered = true;
    time_overridden = true;
    time_override_start = millis();
    calculateTargetDigits(displayed_hour, displayed_min, displayed_is_pm);

    int changes = 0;
    for (int i = 0; i < num_targets; i++) {
      int di = target_digit_index[i];
      if (di == 2) continue;  // skip the colon
      int idx = mcFindEnemySlot(true);
      if (idx < 0) break;     // cannot happen: 4 changes vs pool of 6
      float cx = DIGIT_X[di] + MC_DIGIT_W / 2.0f;
      mcLaunchEnemy(idx, cx + mcRandf(-8.0f, 8.0f), cx, gy + MC_DIGIT_H / 2.0f,
                    MC_CHANGE_SPEED, changes * 0.25f, true, (int8_t)di,
                    (uint8_t)target_digit_values[i]);
      changes++;
    }
    if (changes == 0) time_overridden = false;  // only the colon changed
  }

  // ----- Idle volleys -----
  if (!mcChangeActive()) {
    mc_volley_timer -= dt;
    if (mc_volley_timer <= 0.0f) {
      mc_volley_timer = mcVolleyDelay();
      int count = random(1, 4);  // 1-3 missiles
      for (int k = 0; k < count; k++) {
        int idx = mcFindEnemySlot(false);
        if (idx < 0) break;
        float tx = MC_GAP_X[random(0, 6)] + mcRandf(-3.0f, 3.0f);
        mcLaunchEnemy(idx, mcRandf(4.0f, 124.0f), tx, MC_GROUND_Y - 2.0f,
                      mcEnemySpeed() * mcRandf(0.8f, 1.3f), k * 0.4f, false,
                      -1, 0);
      }
    }
  }

  // ----- Enemy missiles -----
  for (int i = 0; i < MC_MAX_ENEMIES; i++) {
    McMissile &m = mc_enemies[i];
    if (!m.active) continue;
    if (m.launchDelay > 0.0f) {
      m.launchDelay -= dt;
      continue;
    }
    m.x += m.vx * dt;
    m.y += m.vy * dt;

    // Cannon answers idle attackers once they commit past the defense line
    if (!m.isChange && !m.counterFired && m.y >= MC_DEFENSE_Y) {
      m.counterFired = true;
      mcFireCounter(m);
    }

    // Impact: change missiles wipe their digit, idle ones crater a gap
    float ddx = m.x - m.tx, ddy = m.y - m.ty;
    if (ddx * ddx + ddy * ddy < 4.0f || m.y >= m.ty) {
      m.active = false;
      mcSpawnBoom(m.tx, m.ty, m.isChange ? MC_BOOM_R_CHANGE : MC_BOOM_R_IDLE,
                  m.isChange, m.slot, m.newVal);
    }
  }

  // ----- Counter-missiles -----
  for (int i = 0; i < MC_MAX_COUNTERS; i++) {
    McCounter &c = mc_counters[i];
    if (!c.active) continue;
    c.x += c.vx * dt;
    c.y += c.vy * dt;
    float ddx = c.x - c.tx, ddy = c.y - c.ty;
    if (ddx * ddx + ddy * ddy < 4.0f) {
      c.active = false;
      mcSpawnBoom(c.tx, c.ty, MC_BOOM_R_IDLE, false, -1, 0);
    }
  }

  // ----- Explosions -----
  for (int i = 0; i < MC_MAX_BOOMS; i++) {
    McBoom &b = mc_booms[i];
    if (!b.active) continue;
    if (b.phase == 0) {
      b.r += MC_BOOM_RATE * dt;
      if (b.r >= b.maxR) {
        b.r = b.maxR;
        b.phase = 1;
        // Peak radius covers the digit box: swap the value underneath
        if (b.isChange && !b.swapped) {
          b.swapped = true;
          updateDisplayedTimeDigit(b.slot, b.newVal);
        }
      }
    } else if (b.phase == 1) {
      b.holdT -= dt;
      if (b.holdT <= 0.0f) b.phase = 2;
    } else {
      b.r -= MC_BOOM_RATE * dt;
      if (b.r <= 0.0f) {
        b.active = false;
        continue;
      }
    }

    // The blast catches idle attackers flying through it
    for (int e = 0; e < MC_MAX_ENEMIES; e++) {
      McMissile &m = mc_enemies[e];
      if (!m.active || m.isChange || m.launchDelay > 0.0f) continue;
      float ddx = m.x - b.x, ddy = m.y - b.y;
      if (ddx * ddx + ddy * ddy < b.r * b.r) m.active = false;
    }
  }
}

// ========== Drawing ==========
// Dotted Bresenham-style trail from a missile's origin to its head.
static void mcDrawTrail(float x0, float y0, float x1, float y1, uint16_t col) {
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 1.0f) return;
  float sx = dx / len, sy = dy / len;
  for (int s = 0; s < (int)len; s += 2) {
    display.drawPixel((int)(x0 + sx * s), (int)(y0 + sy * s), col);
  }
}

static void mcDrawCity(int cx) {
  uint16_t col = SPRITE_COLOR(COL_MC_CITY);
  display.fillRect(cx - 4, 59, 9, 3, col);   // base block
  display.fillRect(cx - 4, 57, 2, 2, col);   // left tower
  display.fillRect(cx - 1, 56, 2, 3, col);   // centre tower
  display.fillRect(cx + 3, 58, 2, 1, col);   // right ledge
}

static void mcDrawBattlefield() {
  display.fillRect(0, MC_GROUND_Y, SCREEN_WIDTH, 2, SPRITE_COLOR(COL_MC_GROUND));
  for (int i = 0; i < 6; i++) mcDrawCity(MC_CITY_X[i]);
  // Cannon: ochre mound with a cyan barrel tip
  display.fillRect(MC_CANNON_X - 5, 58, 11, 4, SPRITE_COLOR(COL_MC_GROUND));
  display.fillRect(MC_CANNON_X - 3, 56, 7, 2, SPRITE_COLOR(COL_MC_GROUND));
  display.fillRect(MC_CANNON_X - 1, MC_MUZZLE_Y, 2, 4,
                   SPRITE_COLOR(COL_MC_COUNTER));
}

void displayClockWithMissileCommand() {
  if (!mc_init_done) resetMissileAnimation();

  struct tm timeinfo;
  if (!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    display.print(ntpSynced ? "Time Error" : "Syncing time...");
    return;
  }

  updateMissileAnimation(&timeinfo);

  if (!time_overridden) syncDisplayedTime(&timeinfo);
  maintainTimeOverride(&timeinfo, !mcChangeActive());

  int gy = mcTimeY();
  bool flick = (millis() / 60) % 2 == 0;

  mcDrawBattlefield();

  // Missile trails fly behind the digit plates
  for (int i = 0; i < MC_MAX_ENEMIES; i++) {
    const McMissile &m = mc_enemies[i];
    if (!m.active || m.launchDelay > 0.0f) continue;
    uint16_t col = SPRITE_COLOR(COL_MC_MISSILE);
    mcDrawTrail(m.x0, m.y0, m.x, m.y, col);
    display.drawPixel((int)m.x, (int)m.y, flick ? DISPLAY_WHITE : col);
  }
  for (int i = 0; i < MC_MAX_COUNTERS; i++) {
    const McCounter &c = mc_counters[i];
    if (!c.active) continue;
    uint16_t col = SPRITE_COLOR(COL_MC_COUNTER);
    mcDrawTrail(MC_CANNON_X, MC_MUZZLE_Y, c.x, c.y, col);
    display.drawPixel((int)c.x, (int)c.y, flick ? DISPLAY_WHITE : col);
  }

  // Time digits (size 3) on solid plates; the sky is a shooting gallery
  display.setTextSize(3);
  display.setTextColor(SPRITE_COLOR(COL_DIGITS));
  char dch[5];
  dch[0] = '0' + displayed_hour / 10;
  dch[1] = '0' + displayed_hour % 10;
  dch[2] = shouldShowColon() ? ':' : ' ';
  dch[3] = '0' + displayed_min / 10;
  dch[4] = '0' + displayed_min % 10;
  for (int i = 0; i < 5; i++) {
    int dx = DIGIT_X[i];
    display.fillRect(dx - 1, gy - 1, MC_DIGIT_W + 2, MC_DIGIT_H + 2,
                     DISPLAY_BLACK);
    display.setCursor(dx, gy);
    display.print(dch[i]);
  }
  display.setTextColor(DISPLAY_WHITE);  // restore for date chrome

  // Explosion rings burn over everything, including the digit they wipe
  uint16_t boomCol =
      ((millis() / 70) % 2 == 0) ? SPRITE_COLOR(COL_MC_EXPLOSION) : DISPLAY_WHITE;
  for (int i = 0; i < MC_MAX_BOOMS; i++) {
    const McBoom &b = mc_booms[i];
    if (!b.active || b.r < 1.0f) continue;
    display.fillCircle((int)b.x, (int)b.y, (int)b.r, boomCol);
  }

  // Optional date row (top), on its own plate
  if (settings.mcShowDate) {
    display.setTextSize(1);
    char dateStr[12];
    switch (settings.dateFormat) {
      case 0: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
      case 1: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900); break;
      case 2: sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday); break;
      case 3: sprintf(dateStr, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
    }
    int dateX = (SCREEN_WIDTH - 60) / 2;
    display.fillRect(dateX - 1, 3, 62, 9, DISPLAY_BLACK);
    display.setCursor(dateX, 4);
    display.print(dateStr);
  }
  if (!settings.use24Hour) {
    display.fillRect(109, 3, 14, 10, DISPLAY_BLACK);
    drawMeridiemIndicator(110, 4, displayed_is_pm);
  }

  if (!wifiConnected) drawNoWiFiIcon(0, 0);
}
