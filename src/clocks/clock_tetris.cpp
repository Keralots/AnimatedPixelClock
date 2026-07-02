/*
 * AnimatedPixelClock - Tetris Clock (clockStyle 8)
 *
 * Digits are drawn as a 5x7 block grid, sat low on the screen so the rebuild
 * animation above them is easy to see. The idle look is calm: static block
 * numerals, a blinking block colon, and the occasional tumbling tetromino.
 *
 * On the minute change each changed digit is rebuilt. Two rebuild styles:
 *   - Drop-in slabs: the old digit bursts into falling fragments and the new
 *     digit drops in as three slabs that lock into place bottom-up.
 *   - Falling dots: the new digit is assembled from single dots that fall
 *     straight down into place, filling the digit from the bottom up.
 * When several digits change at once they rebuild ONE AT A TIME (left to
 * right), for both styles.
 *
 * The date row is optional and can sit at the top or the bottom.
 *
 * All state is file-local. resetTetrisAnimation() (called from
 * resetClockAnimationState) returns everything to a clean baseline.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "clocks.h"
#include "clock_globals.h"

// ========== Layout / tuning ==========
#define TET_TIME_Y_LOW 28            // date on top: digits sit low, room below
#define TET_TIME_Y_CENTER 22         // date bottom/off: digits vertically centred
#define TET_PITCH 3                  // cell pitch (block + 1px gap)
#define TET_GRID_W 5
#define TET_GRID_H 7
#define TET_ANIM_SPEED 16            // ms; matches the 60 Hz render frame so every
                                     // rendered frame advances motion (no judder).
                                     // Per-tick motion constants are scaled to this.
#define TET_TRIGGER_SECOND 56
#define TET_START_FALL 26            // how far above a slab starts its drop
#define TET_MAX_FRAG 36
#define TET_MAX_DOTS (TET_GRID_W * TET_GRID_H)
#define TET_DATE_Y_TOP 4
#define TET_DATE_Y_BOTTOM 56
#define TET_SLIP_PCT 8               // Smooth Play: % of drops allowed to leave a
                                     // hole (a "human slip") instead of avoiding it

// 5x7 block glyphs (same numerals as the Pac-Man pellet font)
static const uint8_t blockDigitPatterns[10][TET_GRID_H] = {
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

struct TetFrag { float x, y, vx, vy; bool active; };

// ----- Sequential rebuild queue (one digit animates at a time) -----
static int tet_seq_idx[5];           // digit indices to rebuild, in order
static uint8_t tet_seq_val[5];       // their new values
static int tet_seq_len = 0;
static int tet_seq_pos = 0;
static int tet_active = -1;          // digit currently animating, -1 = none
static uint8_t tet_active_val = 0;   // new value of the active digit

// ----- Slab drop-in state (active digit only) -----
static int tet_slab = 3;             // current falling slab (0,1,2) or 3 = settled
static float tet_slab_off = 0;       // current slab's falling Y offset (<=0)
static float tet_slab_vel = 0;

// ----- Falling-dot build-up state (active digit only) -----
static int   dot_tx[TET_MAX_DOTS];   // target x of each lit pixel
static int   dot_ty[TET_MAX_DOTS];   // target y of each lit pixel
static float dot_cy[TET_MAX_DOTS];   // current (falling) y
static int   dot_delay[TET_MAX_DOTS];// frame at which the dot is released
static bool  dot_locked[TET_MAX_DOTS];
static int   dot_n = 0;
static int   dot_frame = 0;

static TetFrag tet_frags[TET_MAX_FRAG];

// ----- Idle Tetris game (auto-played in a well below the centred clock) -----
#define TET_WELL_COLS 32             // 128 / 4 (fixed: full-row mask is 32-bit)
#define TET_WELL_ROWS 5              // ~20px strip under the centred clock
#define TET_WELL_CELL 4
#define TET_FULLROW 0xFFFFFFFFu
static const int TET_WELL_TOP = TET_TIME_Y_CENTER + TET_GRID_H * TET_PITCH + 1;  // 44

enum TetGamePhase { TG_DELAY, TG_MOVING, TG_CLEARING };
static uint32_t tet_well[TET_WELL_ROWS];  // bit c set = filled; row 0 = top of well
// Piece index (0-6, I..L) that filled each settled cell - drives per-piece color.
// Only meaningful where the matching tet_well bit is set.
static uint8_t tet_well_col[TET_WELL_ROWS][TET_WELL_COLS];
// The 7 piece colors are read as COL_TET_I + pieceIndex, so the slots must be
// contiguous in I,O,T,S,Z,J,L order. Fail the build if a future enum edit breaks it.
static_assert(COL_TET_L - COL_TET_I == 6,
              "Tetris piece color slots must stay contiguous in I,O,T,S,Z,J,L order");
static TetGamePhase tet_game_phase = TG_DELAY;
static int tet_pc_rot = 0, tet_pc_destCol = 0, tet_pc_destOy = 0;
static int tet_pc_piece = 0;      // which of the 7 pieces is falling
static int tet_pc_drawRot = 0;    // orientation shown while tumbling (settles to tet_pc_rot)
static int tet_spin_timer = 0;
static int tet_spin_left = 0;     // remaining tumbles (randomized, max 4)
static float tet_pc_curCol = 0;   // animated column (cells)
static float tet_pc_py = 0;       // animated top Y (screen pixels; falls from top)
static unsigned long tet_game_timer = 0;
static uint8_t tet_clear_mask = 0;        // which well rows are full and flashing
static int tet_clear_flash = 0;

static int last_minute_tetris = -1;
static bool tetris_triggered = false;
static bool tetris_transitioning = false;
static unsigned long last_tetris_update = 0;
static bool tetris_init_done = false;

// Tetromino rotations used by the idle game (19 distinct orientations).
struct TetRot { int8_t w, h; int8_t cx[4]; int8_t cy[4]; };
static const TetRot TET_ROTS[19] = {
  {4,1, {0,1,2,3}, {0,0,0,0}},  // I horizontal
  {1,4, {0,0,0,0}, {0,1,2,3}},  // I vertical
  {2,2, {0,1,0,1}, {0,0,1,1}},  // O
  {3,2, {0,1,2,1}, {0,0,0,1}},  // T
  {2,3, {1,0,1,1}, {0,1,1,2}},  // T
  {3,2, {1,0,1,2}, {0,1,1,1}},  // T
  {2,3, {0,0,1,0}, {0,1,1,2}},  // T
  {3,2, {1,2,0,1}, {0,0,1,1}},  // S
  {2,3, {0,0,1,1}, {0,1,1,2}},  // S
  {3,2, {0,1,1,2}, {0,0,1,1}},  // Z
  {2,3, {1,0,1,0}, {0,1,1,2}},  // Z
  {3,2, {0,0,1,2}, {0,1,1,1}},  // J
  {2,3, {0,1,0,0}, {0,0,1,2}},  // J
  {3,2, {0,1,2,2}, {0,0,0,1}},  // J
  {2,3, {1,1,0,1}, {0,1,2,2}},  // J
  {3,2, {2,0,1,2}, {0,1,1,1}},  // L
  {2,3, {0,0,0,1}, {0,1,2,2}},  // L
  {3,2, {0,1,2,0}, {0,0,0,1}},  // L
  {2,3, {0,1,1,1}, {0,0,1,2}},  // L
};

// Rotation range (start index, count) for each of the 7 pieces: I,O,T,S,Z,J,L.
static const uint8_t TET_PIECE_ROT[7][2] = {
  {0,2}, {2,1}, {3,4}, {7,2}, {9,2}, {11,4}, {15,4}
};

// ========== Helpers ==========
static int tetBlockSize() { return settings.tetrisBlockStyle == 1 ? TET_PITCH : 2; }

static int tetDateY() {
  return settings.tetrisDatePosition == 1 ? TET_DATE_Y_BOTTOM : TET_DATE_Y_TOP;
}

// The idle game owns the bottom strip, so when it is on the clock is forced
// centred and dateless regardless of the date settings.
static bool tetDateShown() {
  return settings.tetrisShowDate && !settings.tetrisIdleTumble;
}

static int tetTimeY() {
  if (tetDateShown() && settings.tetrisDatePosition == 0) return TET_TIME_Y_LOW;
  return TET_TIME_Y_CENTER;
}

static void tetSlabRows(int slab, int &rs, int &re) {
  if (slab == 0) { rs = 4; re = 6; }
  else if (slab == 1) { rs = 2; re = 3; }
  else { rs = 0; re = 1; }
}

static void tetDrawCells(int idx, uint8_t val, int rowStart, int rowEnd, int yOff) {
  if (val > 9) return;
  int bs = tetBlockSize();
  for (int row = rowStart; row <= rowEnd; row++) {
    uint8_t bits = blockDigitPatterns[val][row];
    for (int col = 0; col < TET_GRID_W; col++) {
      if ((bits >> (TET_GRID_W - 1 - col)) & 1) {
        int px = DIGIT_X[idx] + col * TET_PITCH;
        int py = tetTimeY() + row * TET_PITCH + yOff;
        display.fillRect(px, py, bs, bs, SPRITE_COLOR(COL_DIGITS));
      }
    }
  }
}

static TetFrag *tetFreeFrag() {
  for (int i = 0; i < TET_MAX_FRAG; i++) if (!tet_frags[i].active) return &tet_frags[i];
  return nullptr;
}

static void tetSpawnFrags(int idx, uint8_t oldVal) {
  if (oldVal > 9) return;
  int spawned = 0;
  for (int row = 0; row < TET_GRID_H && spawned < 8; row++) {
    uint8_t bits = blockDigitPatterns[oldVal][row];
    for (int col = 0; col < TET_GRID_W && spawned < 8; col++) {
      if (((bits >> (TET_GRID_W - 1 - col)) & 1) && random(100) < 40) {
        TetFrag *f = tetFreeFrag();
        if (!f) return;
        f->x = DIGIT_X[idx] + col * TET_PITCH;
        f->y = tetTimeY() + row * TET_PITCH;
        f->vx = random(-10, 11) / 25.0f;
        f->vy = -(random(5, 20) / 25.0f);
        f->active = true;
        spawned++;
      }
    }
  }
}

static bool tetAnyFragActive() {
  for (int i = 0; i < TET_MAX_FRAG; i++) if (tet_frags[i].active) return true;
  return false;
}

static void tetUpdateFrags() {
  for (int i = 0; i < TET_MAX_FRAG; i++) {
    if (!tet_frags[i].active) continue;
    tet_frags[i].vy += 0.048f;
    tet_frags[i].x += tet_frags[i].vx;
    tet_frags[i].y += tet_frags[i].vy;
    if (tet_frags[i].y > SCREEN_HEIGHT + 4 || tet_frags[i].x < -4 ||
        tet_frags[i].x > SCREEN_WIDTH + 4) {
      tet_frags[i].active = false;
    }
  }
}

// ----- Idle Tetris game logic -----
static void tetGameReset() {
  for (int r = 0; r < TET_WELL_ROWS; r++) tet_well[r] = 0;
  memset(tet_well_col, 0, sizeof(tet_well_col));
  tet_game_phase = TG_DELAY;
  tet_game_timer = millis() + 400;
  tet_clear_mask = 0;
  tet_clear_flash = 0;
}

static int tetColTop(int c) {  // first filled row in column c, or ROWS if empty
  for (int r = 0; r < TET_WELL_ROWS; r++) if (tet_well[r] & (1u << c)) return r;
  return TET_WELL_ROWS;
}

// Resting top-row for a rotation dropped at leftCol; -100 if it cannot fit.
static int tetDropOy(int rot, int leftCol) {
  const TetRot &p = TET_ROTS[rot];
  if (leftCol < 0 || leftCol + p.w > TET_WELL_COLS) return -100;
  int oy = TET_WELL_ROWS;
  for (int k = 0; k < 4; k++) {
    int rest = tetColTop(leftCol + p.cx[k]) - 1 - p.cy[k];
    if (rest < oy) oy = rest;
  }
  for (int k = 0; k < 4; k++) {
    int rr = oy + p.cy[k];
    if (rr < 0 || rr >= TET_WELL_ROWS) return -100;
  }
  return oy;
}

// Score a placement (higher is better): reward line clears, punish height/holes.
// `outHoles`, when non-null, returns the resulting hole count - Smooth Play uses
// it to refuse any drop that would bury a new gap.
//
// Smooth Play's goal in this very wide (32 col), shallow (5 row) well is simply
// to keep the stack LOW and cover every column, because the bottom row clears
// the moment all columns are touched. The dominant term is "landing depth":
// always prefer the lowest spot, so pieces fill the empty floor instead of
// growing a tower. (Rewarding flatness alone, as a first attempt did, backfires
// - extending an existing tall plateau adds no step, so it piled up one side.)
// A mild bumpiness term keeps the floor filling contiguously and tidy. The
// piece itself stays random, so the stack still looks natural, not like bars.
static int tetScorePlacement(int rot, int leftCol, int oy, bool smooth, int *outHoles) {
  const TetRot &p = TET_ROTS[rot];
  uint32_t tmp[TET_WELL_ROWS];
  for (int r = 0; r < TET_WELL_ROWS; r++) tmp[r] = tet_well[r];
  for (int k = 0; k < 4; k++) tmp[oy + p.cy[k]] |= (1u << (leftCol + p.cx[k]));

  int lines = 0;
  for (int r = 0; r < TET_WELL_ROWS; r++) if (tmp[r] == TET_FULLROW) lines++;

  int aggH = 0, holes = 0, maxH = 0, bumps = 0, prevH = -1;
  for (int c = 0; c < TET_WELL_COLS; c++) {
    int top = TET_WELL_ROWS;
    for (int r = 0; r < TET_WELL_ROWS; r++) if (tmp[r] & (1u << c)) { top = r; break; }
    int h = TET_WELL_ROWS - top;
    aggH += h;
    if (h > maxH) maxH = h;
    if (prevH >= 0) bumps += abs(h - prevH);
    prevH = h;
    for (int r = top + 1; r < TET_WELL_ROWS; r++) if (!(tmp[r] & (1u << c))) holes++;
  }
  if (outHoles) *outHoles = holes;
  if (smooth) {
    // landDepth = screen row of the piece's lowest cell (TET_WELL_ROWS-1 = floor).
    // Higher = landed lower = better, so it always fills the floor first.
    int maxCy = 0;
    for (int k = 0; k < 4; k++) if (p.cy[k] > maxCy) maxCy = p.cy[k];
    int landDepth = oy + maxCy;
    return lines * 1000 + landDepth * 24 - holes * 60 - maxH * 6 - bumps * 4;
  }
  return lines * 1000 - aggH * 2 - holes * 16 - maxH * 4;
}

// Choose where to drop a piece, then spawn it above the screen, centred.
//
// The piece itself is ALWAYS RANDOM, exactly like a real game - that is what
// gives the natural shape variety (S/Z/T/L/J make real skyline steps). The
// "cheat" only ever chooses the best rotation + column for the piece it was
// dealt; it never hand-picks flat I/O bars (which is what made it look like it
// was laying down rectangles).
//
// Default play takes the best-scoring spot, holes and all - that real-game feel
// where it sometimes can't fix the stack.
//
// Smooth Play (settings.tetrisSmoothGame): same random pieces, but it scores
// spots to keep the stack LOW and every column covered AND refuses any drop
// that would bury a NEW hole (relaxed fallback only if nothing hole-free fits),
// so the wide bottom row actually fills across and clears instead of piling up
// one side. A small share of drops (TET_SLIP_PCT) still allow a hole - a "human
// slip" - so it is not suspiciously perfect.
static bool tetGamePickPiece() {
  bool smart = settings.tetrisSmoothGame;
  bool avoidHoles = smart && (random(100) >= TET_SLIP_PCT);

  int piece = random(7);                       // random shape -> natural variety
  int rotStart = TET_PIECE_ROT[piece][0];
  int rotCount = TET_PIECE_ROT[piece][1];

  // Holes already in the well: only forbid drops that ADD to them.
  int curHoles = 0;
  for (int c = 0; c < TET_WELL_COLS; c++) {
    int top = TET_WELL_ROWS;
    for (int r = 0; r < TET_WELL_ROWS; r++) if (tet_well[r] & (1u << c)) { top = r; break; }
    for (int r = top + 1; r < TET_WELL_ROWS; r++) if (!(tet_well[r] & (1u << c))) curHoles++;
  }

  int bestScore = -1000000, bestRot = -1, bestCol = 0, bestOy = 0, ties = 0;
  // Pass 0 (Smooth Play) refuses hole-burying drops; pass 1 is the relaxed
  // fallback for the rare case nothing hole-free fits. Default/slip use pass 1.
  for (int pass = (avoidHoles ? 0 : 1); pass < 2 && bestRot < 0; pass++) {
    bool forbidHoles = (pass == 0);
    for (int ri = 0; ri < rotCount; ri++) {
      int rot = rotStart + ri;
      for (int leftCol = 0; leftCol + TET_ROTS[rot].w <= TET_WELL_COLS; leftCol++) {
        int oy = tetDropOy(rot, leftCol);
        if (oy <= -100) continue;
        int placedHoles = 0;
        int sc = tetScorePlacement(rot, leftCol, oy, smart, &placedHoles);
        if (forbidHoles && placedHoles > curHoles) continue;  // would bury a gap
        if (sc > bestScore) {
          bestScore = sc; bestRot = rot; bestCol = leftCol; bestOy = oy; ties = 1;
        } else if (sc == bestScore) {
          // Random tie-break: equally-good spots (e.g. an open bottom row) get
          // scattered placements instead of packing tightly left-to-right.
          if (random(++ties) == 0) { bestRot = rot; bestCol = leftCol; bestOy = oy; }
        }
      }
    }
  }
  if (bestRot < 0) return false;

  tet_pc_rot = bestRot;
  tet_pc_destCol = bestCol;
  tet_pc_destOy = bestOy;
  tet_pc_piece = piece;
  tet_pc_drawRot = rotStart + random(rotCount);                       // random start orientation
  tet_spin_left = random(1, 5);                                       // tumble 1-4 times, then settle
  tet_spin_timer = 0;
  tet_pc_curCol = (TET_WELL_COLS - TET_ROTS[bestRot].w) / 2.0f;       // centre
  tet_pc_py = -(float)(TET_ROTS[bestRot].h * TET_WELL_CELL);          // above screen
  tet_game_phase = TG_MOVING;
  return true;
}

static void tetGameClearRows() {
  int writeR = TET_WELL_ROWS - 1;
  for (int r = TET_WELL_ROWS - 1; r >= 0; r--) {
    if (tet_clear_mask & (1 << r)) continue;
    int dst = writeR--;  // capture before decrement so the color copy matches
    tet_well[dst] = tet_well[r];
    memcpy(tet_well_col[dst], tet_well_col[r], TET_WELL_COLS);
  }
  while (writeR >= 0) {
    tet_well[writeR] = 0;
    memset(tet_well_col[writeR], 0, TET_WELL_COLS);
    writeR--;
  }
  tet_clear_mask = 0;
}

static void tetGameUpdate() {
  if (tet_game_phase == TG_DELAY) {
    if (millis() >= tet_game_timer) {
      if (!tetGamePickPiece()) {           // nothing fits -> clear the well
        for (int r = 0; r < TET_WELL_ROWS; r++) tet_well[r] = 0;
        memset(tet_well_col, 0, sizeof(tet_well_col));
        tet_game_timer = millis() + 600;
      }
    }
    return;
  }

  if (tet_game_phase == TG_CLEARING) {
    if (--tet_clear_flash <= 0) {
      tetGameClearRows();
      tet_game_phase = TG_DELAY;
      tet_game_timer = millis() + 450;
    }
    return;
  }

  // TG_MOVING: fall from the top of the screen (through the digits) while
  // sliding to the target column, then land on the stack in the bottom well.
  if (tet_pc_curCol < tet_pc_destCol)
    tet_pc_curCol = min(tet_pc_curCol + 0.4f, (float)tet_pc_destCol);
  else if (tet_pc_curCol > tet_pc_destCol)
    tet_pc_curCol = max(tet_pc_curCol - 0.4f, (float)tet_pc_destCol);

  float vstep = (settings.tetrisFallSpeed / 10.0f) * 0.6f;
  if (vstep < 0.24f) vstep = 0.24f;
  tet_pc_py += vstep;

  float landingPy = TET_WELL_TOP + tet_pc_destOy * TET_WELL_CELL;

  // Cosmetic tumble: spin a few times (max 4) while high, the last tumble
  // settling into the chosen landing orientation.
  if (tet_spin_left > 0 && tet_pc_py < landingPy - TET_WELL_CELL * 2) {
    if (++tet_spin_timer >= 12) {          // slower than before
      tet_spin_timer = 0;
      if (--tet_spin_left <= 0) {
        tet_pc_drawRot = tet_pc_rot;       // final tumble lands in the chosen orientation
      } else {
        int rs = TET_PIECE_ROT[tet_pc_piece][0];
        int rc = TET_PIECE_ROT[tet_pc_piece][1];
        tet_pc_drawRot = rs + ((tet_pc_drawRot - rs + 1) % rc);
      }
    }
  } else {
    tet_pc_drawRot = tet_pc_rot;
  }

  if (tet_pc_py >= landingPy) {
    tet_pc_py = landingPy;
    if (tet_pc_curCol != (float)tet_pc_destCol) return;  // finish aligning first
    const TetRot &p = TET_ROTS[tet_pc_rot];
    for (int k = 0; k < 4; k++) {
      int rr = tet_pc_destOy + p.cy[k];
      int cc = tet_pc_destCol + p.cx[k];
      tet_well[rr] |= (1u << cc);
      tet_well_col[rr][cc] = tet_pc_piece;  // remember which piece for its color
    }
    tet_clear_mask = 0;
    for (int r = 0; r < TET_WELL_ROWS; r++)
      if (tet_well[r] == TET_FULLROW) tet_clear_mask |= (1 << r);
    if (tet_clear_mask) { tet_game_phase = TG_CLEARING; tet_clear_flash = 15; }
    else { tet_game_phase = TG_DELAY; tet_game_timer = millis() + 450; }
  }
}

// ========== Sequential rebuild control ==========

// Begin animating the digit at queue position seqPos using the selected style.
static void tetStartDigitAnim(int seqPos) {
  tet_active = tet_seq_idx[seqPos];
  tet_active_val = tet_seq_val[seqPos];

  if (settings.tetrisAnimStyle == 1) {
    // Falling dots: build the lit-pixel list bottom-up so the digit fills
    // from the bottom (lower rows are released first).
    dot_n = 0;
    dot_frame = 0;
    int startY = (tetDateShown() && settings.tetrisDatePosition == 0)
                     ? (TET_DATE_Y_TOP + 10) // just below a top date row
                     : 0;                     // top of screen otherwise
    // Collect the lit pixels, bottom row first, left to right.
    for (int row = TET_GRID_H - 1; row >= 0; row--) {
      uint8_t bits = blockDigitPatterns[tet_active_val][row];
      for (int col = 0; col < TET_GRID_W; col++) {
        if ((bits >> (TET_GRID_W - 1 - col)) & 1) {
          dot_tx[dot_n] = DIGIT_X[tet_active] + col * TET_PITCH;
          dot_ty[dot_n] = tetTimeY() + row * TET_PITCH;
          dot_cy[dot_n] = startY;
          dot_locked[dot_n] = false;
          dot_n++;
        }
      }
    }
    // Release the dots ONE AT A TIME so each falls separately and the digit
    // assembles dot by dot. Dot Fall Speed sets the gap between releases
    // (lower = slower, more separated). Order is bottom-up or random.
    int gap = (int)(225.0f / settings.tetrisDotSpeed);
    if (gap < 10) gap = 10;
    if (gap > 45) gap = 45;
    int order[TET_MAX_DOTS];
    for (int k = 0; k < dot_n; k++) order[k] = k;
    if (settings.tetrisDotOrder == 1) {           // random build order
      for (int k = dot_n - 1; k > 0; k--) {
        int j = random(k + 1);
        int tmp = order[k]; order[k] = order[j]; order[j] = tmp;
      }
    }
    for (int k = 0; k < dot_n; k++) dot_delay[order[k]] = k * gap;  // each waits for the previous
  } else {
    // Drop-in slabs: shatter the old digit, drop the first slab from above.
    tetSpawnFrags(tet_active, getDisplayedDigitValue(tet_active));
    tet_slab = 0;
    tet_slab_off = -TET_START_FALL;
    tet_slab_vel = 0;
  }
}

// Lock the active digit in, bounce it, and move to the next queued digit.
static void tetFinishActiveDigit() {
  updateDisplayedTimeDigit(tet_active, tet_active_val);
  if (settings.tetrisDigitBounce) triggerDigitBounce(tet_active);
  tet_seq_pos++;
  if (tet_seq_pos < tet_seq_len) {
    tetStartDigitAnim(tet_seq_pos);
  } else {
    tet_active = -1;  // whole sequence done
  }
}

static void tetUpdateSlab() {
  if (tet_slab >= 3) return;
  float accel = (settings.tetrisFallSpeed / 10.0f) * 0.067f;
  if (accel < 0.016f) accel = 0.016f;

  tet_slab_vel += accel;
  tet_slab_off += tet_slab_vel;
  if (tet_slab_off >= 0) {
    tet_slab_off = 0;
    tet_slab++;
    if (tet_slab >= 3) {
      tetFinishActiveDigit();
    } else {
      tet_slab_off = -TET_START_FALL;
      tet_slab_vel = 0;
    }
  }
}

static void tetUpdateDots() {
  dot_frame++;
  float vy = (settings.tetrisDotSpeed / 10.0f) * 0.8f;
  if (vy < 0.24f) vy = 0.24f;

  bool allDone = true;
  for (int k = 0; k < dot_n; k++) {
    if (dot_frame < dot_delay[k]) { allDone = false; continue; }  // not released yet
    if (dot_locked[k]) continue;
    dot_cy[k] += vy;
    if (dot_cy[k] >= dot_ty[k]) {
      dot_cy[k] = dot_ty[k];
      dot_locked[k] = true;
    } else {
      allDone = false;
    }
  }
  if (allDone) tetFinishActiveDigit();
}

static void updateTetrisAnimation(struct tm *timeinfo) {
  unsigned long now = millis();
  updateDigitBounce();
  if (now - last_tetris_update < TET_ANIM_SPEED) return;
  last_tetris_update = now;

  int seconds = timeinfo->tm_sec;
  int minute = timeinfo->tm_min;
  if (minute != last_minute_tetris) {
    last_minute_tetris = minute;
    tetris_triggered = false;
  }

  if (seconds >= TET_TRIGGER_SECOND && !tetris_triggered && !tetris_transitioning) {
    tetris_triggered = true;
    calculateTargetDigits(displayed_hour, displayed_min, displayed_is_pm);

    // Build the rebuild queue (changed digits, left to right; skip the colon).
    tet_seq_len = 0;
    for (int t = 0; t < num_targets; t++) {
      int idx = target_digit_index[t];
      if (idx == 2) continue;
      tet_seq_idx[tet_seq_len] = idx;
      tet_seq_val[tet_seq_len] = target_digit_values[t];
      tet_seq_len++;
    }

    if (tet_seq_len > 0) {
      tetris_transitioning = true;
      tet_seq_pos = 0;
      time_overridden = true;
      time_override_start = millis();
      tetStartDigitAnim(0);       // start the first digit
    }
  }

  if (tetris_transitioning) {
    if (tet_active >= 0) {
      if (settings.tetrisAnimStyle == 1) tetUpdateDots();
      else tetUpdateSlab();
    }
    tetUpdateFrags();
    if (tet_active < 0 && !tetAnyFragActive()) {
      tetris_transitioning = false;
    }
  }

  // The idle game runs in the bottom well, independent of the clock change.
  if (settings.tetrisIdleTumble) tetGameUpdate();
}

void resetTetrisAnimation() {
  tet_seq_len = 0;
  tet_seq_pos = 0;
  tet_active = -1;
  tet_active_val = 0;
  tet_slab = 3;
  tet_slab_off = 0;
  tet_slab_vel = 0;
  dot_n = 0;
  dot_frame = 0;
  for (int i = 0; i < TET_MAX_FRAG; i++) tet_frags[i].active = false;
  tetris_transitioning = false;
  tetris_triggered = false;
  last_minute_tetris = -1;
  tetGameReset();
  tetris_init_done = true;
}

bool tetrisIsAnimating() {
  return tetris_transitioning || settings.tetrisIdleTumble;
}

static void tetGameDraw() {
  // Settled stack (full rows blink while clearing)
  for (int r = 0; r < TET_WELL_ROWS; r++) {
    if ((tet_clear_mask & (1 << r)) && (tet_clear_flash / 5) % 2 == 0) continue;
    uint32_t row = tet_well[r];
    if (!row) continue;
    for (int c = 0; c < TET_WELL_COLS; c++)
      if (row & (1u << c))
        display.fillRect(c * TET_WELL_CELL, TET_WELL_TOP + r * TET_WELL_CELL, 3, 3,
                         SPRITE_COLOR(COL_TET_I + tet_well_col[r][c]));
  }
  // The piece currently falling in (absolute screen Y, so it shows while
  // passing through the digit area on its way down)
  if (tet_game_phase == TG_MOVING) {
    const TetRot &p = TET_ROTS[tet_pc_drawRot];
    int pcCol = (int)(tet_pc_curCol + 0.5f);
    int pyTop = (int)(tet_pc_py + 0.5f);
    for (int k = 0; k < 4; k++) {
      int cx = pcCol + p.cx[k];
      int py = pyTop + p.cy[k] * TET_WELL_CELL;
      if (cx >= 0 && cx < TET_WELL_COLS && py >= 0 && py < SCREEN_HEIGHT)
        display.fillRect(cx * TET_WELL_CELL, py, 3, 3, SPRITE_COLOR(COL_TET_I + tet_pc_piece));
    }
  }
}

// Draw the active digit mid-rebuild, per the selected style.
static void tetDrawActiveDigit(int i) {
  int bs = tetBlockSize();
  if (settings.tetrisAnimStyle == 1) {
    // Falling dots: locked dots at rest, released dots mid-fall.
    for (int k = 0; k < dot_n; k++) {
      if (dot_frame < dot_delay[k]) continue;  // not yet appeared
      int y = dot_locked[k] ? dot_ty[k] : (int)dot_cy[k];
      display.fillRect(dot_tx[k], y, bs, bs, SPRITE_COLOR(COL_DIGITS));
    }
  } else {
    // Drop-in slabs: settled slabs at rest, the current slab falling in.
    for (int s = 0; s < tet_slab; s++) {
      int rs, re;
      tetSlabRows(s, rs, re);
      tetDrawCells(i, tet_active_val, rs, re, 0);
    }
    if (tet_slab < 3) {
      int rs, re;
      tetSlabRows(tet_slab, rs, re);
      tetDrawCells(i, tet_active_val, rs, re, (int)tet_slab_off);
    }
  }
}

// ========== Display ==========
void displayClockWithTetris() {
  if (!tetris_init_done) resetTetrisAnimation();

  struct tm timeinfo;
  if (!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    display.print(ntpSynced ? "Time Error" : "Syncing time...");
    return;
  }

  updateTetrisAnimation(&timeinfo);

  if (!time_overridden) syncDisplayedTime(&timeinfo);
  maintainTimeOverride(&timeinfo, !tetris_transitioning);

  // Date (optional; top or bottom) - hidden while the idle game is on
  if (tetDateShown()) {
    display.setTextSize(1);
    char dateStr[12];
    switch (settings.dateFormat) {
      case 0: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
      case 1: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900); break;
      case 2: sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday); break;
      case 3: sprintf(dateStr, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
    }
    display.setCursor((SCREEN_WIDTH - 60) / 2, tetDateY());
    display.print(dateStr);
  }
  drawMeridiemIndicator(110, 4, displayed_is_pm);

  // Digits
  int bs = tetBlockSize();
  for (int i = 0; i < 5; i++) {
    if (i == 2) {
      // Block colon
      if (shouldShowColon()) {
        int cx = (DIGIT_X[1] + 4 * TET_PITCH + DIGIT_X[3]) / 2;
        display.fillRect(cx, tetTimeY() + 2 * TET_PITCH, bs, bs, SPRITE_COLOR(COL_DIGITS));
        display.fillRect(cx, tetTimeY() + 4 * TET_PITCH, bs, bs, SPRITE_COLOR(COL_DIGITS));
      }
      continue;
    }

    if (tetris_transitioning && tet_active == i) {
      tetDrawActiveDigit(i);
    } else {
      // Settled digit (already-rebuilt, still-old, or idle) with bounce offset.
      tetDrawCells(i, getDisplayedDigitValue(i), 0, TET_GRID_H - 1, (int)digit_offset_y[i]);
    }
  }

  // Fragments from cleared digits (drop-in slab style)
  for (int i = 0; i < TET_MAX_FRAG; i++) {
    if (tet_frags[i].active) display.fillRect((int)tet_frags[i].x, (int)tet_frags[i].y, 2, 2, SPRITE_COLOR(COL_DIGITS));
  }

  // Idle Tetris game (bottom well)
  if (settings.tetrisIdleTumble) tetGameDraw();

  if (!wifiConnected) drawNoWiFiIcon(0, 0);
}
