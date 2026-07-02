/*
 * AnimatedPixelClock - Matrix Rain Clock (clockStyle 12)
 *
 * Digital rain in the style of the classic film effect: columns of random
 * glyphs fall down the screen, each with a bright white-green head and a
 * tail that fades out behind it. Glyphs inside a visible trail mutate at
 * random, and columns respawn on their own rhythm, so the whole panel
 * shimmers continuously. The time floats over the rain as solid digits on
 * masked plates.
 *
 * At the top of each minute the changed digits "decode": the digit box
 * cycles bright random glyphs for a moment before locking onto the new
 * value, while the rain columns crossing that digit speed up as if the
 * change is being flushed down the screen.
 *
 * Everything is dt-based (no fixed tick, so nothing can beat against the
 * 16 ms render frame grid). All state is file-local.
 * resetMatrixRainAnimation() (called from resetClockAnimationState)
 * returns everything to a clean baseline.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "clocks.h"
#include "clock_globals.h"

// ========== Layout / tuning ==========
#define MX_COLS 21               // 6px char columns (126px + 1px margin each side)
#define MX_ROWS 8                // 8px char rows
#define MX_CELL_W 6
#define MX_CELL_H 8
#define MX_X_OFF 1
#define MX_TIME_Y_TOP 16         // digit top when the date row is shown
#define MX_TIME_Y_CENTER 21      // digit top when centred (date off)
#define MX_TRIGGER_SECOND 56
#define MX_DIGIT_W 16            // size-3 digit box width
#define MX_DIGIT_H 21            // size-3 digit box height
#define MX_DECODE_TIME 1.2f      // seconds a changed digit spends decoding
#define MX_DECODE_SWAP 0.08f     // seconds between decode glyph swaps
#define MX_MUTATE_RATE 1.2f      // avg glyph mutations per visible cell per second
#define MX_FADE_LEVELS 8

// Stand-in for the film's katakana: digits, caps and a few dense symbols
// from the built-in GFX 5x7 font.
static const char MX_CHARSET[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$+-*/=%#&<>@";

struct MxColumn {
  bool active;
  float headRow;    // fractional head row; glyphs snap to whole rows
  float speed;      // rows/s
  uint8_t trailLen; // visible tail rows behind the head
  float respawn;    // seconds until this column restarts (while inactive)
};

static MxColumn mx_cols[MX_COLS];
static char mx_chars[MX_COLS][MX_ROWS];

// Digit decode state (slot 2 = colon, never decodes)
static bool mx_decode[5];
static float mx_decode_t[5];
static float mx_decode_swap[5];
static char mx_decode_char[5];
static uint8_t mx_new_val[5];

// Minute-change bookkeeping
static int last_minute_mx = -1;
static bool mx_triggered = false;

static unsigned long last_mx_update = 0;
static bool mx_init_done = false;

// ========== Helpers ==========
static char mxRandChar() {
  return MX_CHARSET[random(0, (int)sizeof(MX_CHARSET) - 1)];
}

static float mxRandf(float lo, float hi) {
  return lo + (hi - lo) * (random(0, 1001) / 1000.0f);
}

static int mxTimeY() {
  return settings.matrixShowDate ? MX_TIME_Y_TOP : MX_TIME_Y_CENTER;
}

static bool mxDecodeActive() {
  for (int i = 0; i < 5; i++) {
    if (mx_decode[i]) return true;
  }
  return false;
}

// Base head speed in rows/s from the user setting (tenths, default 12 = 1.2)
static float mxBaseSpeed() {
  int v = constrain((int)settings.matrixRainSpeed, 5, 30);
  return 4.5f * (v / 10.0f);
}

// Column respawn delay range per density setting
static float mxRespawnDelay() {
  switch (settings.matrixRainDensity) {
    case 0: return mxRandf(1.5f, 4.0f);   // Sparse
    case 2: return mxRandf(0.0f, 0.8f);   // Dense
    default: return mxRandf(0.5f, 2.0f);  // Normal
  }
}

static void mxSpawnColumn(MxColumn &c) {
  c.active = true;
  c.headRow = 0.0f;
  c.speed = mxBaseSpeed() * mxRandf(0.6f, 1.6f);
  c.trailLen = (uint8_t)random(3, 8);  // 3-7 rows of tail
  c.respawn = 0.0f;
}

// Fade LUT derived from the user's rain color every frame, so color edits
// in the web UI apply live. Index 0 = dimmest, MX_FADE_LEVELS-1 = full.
static void mxBuildFade(uint16_t lut[MX_FADE_LEVELS]) {
  uint16_t base = SPRITE_COLOR(COL_MATRIX_RAIN);
  uint8_t r = (base >> 11) & 0x1F;
  uint8_t g = (base >> 5) & 0x3F;
  uint8_t b = base & 0x1F;
  for (int i = 0; i < MX_FADE_LEVELS; i++) {
    lut[i] = (uint16_t)(((r * (i + 1) / MX_FADE_LEVELS) << 11) |
                        ((g * (i + 1) / MX_FADE_LEVELS) << 5) |
                        (b * (i + 1) / MX_FADE_LEVELS));
  }
}

// ========== Reset ==========
void resetMatrixRainAnimation() {
  for (int c = 0; c < MX_COLS; c++) {
    mx_cols[c].active = false;
    mx_cols[c].respawn = mxRandf(0.0f, 1.5f);  // staggered first wave
    for (int r = 0; r < MX_ROWS; r++) {
      mx_chars[c][r] = mxRandChar();
    }
  }
  for (int i = 0; i < 5; i++) {
    mx_decode[i] = false;
    mx_decode_t[i] = 0.0f;
    mx_decode_swap[i] = 0.0f;
    mx_decode_char[i] = '0';
    mx_new_val[i] = 0;
  }
  last_minute_mx = -1;
  mx_triggered = false;
  last_mx_update = 0;
  mx_init_done = true;
}

// ========== Update ==========
static void updateMatrixAnimation(struct tm *timeinfo) {
  unsigned long now = millis();
  float dt = (now - last_mx_update) / 1000.0f;
  if (dt > 0.1f || last_mx_update == 0) dt = 0.016f;
  last_mx_update = now;

  // ----- Minute-change trigger (same scheme as Snake/Tetris/Asteroids) -----
  int seconds = timeinfo->tm_sec;
  int minute = timeinfo->tm_min;
  if (minute != last_minute_mx) {
    last_minute_mx = minute;
    mx_triggered = false;
  }
  if (seconds >= MX_TRIGGER_SECOND && !mx_triggered && !mxDecodeActive()) {
    mx_triggered = true;
    time_overridden = true;
    time_override_start = millis();
    calculateTargetDigits(displayed_hour, displayed_min, displayed_is_pm);

    int changes = 0;
    for (int i = 0; i < num_targets; i++) {
      int di = target_digit_index[i];
      if (di == 2) continue;  // skip the colon
      mx_decode[di] = true;
      mx_decode_t[di] = MX_DECODE_TIME;
      mx_decode_swap[di] = 0.0f;
      mx_decode_char[di] = mxRandChar();
      mx_new_val[di] = (uint8_t)target_digit_values[i];
      changes++;
    }
    if (changes == 0) time_overridden = false;  // only the colon changed
  }

  // ----- Decode flicker on changed digits -----
  for (int i = 0; i < 5; i++) {
    if (!mx_decode[i]) continue;
    mx_decode_t[i] -= dt;
    mx_decode_swap[i] -= dt;
    if (mx_decode_swap[i] <= 0.0f) {
      mx_decode_char[i] = mxRandChar();
      mx_decode_swap[i] = MX_DECODE_SWAP;
    }
    if (mx_decode_t[i] <= 0.0f) {
      mx_decode[i] = false;
      updateDisplayedTimeDigit(i, mx_new_val[i]);
    }
  }

  // ----- Rain columns -----
  int mutateChance = (int)(MX_MUTATE_RATE * dt * 1000.0f);  // per-cell, of 1000

  for (int c = 0; c < MX_COLS; c++) {
    MxColumn &col = mx_cols[c];
    if (!col.active) {
      col.respawn -= dt;
      if (col.respawn <= 0.0f) mxSpawnColumn(col);
      continue;
    }

    // Columns crossing a decoding digit run hot, as if flushing the change
    float speed = col.speed;
    if (mxDecodeActive()) {
      int colX = MX_X_OFF + c * MX_CELL_W + MX_CELL_W / 2;
      for (int i = 0; i < 5; i++) {
        if (mx_decode[i] && colX >= DIGIT_X[i] - 1 &&
            colX <= DIGIT_X[i] + MX_DIGIT_W + 1) {
          speed *= 2.0f;
          break;
        }
      }
    }

    int prevHead = (int)col.headRow;
    col.headRow += speed * dt;
    int head = (int)col.headRow;

    // Fresh glyph on every row the head newly enters
    for (int r = prevHead + 1; r <= head && r < MX_ROWS; r++) {
      if (r >= 0) mx_chars[c][r] = mxRandChar();
    }

    // Column is done once the whole tail has left the bottom
    if (head - (int)col.trailLen >= MX_ROWS) {
      col.active = false;
      col.respawn = mxRespawnDelay();
      continue;
    }

    // Mutate glyphs inside the visible trail
    for (int k = 1; k <= col.trailLen; k++) {
      int r = head - k;
      if (r < 0 || r >= MX_ROWS) continue;
      if (random(0, 1000) < mutateChance) mx_chars[c][r] = mxRandChar();
    }
  }
}

// ========== Drawing ==========
static void drawMatrixRain() {
  uint16_t fade[MX_FADE_LEVELS];
  mxBuildFade(fade);
  uint16_t headCol = SPRITE_COLOR(COL_MATRIX_HEAD);

  display.setTextSize(1);
  for (int c = 0; c < MX_COLS; c++) {
    const MxColumn &col = mx_cols[c];
    if (!col.active) continue;
    int head = (int)col.headRow;
    int x = MX_X_OFF + c * MX_CELL_W;

    for (int k = 0; k <= col.trailLen; k++) {
      int r = head - k;
      if (r < 0 || r >= MX_ROWS) continue;
      uint16_t color;
      if (k == 0) {
        color = headCol;
      } else {
        int lvl = (MX_FADE_LEVELS - 1) -
                  (k * (MX_FADE_LEVELS - 1)) / col.trailLen;
        color = fade[lvl];
      }
      // bg == color skips the background fill (screen is cleared each frame)
      display.drawChar(x, r * MX_CELL_H, mx_chars[c][r], color, color, 1);
    }
  }
}

void displayClockWithMatrixRain() {
  if (!mx_init_done) resetMatrixRainAnimation();

  struct tm timeinfo;
  if (!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    display.print(ntpSynced ? "Time Error" : "Syncing time...");
    return;
  }

  updateMatrixAnimation(&timeinfo);

  if (!time_overridden) syncDisplayedTime(&timeinfo);
  maintainTimeOverride(&timeinfo, !mxDecodeActive());

  drawMatrixRain();

  int gy = mxTimeY();

  // Time digits (size 3) on solid plates. The rain is busy enough that every
  // piece of foreground text gets a masked box, or it shimmers unreadably.
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
    display.fillRect(dx - 1, gy - 1, MX_DIGIT_W + 2, MX_DIGIT_H + 2,
                     DISPLAY_BLACK);
    display.setCursor(dx, gy);
    if (mx_decode[i]) {
      // Bright decode flicker until the new value locks in
      display.setTextColor(SPRITE_COLOR(COL_MATRIX_HEAD));
      display.print(mx_decode_char[i]);
      display.setTextColor(SPRITE_COLOR(COL_DIGITS));
    } else {
      display.print(dch[i]);
    }
  }
  display.setTextColor(DISPLAY_WHITE);  // restore for date chrome

  // Optional date row (top), on its own plate
  if (settings.matrixShowDate) {
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
