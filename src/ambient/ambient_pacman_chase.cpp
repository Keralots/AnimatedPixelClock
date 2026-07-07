/*
 * AnimatedPixelClock - Ambient: Pac-Man maze
 *
 * A self-playing game of Pac-Man on a 16x8 pillar maze (no clock, no digits).
 * Pac-Man navigates the corridors greedily toward the nearest dot, turning
 * corners like a real player; four ghosts chase him through the maze. Eating a
 * power pellet turns the ghosts blue and sends them fleeing while Pac hunts
 * them into a pair of retreating eyes that scurry back to the pen. Getting
 * caught costs Pac a life (shrink, then the board resets with the dots intact).
 * When every dot is gone a fresh board is laid out.
 *
 * Pac-Man is reused from the Pac-Man clock (drawPacman, follows COL_PACMAN).
 * Ghosts and the maze are drawn here in hardcoded RGB565.
 */

#include "ambient.h"

#include "../config/config.h"
#include "../display/display.h"
#include "../clocks/clocks.h"

#define MZ_COLS 16
#define MZ_ROWS 8
#define MZ_CELL 8           // px per cell (16*8 = 128, 8*8 = 64)
#define GHOST_COUNT 4

// Speeds (px/sec)
#define SPEED_PAC 44.0f
#define SPEED_GHOST 40.0f
#define SPEED_FRIGHT 26.0f
#define SPEED_EYES 104.0f

// Colors (RGB565)
static const uint16_t GHOST_COLORS[GHOST_COUNT] = {
  0xF800,  // Blinky red
  0xFDB8,  // Pinky pink
  0x07FF,  // Inky cyan
  0xFD20,  // Clyde orange
};
#define COL_WALL   0x021F    // maze blue
#define COL_FRIGHT 0x001F    // frightened blue
#define COL_FRIGHT2 0xFFFF   // frightened blink (near timeout)
#define COL_EYE 0xFFFF
#define COL_PUPIL 0x001F
#define COL_DOT 0xFED7       // pellet peach
#define COL_POWER 0xFED7

// Pen (ghost home) cell that eaten eyes return to.
#define PEN_COL 7
#define PEN_ROW 3

enum GMode { G_NORMAL, G_FRIGHT, G_EYES };

struct Actor {
  float x, y;         // pixel center
  int8_t dx, dy;      // heading (-1/0/1, axis-aligned)
  uint8_t col, row;   // cell last centered on
  uint8_t tcol, trow; // cell being moved into
};

static Actor pac;
static int8_t pacLastDx = 1, pacLastDy = 0;
static Actor gh[GHOST_COUNT];
static uint8_t ghMode[GHOST_COUNT];

static bool dot[MZ_ROWS][MZ_COLS];
static bool power[MZ_ROWS][MZ_COLS];
static uint16_t dotsLeft = 0;

static unsigned long powerUntil = 0;
static bool powerActive = false;
static uint16_t deathTimer = 0;   // frames of the caught animation (0 = playing)

static uint8_t mouthFrame = 0;
static uint8_t skirtFrame = 0;
static bool mzInit = false;
static unsigned long lastFrame = 0, lastMouth = 0, lastSkirt = 0;

static inline int ccx(int col) { return col * MZ_CELL + MZ_CELL / 2; }
static inline int ccy(int row) { return row * MZ_CELL + MZ_CELL / 2; }

// Border walls + interior pillars at even/even cells. No dead ends, all loops.
static bool isWall(int c, int r) {
  if (c < 0 || c >= MZ_COLS || r < 0 || r >= MZ_ROWS) return true;
  if (c == 0 || c == MZ_COLS - 1 || r == 0 || r == MZ_ROWS - 1) return true;
  return (c % 2 == 0) && (r % 2 == 0);
}

static const struct { int8_t c, r; } POWER_CELLS[4] = {
  {1, 1}, {14, 1}, {1, 6}, {14, 5},
};

static void layoutBoard() {
  dotsLeft = 0;
  for (int r = 0; r < MZ_ROWS; r++) {
    for (int c = 0; c < MZ_COLS; c++) {
      dot[r][c] = false;
      power[r][c] = false;
      if (!isWall(c, r)) { dot[r][c] = true; dotsLeft++; }
    }
  }
  for (auto& p : POWER_CELLS) {
    if (!isWall(p.c, p.r)) { power[p.r][p.c] = true; dot[p.r][p.c] = false; }
  }
}

static void placeActor(Actor& a, int c, int r, int8_t dx, int8_t dy) {
  a.col = a.tcol = c;
  a.row = a.trow = r;
  a.x = ccx(c);
  a.y = ccy(r);
  a.dx = dx;
  a.dy = dy;
}

static void resetPositions() {
  placeActor(pac, 7, 6, -1, 0);
  pacLastDx = -1; pacLastDy = 0;
  const int8_t gc[GHOST_COUNT] = {5, 7, 9, 11};
  for (int i = 0; i < GHOST_COUNT; i++) {
    placeActor(gh[i], gc[i], PEN_ROW, (i & 1) ? 1 : -1, 0);
    ghMode[i] = G_NORMAL;
  }
  powerActive = false;
}

// Manhattan distance from (c,r) to the nearest remaining dot/power cell.
static int nearestDotDist(int c, int r) {
  int best = 9999;
  for (int rr = 0; rr < MZ_ROWS; rr++) {
    for (int cc = 0; cc < MZ_COLS; cc++) {
      if (dot[rr][cc] || power[rr][cc]) {
        int d = abs(cc - c) + abs(rr - r);
        if (d < best) best = d;
      }
    }
  }
  return best;
}

static const int8_t DIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

// Pac heads toward the nearest dot, never reversing unless boxed in.
static void choosePacDir(Actor& a) {
  int8_t bestDx = a.dx, bestDy = a.dy;
  int bestScore = 99999;
  bool found = false;
  int order[4] = {0, 1, 2, 3};
  for (int i = 3; i > 0; i--) { int j = random(i + 1); int t = order[i]; order[i] = order[j]; order[j] = t; }
  for (int k = 0; k < 4; k++) {
    int8_t dx = DIRS[order[k]][0], dy = DIRS[order[k]][1];
    if (dx == -a.dx && dy == -a.dy) continue;       // no reversing
    int nc = a.col + dx, nr = a.row + dy;
    if (isWall(nc, nr)) continue;
    int score = nearestDotDist(nc, nr);
    if (score < bestScore) { bestScore = score; bestDx = dx; bestDy = dy; found = true; }
  }
  if (!found) { bestDx = -a.dx; bestDy = -a.dy; }   // dead end: turn around
  a.dx = bestDx; a.dy = bestDy;
  a.tcol = a.col + bestDx; a.trow = a.row + bestDy;
}

// Ghost greedily chases (or flees) a target cell, never reversing unless boxed.
static void chooseGhostDir(Actor& a, uint8_t mode, int tc, int tr) {
  int8_t bestDx = a.dx, bestDy = a.dy;
  int bestScore = (mode == G_FRIGHT) ? -1 : 99999;
  bool found = false;
  for (int k = 0; k < 4; k++) {
    int8_t dx = DIRS[k][0], dy = DIRS[k][1];
    if (dx == -a.dx && dy == -a.dy) continue;
    int nc = a.col + dx, nr = a.row + dy;
    if (isWall(nc, nr)) continue;
    int d = abs(nc - tc) + abs(nr - tr);
    if (mode == G_FRIGHT) {
      if (d > bestScore) { bestScore = d; bestDx = dx; bestDy = dy; found = true; }
    } else {
      if (d < bestScore) { bestScore = d; bestDx = dx; bestDy = dy; found = true; }
    }
  }
  if (!found) { bestDx = -a.dx; bestDy = -a.dy; }
  a.dx = bestDx; a.dy = bestDy;
  a.tcol = a.col + bestDx; a.trow = a.row + bestDy;
}

// Move an actor along its heading; return true when it reaches the next cell
// center (a decision point). Heading is axis-aligned so the remaining distance
// is along one axis.
static bool advanceActor(Actor& a, float speed, float dt) {
  float tx = ccx(a.tcol), ty = ccy(a.trow);
  float remain = fabsf(tx - a.x) + fabsf(ty - a.y);
  float move = speed * dt;
  if (move >= remain) {
    a.x = tx; a.y = ty;
    a.col = a.tcol; a.row = a.trow;
    return true;
  }
  a.x += a.dx * move;
  a.y += a.dy * move;
  return false;
}

static void drawGhost(int cx, int cy, uint8_t mode, uint16_t bodyCol, int faceDir) {
  int sx = cx - 3, sy = cy - 4;  // 7x8
  if (mode == G_EYES) {
    display.fillRect(sx + 1, sy + 2, 2, 2, COL_EYE);
    display.fillRect(sx + 4, sy + 2, 2, 2, COL_EYE);
    int px = (faceDir >= 0) ? 1 : 0;
    display.drawPixel(sx + 1 + px, sy + 3, COL_PUPIL);
    display.drawPixel(sx + 4 + px, sy + 3, COL_PUPIL);
    return;
  }
  display.fillRect(sx + 1, sy, 5, 1, bodyCol);
  display.fillRect(sx, sy + 1, 7, 5, bodyCol);
  if (skirtFrame == 0) {
    display.drawPixel(sx + 0, sy + 6, bodyCol);
    display.drawPixel(sx + 2, sy + 6, bodyCol);
    display.drawPixel(sx + 4, sy + 6, bodyCol);
    display.drawPixel(sx + 6, sy + 6, bodyCol);
  } else {
    display.drawPixel(sx + 1, sy + 6, bodyCol);
    display.drawPixel(sx + 3, sy + 6, bodyCol);
    display.drawPixel(sx + 5, sy + 6, bodyCol);
  }
  if (mode == G_FRIGHT) {
    display.drawPixel(sx + 1, sy + 2, COL_EYE);
    display.drawPixel(sx + 5, sy + 2, COL_EYE);
    display.drawPixel(sx + 1, sy + 4, COL_EYE);
    display.drawPixel(sx + 3, sy + 4, COL_EYE);
    display.drawPixel(sx + 5, sy + 4, COL_EYE);
  } else {
    display.fillRect(sx + 1, sy + 2, 2, 2, COL_EYE);
    display.fillRect(sx + 4, sy + 2, 2, 2, COL_EYE);
    int px = (faceDir >= 0) ? 1 : 0;
    display.drawPixel(sx + 1 + px, sy + 3, COL_PUPIL);
    display.drawPixel(sx + 4 + px, sy + 3, COL_PUPIL);
  }
}

static int pacDirCode() {
  if (pacLastDx > 0) return 1;
  if (pacLastDx < 0) return -1;
  if (pacLastDy > 0) return 2;
  return -2;
}

void ambientPacmanChaseFrame() {
  unsigned long now = millis();
  if (!mzInit) {
    layoutBoard();
    resetPositions();
    dot[pac.row][pac.col] = false;  // clear Pac's start cell
    lastFrame = now; lastMouth = now; lastSkirt = now;
    mzInit = true;
  }

  float dt = (now - lastFrame) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  lastFrame = now;
  if (now - lastMouth > 90) { mouthFrame = (mouthFrame + 1) % 4; lastMouth = now; }
  if (now - lastSkirt > 180) { skirtFrame ^= 1; lastSkirt = now; }

  if (powerActive && now >= powerUntil) {
    powerActive = false;
    for (int i = 0; i < GHOST_COUNT; i++) if (ghMode[i] == G_FRIGHT) ghMode[i] = G_NORMAL;
  }

  if (deathTimer > 0) {
    // Caught: play the shrink, then reset the board (dots survive).
    deathTimer--;
    if (deathTimer == 0) resetPositions();
  } else {
    // ---- Pac-Man ----
    if (advanceActor(pac, SPEED_PAC, dt)) {
      if (power[pac.row][pac.col]) {
        power[pac.row][pac.col] = false;
        powerActive = true;
        powerUntil = now + 6000;
        for (int i = 0; i < GHOST_COUNT; i++) if (ghMode[i] != G_EYES) ghMode[i] = G_FRIGHT;
      }
      if (dot[pac.row][pac.col]) { dot[pac.row][pac.col] = false; dotsLeft--; }
      if (dotsLeft == 0) { layoutBoard(); dot[pac.row][pac.col] = false; }
      choosePacDir(pac);
    }
    if (pac.dx || pac.dy) { pacLastDx = pac.dx; pacLastDy = pac.dy; }

    // ---- Ghosts ----
    for (int i = 0; i < GHOST_COUNT; i++) {
      float sp = (ghMode[i] == G_EYES) ? SPEED_EYES
                 : (ghMode[i] == G_FRIGHT) ? SPEED_FRIGHT : SPEED_GHOST;
      if (advanceActor(gh[i], sp, dt)) {
        if (ghMode[i] == G_EYES && gh[i].col == PEN_COL && gh[i].row == PEN_ROW) {
          ghMode[i] = G_NORMAL;
        }
        int tc, tr;
        if (ghMode[i] == G_EYES) { tc = PEN_COL; tr = PEN_ROW; }
        else { tc = pac.col; tr = pac.row; }
        chooseGhostDir(gh[i], ghMode[i], tc, tr);
      }
    }

    // ---- Collisions ----
    for (int i = 0; i < GHOST_COUNT; i++) {
      if (ghMode[i] == G_EYES) continue;
      float d = fabsf(gh[i].x - pac.x) + fabsf(gh[i].y - pac.y);
      if (d < 5.0f) {
        if (ghMode[i] == G_FRIGHT) {
          ghMode[i] = G_EYES;
        } else {
          deathTimer = 32;  // ~1s caught animation
          break;
        }
      }
    }
  }

  // ================= Draw =================
  // Maze: border frame + interior pillars.
  display.drawRect(MZ_CELL - 1, MZ_CELL - 1,
                   (MZ_COLS - 2) * MZ_CELL + 2, (MZ_ROWS - 2) * MZ_CELL + 2, COL_WALL);
  for (int r = 2; r < MZ_ROWS - 1; r += 2) {
    for (int c = 2; c < MZ_COLS - 1; c += 2) {
      display.fillRect(ccx(c) - 2, ccy(r) - 2, 5, 5, COL_WALL);
    }
  }

  // Dots + power pellets.
  for (int r = 0; r < MZ_ROWS; r++) {
    for (int c = 0; c < MZ_COLS; c++) {
      if (dot[r][c]) display.drawPixel(ccx(c), ccy(r), COL_DOT);
      else if (power[r][c] && (now / 250) % 2 == 0) {
        display.fillRect(ccx(c) - 1, ccy(r) - 1, 3, 3, COL_POWER);
      }
    }
  }

  // Ghosts.
  bool blink = powerActive && (powerUntil - now < 1500) && ((now / 200) % 2 == 0);
  for (int i = 0; i < GHOST_COUNT; i++) {
    uint16_t col = (ghMode[i] == G_FRIGHT) ? (blink ? COL_FRIGHT2 : COL_FRIGHT)
                                           : GHOST_COLORS[i];
    int faceDir = (gh[i].dx >= 0) ? 1 : -1;
    drawGhost((int)gh[i].x, (int)gh[i].y, ghMode[i], col, faceDir);
  }

  // Pac-Man (shrinks during the caught animation).
  if (deathTimer > 0) {
    int rad = (deathTimer * 4) / 32;  // 4 -> 0
    if (rad > 0) display.fillCircle((int)pac.x, (int)pac.y, rad, SPRITE_COLOR(COL_PACMAN));
  } else {
    drawPacman((int)pac.x, (int)pac.y, pacDirCode(), mouthFrame);
  }
}
