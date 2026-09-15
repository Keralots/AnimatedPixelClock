/*
 * AnimatedPixelClock - Doom Fire Clock (clockStyle 17)
 *
 * The PSX Doom fire effect: a heat buffer where every cell cools a little as
 * it is carried up into the row above, so a hot bottom edge grows a sheet of
 * living flame. Here the time digits are heat sources too - the glyph shape is
 * stamped back into the buffer at full heat every frame, so each digit burns
 * white-hot and sheds its own flames upward off a burning ground line.
 *
 * At the top of each minute a changed digit stops feeding the fire and burns
 * away, its leftover heat rising and dying out; the new value then re-ignites
 * with a white flare. Only changed digits burn, the same :56 trigger scheme as
 * Matrix Rain / Snake / Tetris.
 *
 * One simulation step per rendered frame (no fixed tick, so nothing can beat
 * against the render frame grid). Flame height is a cooling rate rather than a
 * tick rate for the same reason. All state is file-local; resetDoomAnimation()
 * (called from resetClockAnimationState) returns everything to a clean
 * baseline.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "clocks.h"
#include "clock_constants.h"
#include "clock_globals.h"

// ========== Layout / tuning ==========
#define DOOM_W SCREEN_WIDTH
#define DOOM_H SCREEN_HEIGHT
// The classic effect uses 37 heat levels over a tall CRT buffer. Only 64 rows
// are available here, so the whole ramp is crossed in ~20 of them and 37 levels
// would step ~2 palette entries per row - a visible horizontal band per row.
#define DOOM_LEVELS 64
#define DOOM_MAX_HEAT (DOOM_LEVELS - 1)
// The ground burns cooler than the digits do, so its flames top out in
// orange well below the time and the white-hot core stays the digits alone.
#define DOOM_GROUND_HEAT ((DOOM_MAX_HEAT * 2) / 3)
// Heat at which the ramp starts going white. Kept high so white reads as
// "this is a digit": a plume leaving a digit cools out of it within a few
// rows, and the cooler ground never reaches it at all.
#define DOOM_CORE_START 49
#define DOOM_GLYPH_ROWS 7        // 5x7 glyph, drawn at 3px per cell
// Smooth-fire only. The blur spreads what were sparse embers into a continuous
// wash on the darkest palette entries, which reads as a dirty haze; this snaps
// that back to black.
#define DOOM_SOFT_FLOOR 3
#define DOOM_TIME_Y_TOP 16       // digit top when the date row is shown
#define DOOM_TIME_Y_CENTER 21    // digit top when centred (date off)
#define DOOM_TRIGGER_SECOND 56
#define DOOM_BURN_TIME 0.55f     // seconds a changed digit spends burning away
#define DOOM_IGNITE_TIME 0.55f   // seconds the new value spends flaring

// 5x7 numerals, the same table Pong/Snake/Tetris sample to place fragments and
// pellets. Sampled rather than read back from the panel, because
// MatrixDisplay.getBuffer() is nullptr on HUB75. Bit 4 = leftmost column.
static const uint8_t doomDigitGlyph[10][7] = {
  {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},
  {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
  {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},
  {0b01110, 0b10001, 0b00001, 0b00110, 0b00001, 0b10001, 0b01110},
  {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
  {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},
  {0b00110, 0b01000, 0b10000, 0b10110, 0b10001, 0b10001, 0b01110},
  {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
  {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
  {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}
};

enum DoomDigitState : uint8_t {
  DOOM_DIGIT_STEADY = 0,  // glyph feeds the fire at full heat
  DOOM_DIGIT_BURNOUT,     // heat source cut, leftover flame rises and dies
  DOOM_DIGIT_IGNITE       // new value re-lit, flaring white
};

// heat[y][x], 0 = cold. 8 KiB of .bss, not heap.
static uint8_t doom_heat[DOOM_H][DOOM_W];
static int doom_top = DOOM_H - 1;  // topmost row that currently holds heat

// Per-slot burn state (slot 2 = colon, never burns)
static DoomDigitState doom_state[5];
static float doom_state_t[5];
static uint8_t doom_new_val[5];

// Minute-change bookkeeping
static int last_minute_doom = -1;
static bool doom_triggered = false;

static unsigned long last_doom_update = 0;
static bool doom_init_done = false;

// ========== Helpers ==========

// xorshift32. One call per cell per frame, so Arduino random() (a libc call
// with a modulo) would dominate the simulation cost across 8192 cells.
static uint32_t doom_rng = 0x2545F491u;
static inline uint32_t doomRand() {
  doom_rng ^= doom_rng << 13;
  doom_rng ^= doom_rng >> 17;
  doom_rng ^= doom_rng << 5;
  return doom_rng;
}

static int doomTimeY() {
  return settings.doomShowDate ? DOOM_TIME_Y_TOP : DOOM_TIME_Y_CENTER;
}

static bool doomBurnActive() {
  for (int i = 0; i < 5; i++) {
    if (doom_state[i] != DOOM_DIGIT_STEADY) return true;
  }
  return false;
}

// Mean cooling per row, 8.8 fixed point. A source dies after
// (its heat / mean cooling) rows, so dividing each source's own heat by the
// wanted reach makes both settings read directly as pixels.
static uint16_t doomDigitDecayQ8() {
  int h = constrain((int)settings.doomFlameHeight, 8, 40);
  return (uint16_t)((DOOM_MAX_HEAT * 256) / h);
}

static uint16_t doomGroundDecayQ8() {
  int h = constrain((int)settings.doomGroundHeight, 5, 40);
  return (uint16_t)((DOOM_GROUND_HEAT * 256) / h);
}

// Topmost row the ground fire can still reach.
static int doomGroundTop() {
  return DOOM_H - 1 - constrain((int)settings.doomGroundHeight, 5, 40);
}

// Horizontal drift as heat moves up one row. The classic effect biases left;
// the other two settings centre it or mirror it.
//
// The jitter is a difference of two three-bit popcounts: it spans -3..+3 but
// sits near 0 most of the time. Neighbouring cells then rarely sample the same
// source - a plain 4-value offset bands the flame into horizontal dashes - while
// a plume still holds together instead of smearing into drifting sparks. The
// drift itself is a half step per row, as in the original.
static inline int doomWindShift(uint32_t r) {
  int jitter = (int)((r & 1) + ((r >> 1) & 1) + ((r >> 2) & 1)) -
               (int)(((r >> 3) & 1) + ((r >> 4) & 1) + ((r >> 5) & 1));
  int drift = (int)((r >> 16) & 1);
  switch (settings.doomWind) {
    case 1: return jitter;
    case 2: return jitter + drift;
    default: return jitter - drift;
  }
}

// Fire ramp rebuilt every frame from the three user colors, so palette edits in
// the web UI apply live. Index 0 is never drawn.
static void doomBuildPalette(uint16_t lut[DOOM_LEVELS]) {
  uint16_t ember = SPRITE_COLOR(COL_DOOM_EMBER);
  uint16_t flame = SPRITE_COLOR(COL_DOOM_FLAME);
  uint16_t core = SPRITE_COLOR(COL_DOOM_CORE);
  lut[0] = DISPLAY_BLACK;
  for (int i = 1; i < DOOM_LEVELS; i++) {
    uint16_t a, b;
    int num, den;
    if (i < DOOM_CORE_START) {
      a = ember; b = flame; num = i - 1; den = DOOM_CORE_START - 2;
    } else {
      a = flame; b = core; num = i - DOOM_CORE_START + 1;
      den = DOOM_LEVELS - DOOM_CORE_START;
    }
    int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    lut[i] = (uint16_t)((((ar + (br - ar) * num / den) & 0x1F) << 11) |
                        (((ag + (bg - ag) * num / den) & 0x3F) << 5) |
                        ((ab + (bb - ab) * num / den) & 0x1F));
  }
}

// Blend toward white for the ignite flare.
static uint16_t doomWhiten(uint16_t c, int num, int den) {
  int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  r += (0x1F - r) * num / den;
  g += (0x3F - g) * num / den;
  b += (0x1F - b) * num / den;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// Glyph cell lookup on the 5x7 grid, bounds-checked so the halo pass can ask
// about cells off the edge of the glyph.
static bool doomGlyphLit(const uint8_t* rows, int gx, int gy) {
  if (gx < 0 || gx > 4 || gy < 0 || gy > 6) return false;
  return (rows[gy] >> (4 - gx)) & 0x01;
}

// Digits are drawn from the same table that stamps them into the heat buffer.
// Using display.print() here instead would draw the Adafruit GFX font, whose
// numerals differ (its zero is slashed), and the glyph would no longer line up
// with its own heat source and halo.
static void doomDrawGlyph(int x0, int y0, uint8_t value, uint16_t color) {
  if (value > 9) return;
  const uint8_t* rows = doomDigitGlyph[value];
  for (int gy = 0; gy < 7; gy++) {
    for (int gx = 0; gx < 5; gx++) {
      if (doomGlyphLit(rows, gx, gy)) {
        display.fillRect(x0 + gx * 3, y0 + gy * 3, 3, 3, color);
      }
    }
  }
}

// ========== Reset ==========
void resetDoomAnimation() {
  memset(doom_heat, 0, sizeof(doom_heat));
  doom_top = DOOM_H - 1;
  for (int i = 0; i < 5; i++) {
    doom_state[i] = DOOM_DIGIT_STEADY;
    doom_state_t[i] = 0.0f;
    doom_new_val[i] = 0;
  }
  last_minute_doom = -1;
  doom_triggered = false;
  last_doom_update = 0;
  doom_rng = 0x2545F491u ^ (uint32_t)millis();
  if (doom_rng == 0) doom_rng = 0x2545F491u;
  doom_init_done = true;
}

// ========== Simulation ==========

// Fill every cell of the row above by sampling the row below with a random
// sideways offset, cooling it on the way.
//
// The original pushes each cell up into a randomly offset neighbour instead.
// That leaves destination cells nothing lands on holding their previous value,
// which is what makes the classic effect continuous - but only because it runs
// over a tall CRT-shaped buffer. Pushing on a 64px panel and clearing the stale
// cells instead reads as orange confetti, so this pulls: every destination gets
// exactly one sample, and the sheet of flame stays solid.
static void doomSpread(int gy) {
  int top = doom_top;
  if (top < 1) top = 1;

  // Rows from the digit box upward cool at the digit rate, everything below it
  // at the ground rate, so the two heights are independent. A plume only ever
  // travels upward out of the digits, so it never sees the ground rate.
  uint16_t digitQ8 = doomDigitDecayQ8();
  uint16_t groundQ8 = doomGroundDecayQ8();
  const int digitBase = (int)(digitQ8 >> 8);
  const uint8_t digitFrac = (uint8_t)(digitQ8 & 0xFF);
  const int groundBase = (int)(groundQ8 >> 8);
  const uint8_t groundFrac = (uint8_t)(groundQ8 & 0xFF);
  // Smooth mode loses plume height twice over: the blur spreads a cell's heat
  // across its neighbours and DOOM_SOFT_FLOOR trims the faint tip. Cooling the
  // softened rows a quarter slower puts that height back with a bit to spare.
  const uint16_t softQ8 = (uint16_t)((digitQ8 * 3) / 4);
  const int softBase = (int)(softQ8 >> 8);
  const uint8_t softFrac = (uint8_t)(softQ8 & 0xFF);

  // Softening stops at the bottom of the digit box plus its halo row, and is
  // pulled further up if the ground fire reaches that high - the ground is left
  // exactly as it is, because the blocky look is what makes it read as fire and
  // that is not what the option is for. Keeping the boundary clear of the
  // ground also keeps it inside the dark gap, so there is no visible seam
  // between the softened rows and the raw ones.
  const int softLimit = min(gy + DOOM_GLYPH_ROWS * 3 + 3, doomGroundTop() - 2);
  uint8_t rowCopy[DOOM_W];

  int newTop = DOOM_H - 1;
  bool foundTop = false;
  for (int y = top; y < DOOM_H; y++) {
    const uint8_t* src = doom_heat[y];
    uint8_t* dst = doom_heat[y - 1];
    const bool softRow = settings.doomSmoothFire && (y - 1) <= softLimit;
    const bool digitRow = (y - 1) < gy;  // above the digit box = plume territory

    // The blend needs each cell's previous value, which the write below
    // destroys, so take the row first.
    if (softRow) memcpy(rowCopy, dst, DOOM_W);

    for (int x = 0; x < DOOM_W; x++) {
      uint32_t r = doomRand();
      // Sampling the cell to the right carries heat left, so the shift keeps
      // its "positive is rightward" meaning.
      int sx = x - doomWindShift(r);
      if (sx < 0) sx = 0;
      else if (sx >= DOOM_W) sx = DOOM_W - 1;
      uint8_t heat = src[sx];
      int decay = softRow ? softBase : (digitRow ? digitBase : groundBase);
      uint8_t frac = softRow ? softFrac : (digitRow ? digitFrac : groundFrac);
      if ((uint8_t)(r >> 8) < frac) decay++;
      // Spread the cooling a step either side of the mean. Neighbouring cells
      // then land on different palette entries and the steep vertical gradient
      // dithers instead of banding into stripes.
      int spread = (int)((r >> 24) & 3);
      if (spread == 0) decay--;
      else if (spread == 3) decay++;
      if (decay < 0) decay = 0;
      uint8_t cooled = (heat > decay) ? (uint8_t)(heat - decay) : 0;
      // Carry half of what the cell held last frame. The flame stops twitching
      // between frames and starts flowing.
      dst[x] = softRow ? (uint8_t)((cooled + rowCopy[x] + 1) >> 1) : cooled;
    }

    if (softRow) {
      // Horizontal 1-2-1 blur off a pre-blur copy, which is what actually takes
      // the hard pixel edges off the plume. The floor matters: without it the
      // blur smears sparse embers into a continuous wash on the darkest palette
      // entries and the panel looks dirty rather than dark.
      memcpy(rowCopy, dst, DOOM_W);
      for (int x = 0; x < DOOM_W; x++) {
        int l = rowCopy[x > 0 ? x - 1 : 0];
        int r2 = rowCopy[x < DOOM_W - 1 ? x + 1 : DOOM_W - 1];
        int v = (l + 2 * (int)rowCopy[x] + r2) >> 2;
        dst[x] = (v < DOOM_SOFT_FLOOR) ? 0 : (uint8_t)v;
      }
    }

    if (!foundTop) {
      for (int x = 0; x < DOOM_W; x++) {
        if (dst[x]) { newTop = y - 1; foundTop = true; break; }
      }
    }
  }
  doom_top = newTop;
}

static void doomFillHeat(int x0, int y0, int w, int h, uint8_t value) {
  for (int y = y0; y < y0 + h; y++) {
    if (y < 0 || y >= DOOM_H) continue;
    for (int x = x0; x < x0 + w; x++) {
      if (x < 0 || x >= DOOM_W) continue;
      doom_heat[y][x] = value;
    }
  }
}

// Re-apply the heat sources: the burning ground line, then every digit that is
// currently feeding the fire.
static void doomStampSources(const char* glyphs, int gy) {
  // The ground is re-noised every frame on purpose: it is the generator the
  // rest of the sheet inherits its grain from. Freezing it into a fixed pattern
  // leaves the band a dead gradient once the smoothing above has damped it.
  uint8_t* ground = doom_heat[DOOM_H - 1];
  for (int x = 0; x < DOOM_W; x++) {
    ground[x] = (uint8_t)(DOOM_GROUND_HEAT - (doomRand() & 7));
  }

  if (!settings.doomBurningDigits) return;

  for (int i = 0; i < 5; i++) {
    if (doom_state[i] == DOOM_DIGIT_BURNOUT) continue;  // burning away
    int dx0 = DIGIT_X[i];

    if (i == 2) {
      if (glyphs[2] != ':') continue;
      for (int b = 0; b < 2; b++) {
        doomFillHeat(dx0 + 5, gy + 5 + b * 6, 5, 5, 0);  // halo
        doomFillHeat(dx0 + 6, gy + 6 + b * 6, 3, 3, DOOM_MAX_HEAT);
      }
      continue;
    }

    uint8_t value = (uint8_t)(glyphs[i] - '0');
    if (value > 9) continue;
    const uint8_t* rows = doomDigitGlyph[value];
    for (int cy = 0; cy < DOOM_GLYPH_ROWS; cy++) {
      for (int cx = -1; cx <= 5; cx++) {
        if (doomGlyphLit(rows, cx, cy)) {
          doomFillHeat(dx0 + cx * 3, gy + cy * 3, 3, 3, DOOM_MAX_HEAT);
        } else if (doomGlyphLit(rows, cx - 1, cy) || doomGlyphLit(rows, cx + 1, cy)) {
          // Cold halo down the sides only, so neighbouring digits cannot bleed
          // into each other. Never above a stroke - that cell is re-zeroed every
          // frame and the digit could not throw a flame at all - and never
          // below one either: that used to cut a dead band through the ground
          // fire wherever it reached the digits, instead of letting the two
          // meet. The glyph is drawn opaque on top, so it stays readable with
          // flame right up against it.
          doomFillHeat(dx0 + cx * 3, gy + cy * 3, 3, 3, 0);
        }
      }
    }
  }

  if (gy < doom_top) doom_top = gy;
  if (doom_top < 0) doom_top = 0;
  if (doom_top > DOOM_H - 1) doom_top = DOOM_H - 1;
}

static void updateDoomAnimation(struct tm* timeinfo) {
  unsigned long now = millis();
  float dt = (now - last_doom_update) / 1000.0f;
  if (dt > 0.1f || last_doom_update == 0) dt = 0.016f;
  last_doom_update = now;

  // ----- Minute-change trigger (same scheme as Matrix Rain / Snake / Tetris) -----
  if (timeinfo->tm_min != last_minute_doom) {
    last_minute_doom = timeinfo->tm_min;
    doom_triggered = false;
  }
  if (timeinfo->tm_sec >= DOOM_TRIGGER_SECOND && !doom_triggered && !doomBurnActive()) {
    doom_triggered = true;
    time_overridden = true;
    time_override_start = millis();
    calculateTargetDigits(displayed_hour, displayed_min, displayed_is_pm);

    int changes = 0;
    for (int i = 0; i < num_targets; i++) {
      int di = target_digit_index[i];
      if (di == 2) continue;  // the colon never burns
      doom_state[di] = DOOM_DIGIT_BURNOUT;
      doom_state_t[di] = DOOM_BURN_TIME;
      doom_new_val[di] = (uint8_t)target_digit_values[i];
      changes++;
    }
    if (changes == 0) time_overridden = false;  // only the colon changed
  }

  // ----- Burn away, then re-ignite -----
  for (int i = 0; i < 5; i++) {
    if (doom_state[i] == DOOM_DIGIT_STEADY) continue;
    doom_state_t[i] -= dt;
    if (doom_state_t[i] > 0.0f) continue;
    if (doom_state[i] == DOOM_DIGIT_BURNOUT) {
      updateDisplayedTimeDigit(i, doom_new_val[i]);
      doom_state[i] = DOOM_DIGIT_IGNITE;
      doom_state_t[i] = DOOM_IGNITE_TIME;
    } else {
      doom_state[i] = DOOM_DIGIT_STEADY;
      doom_state_t[i] = 0.0f;
    }
  }
}

// ========== Drawing ==========
// Every pixel is written, cold cells included, so the caller can skip
// clearDisplay(). flipDMABuffer() only queues the chain switch, so the buffer
// we draw into is still the one on screen for up to a full scan - blanking it
// first shows as a dark flash, worst on the rows drawn last. Cold rows collapse
// into one drawFastHLine each, so painting them costs almost nothing.
static void drawDoomFire() {
  uint16_t pal[DOOM_LEVELS];
  doomBuildPalette(pal);

  for (int y = 0; y < DOOM_H; y++) {
    const uint8_t* row = doom_heat[y];
    int x = 0;
    while (x < DOOM_W) {
      uint8_t heat = row[x];
      int run = 1;
      while (x + run < DOOM_W && row[x + run] == heat) run++;
      // Equal-heat runs are common across a row of flame, so batching them
      // saves a few thousand per-pixel calls per frame at 60 Hz.
      if (run == 1) display.drawPixel(x, y, pal[heat]);
      else display.drawFastHLine(x, y, run, pal[heat]);
      x += run;
    }
  }
}

// Crisp glyph over the fire, so the time stays readable whatever the flame is
// doing. A digit mid-burnout is deliberately absent.
static void drawDoomDigits(const char* glyphs, int gy) {
  for (int i = 0; i < 5; i++) {
    if (doom_state[i] == DOOM_DIGIT_BURNOUT) continue;
    if (i == 2 && glyphs[2] != ':') continue;

    uint16_t color = digitColor();
    if (doom_state[i] == DOOM_DIGIT_IGNITE) {
      int num = (int)(doom_state_t[i] * 1000.0f);
      int den = (int)(DOOM_IGNITE_TIME * 1000.0f);
      if (num > den) num = den;
      if (num < 0) num = 0;
      color = doomWhiten(color, num, den);  // white flare fading to normal
    }

    if (i == 2) {
      display.fillRect(DIGIT_X[2] + 6, gy + 6, 3, 3, color);
      display.fillRect(DIGIT_X[2] + 6, gy + 12, 3, 3, color);
      continue;
    }

    doomDrawGlyph(DIGIT_X[i], gy, (uint8_t)(glyphs[i] - '0'), color);
  }
}

// Small text over the fire. A solid plate behind it reads as a black box on a
// full-screen effect, so the characters get a one-pixel dark outline instead and
// the flame shows through between them. Diagonals are included: without them
// orange leaks into the corners of the glyphs on a busy background.
static void doomOutlineText(int x, int y, const char* str) {
  display.setTextSize(1);
  display.setTextColor(DISPLAY_BLACK);
  for (int dy = -1; dy <= 1; dy++) {
    for (int dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dy == 0) continue;
      display.setCursor(x + dx, y + dy);
      display.print(str);
    }
  }
  display.setTextColor(DISPLAY_WHITE);
  display.setCursor(x, y);
  display.print(str);
}

void displayClockWithDoom() {
  if (!doom_init_done) resetDoomAnimation();

  struct tm timeinfo;
  if (!getTimeWithTimeout(&timeinfo)) {
    display.clearDisplay();  // this path paints no fire, so it must blank
    display.setTextSize(1);
    display.setTextColor(DISPLAY_WHITE);
    display.setCursor(20, 28);
    display.print(ntpSynced ? "Time Error" : "Syncing time...");
    return;
  }

  updateDoomAnimation(&timeinfo);

  if (!time_overridden) syncDisplayedTime(&timeinfo);
  maintainTimeOverride(&timeinfo, !doomBurnActive());

  char glyphs[5];
  glyphs[0] = '0' + displayed_hour / 10;
  glyphs[1] = '0' + displayed_hour % 10;
  glyphs[2] = shouldShowColon() ? ':' : ' ';
  glyphs[3] = '0' + displayed_min / 10;
  glyphs[4] = '0' + displayed_min % 10;

  int gy = doomTimeY();

  doomSpread(gy);
  doomStampSources(glyphs, gy);
  drawDoomFire();
  drawDoomDigits(glyphs, gy);

  // Date and meridiem sit in the flames rising off the digits, so both are
  // outlined rather than plated.
  if (settings.doomShowDate) {
    char dateStr[12];
    switch (settings.dateFormat) {
      case 0: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
      case 1: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900); break;
      case 2: sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday); break;
      case 3: sprintf(dateStr, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
    }
    doomOutlineText((SCREEN_WIDTH - DATE_DISPLAY_WIDTH) / 2, 4, dateStr);
  }
  if (!settings.use24Hour) {
    doomOutlineText(110, 4, displayed_is_pm ? "PM" : "AM");
  }

  if (!wifiConnected) drawNoWiFiIcon(0, 0);
}
