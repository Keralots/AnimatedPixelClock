/*
 * AnimatedPixelClock - Dino Runner Clock (clockStyle 11)
 *
 * Chrome T-Rex homage. The dino runs in place near the bottom of the screen
 * while the world scrolls past: dashed ground, drifting parallax clouds and
 * the occasional cactus, which the dino hops over with a proper gravity arc.
 * Every so often a pterodactyl flaps across the midfield for flavour.
 *
 * At the top of each minute the pterodactyl turns courier: it swoops in from
 * the right at digit height, snatches the old digit (which hangs from its
 * claws as it flies off-screen left), and the new digit then drops in from
 * above, landing with a little dust puff. One changed digit at a time.
 *
 * The whole scene is 1-bit native - everything is procedural rects, lines
 * and triangles, no bitmaps. All state is file-local. resetDinoAnimation()
 * (called from resetClockAnimationState) returns everything to baseline.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "clocks.h"
#include "clock_globals.h"

// ========== Layout / tuning ==========
#define DINO_TIME_Y_TOP 16       // digit top when the date row is shown
#define DINO_TIME_Y_CENTER 21    // digit top when centred (date off)
#define DINO_TRIGGER_SECOND 56
#define DINO_DIGIT_W 16
#define DINO_GROUND_Y 57         // ground line; feet sit on it
#define DINO_X 12                // dino's fixed screen position
#define DINO_MAX_CACTI 3
#define DINO_MAX_CLOUDS 2
#define DINO_MAX_DUST 6
#define DINO_PTERO_SPEED 60.0f   // courier flight speed, px/s
#define DINO_DROP_START -30.0f   // new digit starts this far above its slot
#define DINO_PHASE_TIMEOUT 6.0f  // per-phase failsafe, seconds

enum DinoPhase { DINO_IDLE, DINO_PTERO_ENTER, DINO_PTERO_CARRY, DINO_DIGIT_DROP };

struct DinoCactus {
  bool active;
  float x;
  bool tall;                     // small (4x8) or tall (5x12) variant
};

struct DinoCloud {
  float x;
  int y;
};

struct DinoDust {
  bool active;
  float x, y, vx, vy;
  float life;
};

static DinoPhase dino_phase = DINO_IDLE;

// Dino body
static float dino_jump_y = 0.0f;     // 0 = on ground, negative = airborne
static float dino_jump_vy = 0.0f;
static bool dino_airborne = false;
static int dino_leg_frame = 0;
static unsigned long last_leg_toggle = 0;

// World
static DinoCactus dino_cacti[DINO_MAX_CACTI];
static DinoCloud dino_clouds[DINO_MAX_CLOUDS];
static DinoDust dino_dust[DINO_MAX_DUST];
static float dino_cactus_timer = 3.0f;   // until the next cactus spawns
static float dino_ground_phase = 0.0f;   // scroll offset for ground dashes

// Pterodactyl (idle flybys + the minute-change courier)
static bool ptero_active = false;
static float ptero_x = 0, ptero_y = 0;
static int ptero_wing_frame = 0;
static unsigned long last_wing_toggle = 0;
static float ptero_idle_timer = 12.0f;   // until the next idle flyby

// Minute-change bookkeeping
static int dino_change_idx[4];
static uint8_t dino_change_val[4];
static int dino_num_changes = 0;
static int dino_cur_change = 0;
static float dino_phase_timer = 0.0f;
static int last_minute_dino = -1;
static bool dino_triggered = false;

static unsigned long last_dino_update = 0;
static bool dino_init_done = false;

// ========== Helpers ==========
static int dinoTimeY() {
  return settings.dinoShowDate ? DINO_TIME_Y_TOP : DINO_TIME_Y_CENTER;
}

static float dinoRandf(float lo, float hi) {
  return lo + (hi - lo) * (random(0, 1001) / 1000.0f);
}

static float dinoScrollSpeed() {
  return 30.0f * (settings.dinoSpeed / 10.0f);  // px/s, default 36
}

static void dinoCactusGap(float &lo, float &hi) {
  switch (settings.dinoCactusFreq) {
    case 0:  lo = 8.0f;  hi = 16.0f; break;  // rare
    case 2:  lo = 3.0f;  hi = 6.0f;  break;  // frequent
    default: lo = 5.0f;  hi = 10.0f; break;  // normal
  }
}

static void dinoSpawnDust(float x, float y) {
  int spawned = 0;
  for (int i = 0; i < DINO_MAX_DUST && spawned < 4; i++) {
    if (dino_dust[i].active) continue;
    dino_dust[i].active = true;
    dino_dust[i].x = x + dinoRandf(-2, 2);
    dino_dust[i].y = y;
    dino_dust[i].vx = dinoRandf(-18, 18);
    dino_dust[i].vy = dinoRandf(-22, -8);
    dino_dust[i].life = dinoRandf(0.25f, 0.45f);
    spawned++;
  }
}

// ========== Reset ==========
void resetDinoAnimation() {
  dino_phase = DINO_IDLE;
  dino_jump_y = 0.0f;
  dino_jump_vy = 0.0f;
  dino_airborne = false;
  dino_leg_frame = 0;
  for (int i = 0; i < DINO_MAX_CACTI; i++) dino_cacti[i].active = false;
  for (int i = 0; i < DINO_MAX_DUST; i++) dino_dust[i].active = false;
  dino_clouds[0].x = 30;  dino_clouds[0].y = 6;
  dino_clouds[1].x = 95;  dino_clouds[1].y = 11;
  dino_cactus_timer = dinoRandf(2.0f, 5.0f);
  dino_ground_phase = 0.0f;
  ptero_active = false;
  ptero_idle_timer = dinoRandf(8.0f, 18.0f);
  dino_num_changes = 0;
  dino_cur_change = 0;
  dino_triggered = false;
  last_minute_dino = -1;
  last_dino_update = 0;
  dino_init_done = true;
}

// ========== Update ==========
static void dinoRevealAndAdvance() {
  // The drop already wrote the new value; move on to the next change.
  dino_cur_change++;
  if (dino_cur_change < dino_num_changes) {
    dino_phase = DINO_PTERO_ENTER;
    dino_phase_timer = 0.0f;
    ptero_active = true;
    ptero_x = SCREEN_WIDTH + 14;
    ptero_y = dinoTimeY() + 4;
  } else {
    dino_phase = DINO_IDLE;
    ptero_idle_timer = dinoRandf(8.0f, 18.0f);
  }
}

static void updateDinoAnimation(struct tm *timeinfo) {
  unsigned long now = millis();
  updateDigitBounce();

  float dt = (now - last_dino_update) / 1000.0f;
  if (dt > 0.1f || last_dino_update == 0) dt = 0.025f;
  last_dino_update = now;

  float scroll = dinoScrollSpeed();

  // ----- Minute-change trigger (same scheme as Snake/Asteroids) -----
  int seconds = timeinfo->tm_sec;
  int minute = timeinfo->tm_min;
  if (minute != last_minute_dino) {
    last_minute_dino = minute;
    dino_triggered = false;
  }
  if (seconds >= DINO_TRIGGER_SECOND && !dino_triggered &&
      dino_phase == DINO_IDLE) {
    dino_triggered = true;
    time_overridden = true;
    time_override_start = millis();
    calculateTargetDigits(displayed_hour, displayed_min, displayed_is_pm);

    dino_num_changes = 0;
    for (int i = 0; i < num_targets; i++) {
      if (target_digit_index[i] != 2) {  // skip the colon
        dino_change_idx[dino_num_changes] = target_digit_index[i];
        dino_change_val[dino_num_changes] = target_digit_values[i];
        dino_num_changes++;
      }
    }
    dino_cur_change = 0;
    if (dino_num_changes > 0) {
      dino_phase = DINO_PTERO_ENTER;
      dino_phase_timer = 0.0f;
      ptero_active = true;            // repurpose any idle flyby as courier
      ptero_x = SCREEN_WIDTH + 14;
      ptero_y = dinoTimeY() + 4;
    } else {
      time_overridden = false;        // only the colon changed
    }
  }

  // ----- Courier phases -----
  if (dino_phase != DINO_IDLE) {
    dino_phase_timer += dt;
    int didx = dino_change_idx[dino_cur_change];

    if (dino_phase == DINO_PTERO_ENTER) {
      ptero_x -= DINO_PTERO_SPEED * dt;
      ptero_y = dinoTimeY() + 4;
      if (ptero_x <= DIGIT_X[didx] + DINO_DIGIT_W / 2 ||
          dino_phase_timer > DINO_PHASE_TIMEOUT) {
        dino_phase = DINO_PTERO_CARRY;  // claws close on the old digit
        dino_phase_timer = 0.0f;
      }
    } else if (dino_phase == DINO_PTERO_CARRY) {
      ptero_x -= DINO_PTERO_SPEED * dt;
      if (ptero_x < -24 || dino_phase_timer > DINO_PHASE_TIMEOUT) {
        ptero_active = false;
        // Swap in the new value and let it fall from above the screen.
        updateDisplayedTimeDigit(didx, dino_change_val[dino_cur_change]);
        digit_offset_y[didx] = DINO_DROP_START;
        digit_velocity[didx] = 0.0f;
        dino_phase = DINO_DIGIT_DROP;
        dino_phase_timer = 0.0f;
      }
    } else if (dino_phase == DINO_DIGIT_DROP) {
      // updateDigitBounce() pulls the offset down to 0 with gravity.
      if (digit_offset_y[didx] >= 0.0f ||
          dino_phase_timer > DINO_PHASE_TIMEOUT) {
        digit_offset_y[didx] = 0.0f;
        digit_velocity[didx] = 0.0f;
        dinoSpawnDust(DIGIT_X[didx] + DINO_DIGIT_W / 2,
                      dinoTimeY() + 21);
        dinoRevealAndAdvance();
      }
    }
  } else {
    // Idle pterodactyl flyby across the midfield (right to left)
    if (ptero_active) {
      ptero_x -= 35.0f * dt;
      if (ptero_x < -16) ptero_active = false;
    } else {
      ptero_idle_timer -= dt;
      if (ptero_idle_timer <= 0) {
        ptero_idle_timer = dinoRandf(12.0f, 28.0f);
        ptero_active = true;
        ptero_x = SCREEN_WIDTH + 14;
        ptero_y = dinoRandf(42, 48);
      }
    }
  }

  // ----- World scroll: ground, cacti, clouds -----
  dino_ground_phase += scroll * dt;
  while (dino_ground_phase >= 16.0f) dino_ground_phase -= 16.0f;

  for (int i = 0; i < DINO_MAX_CACTI; i++) {
    if (!dino_cacti[i].active) continue;
    dino_cacti[i].x -= scroll * dt;
    if (dino_cacti[i].x < -8) dino_cacti[i].active = false;
  }
  dino_cactus_timer -= dt;
  if (dino_cactus_timer <= 0) {
    float lo, hi;
    dinoCactusGap(lo, hi);
    dino_cactus_timer = dinoRandf(lo, hi);
    for (int i = 0; i < DINO_MAX_CACTI; i++) {
      if (dino_cacti[i].active) continue;
      dino_cacti[i].active = true;
      dino_cacti[i].x = SCREEN_WIDTH + 6;
      dino_cacti[i].tall = random(0, 3) == 0;  // 1 in 3 tall
      break;
    }
  }

  for (int i = 0; i < DINO_MAX_CLOUDS; i++) {
    dino_clouds[i].x -= (scroll * 0.18f) * dt;  // parallax: clouds far away
    if (dino_clouds[i].x < -16) {
      dino_clouds[i].x = SCREEN_WIDTH + dinoRandf(4, 30);
      dino_clouds[i].y = (int)dinoRandf(3, 13);
    }
  }

  // ----- Dino: auto-jump approaching cacti -----
  if (!dino_airborne) {
    for (int i = 0; i < DINO_MAX_CACTI; i++) {
      if (!dino_cacti[i].active) continue;
      float dist = dino_cacti[i].x - DINO_X;
      // Jump when the cactus is roughly half a jump away at current speed
      if (dist > 0 && dist < scroll * 0.45f) {
        dino_airborne = true;
        dino_jump_vy = -52.0f;             // px/s up
        dinoSpawnDust(DINO_X + 2, DINO_GROUND_Y);
        break;
      }
    }
  } else {
    dino_jump_vy += 160.0f * dt;           // gravity px/s^2
    dino_jump_y += dino_jump_vy * dt;
    if (dino_jump_y >= 0.0f) {
      dino_jump_y = 0.0f;
      dino_jump_vy = 0.0f;
      dino_airborne = false;
      dinoSpawnDust(DINO_X + 2, DINO_GROUND_Y);
    }
  }

  // Leg + wing animation clocks (run off wall time, scale with speed)
  unsigned long legMs = (unsigned long)(140.0f / (settings.dinoSpeed / 10.0f));
  if (now - last_leg_toggle > legMs) {
    last_leg_toggle = now;
    dino_leg_frame ^= 1;
  }
  if (now - last_wing_toggle > 160) {
    last_wing_toggle = now;
    ptero_wing_frame ^= 1;
  }

  // ----- Dust -----
  for (int i = 0; i < DINO_MAX_DUST; i++) {
    if (!dino_dust[i].active) continue;
    dino_dust[i].x += dino_dust[i].vx * dt;
    dino_dust[i].y += dino_dust[i].vy * dt;
    dino_dust[i].vy += 90.0f * dt;
    dino_dust[i].life -= dt;
    if (dino_dust[i].life <= 0) dino_dust[i].active = false;
  }
}

// ========== Drawing ==========
// ~12x12 procedural T-Rex, feet at (x, groundY), facing right.
static void drawDino(int x, int groundY) {
  int top = groundY - 12 + (int)dino_jump_y;
  uint16_t col = SPRITE_COLOR(COL_DINO);

  // Head (with a bite of jaw) and eye
  display.fillRect(x + 6, top, 6, 4, col);
  display.drawPixel(x + 8, top + 1, DISPLAY_BLACK);     // eye
  display.fillRect(x + 6, top + 3, 4, 1, col);

  // Neck + body
  display.fillRect(x + 4, top + 2, 4, 6, col);
  display.fillRect(x + 1, top + 4, 6, 5, col);

  // Tail kicked up to the left
  display.fillRect(x - 1, top + 3, 2, 3, col);

  // Tiny forearm
  display.drawPixel(x + 7, top + 5, col);

  // Legs: alternate while running, both tucked when airborne
  if (dino_airborne) {
    display.fillRect(x + 2, top + 9, 2, 2, col);
    display.fillRect(x + 5, top + 9, 2, 2, col);
  } else if (dino_leg_frame == 0) {
    display.fillRect(x + 2, top + 9, 2, 3, col);
    display.fillRect(x + 5, top + 9, 2, 2, col);
  } else {
    display.fillRect(x + 2, top + 9, 2, 2, col);
    display.fillRect(x + 5, top + 9, 2, 3, col);
  }
}

static void drawCactus(const DinoCactus &c) {
  int x = (int)c.x;
  uint16_t col = SPRITE_COLOR(COL_DINO_CACTUS);
  if (c.tall) {
    display.fillRect(x + 2, DINO_GROUND_Y - 12, 2, 12, col);
    display.fillRect(x, DINO_GROUND_Y - 9, 2, 4, col);      // left arm
    display.drawPixel(x + 1, DINO_GROUND_Y - 10, col);
    display.fillRect(x + 4, DINO_GROUND_Y - 7, 2, 3, col);  // right arm
  } else {
    display.fillRect(x + 1, DINO_GROUND_Y - 8, 2, 8, col);
    display.drawPixel(x, DINO_GROUND_Y - 6, col);
    display.drawPixel(x + 3, DINO_GROUND_Y - 5, col);
  }
}

static void drawPtero(int x, int y) {
  uint16_t col = SPRITE_COLOR(COL_DINO_PTERO);
  // Body + beak
  display.drawLine(x - 4, y, x + 5, y, col);
  display.drawPixel(x + 6, y - 1, col);
  // Wings flap between up and down strokes
  if (ptero_wing_frame == 0) {
    display.drawLine(x, y, x - 3, y - 4, col);
    display.drawLine(x, y, x + 2, y - 3, col);
  } else {
    display.drawLine(x, y, x - 3, y + 3, col);
    display.drawLine(x, y, x + 2, y + 2, col);
  }
}

static void drawCloud(const DinoCloud &c) {
  int x = (int)c.x;
  uint16_t col = SPRITE_COLOR(COL_DINO_CLOUD);
  display.drawFastHLine(x + 3, c.y, 7, col);
  display.drawFastHLine(x + 1, c.y + 1, 12, col);
  display.drawFastHLine(x + 4, c.y + 2, 6, col);
}

void displayClockWithDino() {
  if (!dino_init_done) resetDinoAnimation();

  struct tm timeinfo;
  if (!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    display.print(ntpSynced ? "Time Error" : "Syncing time...");
    return;
  }

  updateDinoAnimation(&timeinfo);

  if (!time_overridden) syncDisplayedTime(&timeinfo);
  maintainTimeOverride(&timeinfo, dino_phase == DINO_IDLE);

  int gy = dinoTimeY();

  // Background: clouds drift behind everything
  if (settings.dinoShowClouds) {
    for (int i = 0; i < DINO_MAX_CLOUDS; i++) drawCloud(dino_clouds[i]);
  }

  // Ground: solid line plus scrolling dash marks below it
  uint16_t groundCol = SPRITE_COLOR(COL_DINO_GROUND);
  display.drawFastHLine(0, DINO_GROUND_Y, SCREEN_WIDTH, groundCol);
  for (int gx = -(int)dino_ground_phase; gx < SCREEN_WIDTH; gx += 16) {
    display.drawFastHLine(gx + 4, DINO_GROUND_Y + 3, 4, groundCol);
    display.drawFastHLine(gx + 11, DINO_GROUND_Y + 2, 2, groundCol);
  }

  for (int i = 0; i < DINO_MAX_CACTI; i++) {
    if (dino_cacti[i].active) drawCactus(dino_cacti[i]);
  }
  drawDino(DINO_X, DINO_GROUND_Y);

  for (int i = 0; i < DINO_MAX_DUST; i++) {
    if (dino_dust[i].active) {
      display.drawPixel((int)dino_dust[i].x, (int)dino_dust[i].y, groundCol);
    }
  }

  // Time digits (size 3). The digit being carried away is drawn hanging from
  // the pterodactyl instead of in its slot; the slot stays empty until the
  // new digit drops in from above.
  display.setTextSize(3);
  display.setTextColor(digitColor());  // kept through the carried ptero digit
  char dch[5];
  dch[0] = '0' + displayed_hour / 10;
  dch[1] = '0' + displayed_hour % 10;
  dch[2] = shouldShowColon() ? ':' : ' ';
  dch[3] = '0' + displayed_min / 10;
  dch[4] = '0' + displayed_min % 10;

  // Only while actually carried; during the approach it stays in its slot
  int carryIdx = -1;
  if (dino_phase == DINO_PTERO_CARRY) carryIdx = dino_change_idx[dino_cur_change];

  for (int i = 0; i < 5; i++) {
    if (i == carryIdx) continue;
    int dy = gy + ((i == 2) ? 0 : (int)digit_offset_y[i]);
    display.setCursor(DIGIT_X[i], dy);
    display.print(dch[i]);
  }

  // Pterodactyl (idle flyby or courier), with the snatched digit in its claws
  if (ptero_active) {
    drawPtero((int)ptero_x, (int)ptero_y);
    if (dino_phase == DINO_PTERO_CARRY && carryIdx >= 0) {
      display.setTextSize(3);
      display.setCursor((int)ptero_x - 6, (int)ptero_y + 4);
      display.print(dch[carryIdx]);
    }
  }
  display.setTextColor(DISPLAY_WHITE);  // restore for date/meridiem chrome

  // Optional date row (top)
  if (settings.dinoShowDate) {
    display.setTextSize(1);
    char dateStr[12];
    switch (settings.dateFormat) {
      case 0: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
      case 1: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900); break;
      case 2: sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday); break;
      case 3: sprintf(dateStr, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
    }
    display.setCursor((SCREEN_WIDTH - 60) / 2, 4);
    display.print(dateStr);
  }
  drawMeridiemIndicator(110, 4, displayed_is_pm);

  if (!wifiConnected) drawNoWiFiIcon(0, 0);
}
