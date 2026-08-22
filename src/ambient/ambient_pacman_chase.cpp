/*
 * AnimatedPixelClock - Ambient: Pac-Man maze
 *
 * A self-playing game of Pac-Man that fills the whole 128x64 panel (no clock,
 * no digits). The maze is a lattice of pillars with an open perimeter loop, so
 * Pac always has an escape route. Pac plays like a real person: he heads for the
 * nearest pellet when it is safe but swerves away (and reverses) from any ghost
 * that gets close, and he turns to hunt the blue ghosts after eating a power
 * pellet. The four ghosts run classic scatter/chase waves with individual
 * personalities (Blinky chases directly, Pinky aims ahead of Pac, Inky mirrors
 * across Blinky, Clyde only closes in from a distance), so they spread out and
 * corner Pac only rarely instead of dogpiling him. Getting caught costs a life
 * (shrink, then the board resets with the dots intact); clearing every dot lays
 * out a fresh board.
 *
 * Pac-Man is reused from the Pac-Man clock (drawPacman, follows COL_PACMAN).
 * Ghosts and the maze are drawn here in hardcoded RGB565.
 */

#include "ambient.h"

#include "../config/config.h"
#include "../display/display.h"
#include "../clocks/clocks.h"

#define MZ_COLS 15
#define MZ_ROWS 7
#define MZ_CELL 8           // px per cell (15*8 = 120, 7*8 = 56)
#define MZ_OX 4             // origin inset so the maze centers inside a thin
#define MZ_OY 4             // outer frame instead of bleeding to the panel edge
#define GHOST_COUNT 4

// Speeds (px/sec). Pac keeps a clear edge over the ghosts so a smart player can
// outrun them along the open perimeter.
#define SPEED_PAC 48.0f
#define SPEED_GHOST 36.0f
#define SPEED_FRIGHT 24.0f
#define SPEED_EYES 104.0f

// Pac's brain. When the nearest hostile ghost is within SCARE_RANGE he drops
// pellet-seeking and just maximizes distance from it (FLEE_W per cell), so he
// never steps toward a chaser. A straight-course bonus and a stiff reverse cost
// keep him committing to corridors instead of jittering back and forth between
// two cells, and he detours to eat any frightened ghost within HUNT_RANGE.
#define SCARE_RANGE 4
#define FLEE_W 10.0f
#define STRAIGHT_BONUS 2.0f
#define REVERSE_PEN 5.0f
#define HUNT_RANGE 8

// Scatter/chase wave lengths (ms).
#define SCATTER_MS 6000
#define CHASE_MS 18000

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

// Each ghost's scatter corner (also Clyde's bail-out target when he crowds Pac).
static const int8_t SCATTER_C[GHOST_COUNT] = {MZ_COLS - 1, 0, MZ_COLS - 1, 0};
static const int8_t SCATTER_R[GHOST_COUNT] = {0, 0, MZ_ROWS - 1, MZ_ROWS - 1};

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

static uint8_t ghostPhase = 0;    // 0 = scatter, 1 = chase
static unsigned long phaseUntil = 0;

static uint8_t mouthFrame = 0;
static uint8_t skirtFrame = 0;
static bool mzInit = false;
static unsigned long lastFrame = 0, lastMouth = 0, lastSkirt = 0;

static inline int ccx(int col) { return MZ_OX + col * MZ_CELL + MZ_CELL / 2; }
static inline int ccy(int row) { return MZ_OY + row * MZ_CELL + MZ_CELL / 2; }

// Interior pillar lattice at even/even cells (2..COLS-2, 2..ROWS-2). The whole
// perimeter is open corridor, so there are no dead ends and Pac can loop the
// outside to shake a chaser.
static bool isWall(int c, int r) {
  if (c < 0 || c >= MZ_COLS || r < 0 || r >= MZ_ROWS) return true;
  return (c >= 2 && c <= MZ_COLS - 2 && r >= 2 && r <= MZ_ROWS - 2
          && (c % 2 == 0) && (r % 2 == 0));
}

// Power pellets sit in the four corners, classic-style.
static const struct { int8_t c, r; } POWER_CELLS[4] = {
  {0, 0}, {MZ_COLS - 1, 0}, {0, MZ_ROWS - 1}, {MZ_COLS - 1, MZ_ROWS - 1},
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
  placeActor(pac, 1, MZ_ROWS - 1, 1, 0);
  pacLastDx = 1; pacLastDy = 0;
  const int8_t gc[GHOST_COUNT] = {4, 6, 8, 10};
  for (int i = 0; i < GHOST_COUNT; i++) {
    placeActor(gh[i], gc[i], PEN_ROW, (i & 1) ? 1 : -1, 0);
    ghMode[i] = G_NORMAL;
  }
  powerActive = false;
  ghostPhase = 0;                       // open on a scatter wave: Pac gets room
  phaseUntil = millis() + SCATTER_MS;
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

static void choosePacDir(Actor& a) {
  // Distance to the nearest hostile (normal) ghost from where Pac stands now.
  int threat = 999;
  for (int g = 0; g < GHOST_COUNT; g++) {
    if (ghMode[g] != G_NORMAL) continue;
    int d = abs(gh[g].col - a.col) + abs(gh[g].row - a.row);
    if (d < threat) threat = d;
  }
  bool scared = (threat <= SCARE_RANGE);

  int8_t bestDx = a.dx, bestDy = a.dy;
  float bestScore = 1e9f;
  bool found = false;
  int order[4] = {0, 1, 2, 3};
  for (int i = 3; i > 0; i--) { int j = random(i + 1); int t = order[i]; order[i] = order[j]; order[j] = t; }
  for (int k = 0; k < 4; k++) {
    int8_t dx = DIRS[order[k]][0], dy = DIRS[order[k]][1];
    int nc = a.col + dx, nr = a.row + dy;
    if (isWall(nc, nr)) continue;

    float score;
    if (scared) {
      // Survival: get as far from the nearest hostile ghost as possible. Moving
      // toward one shrinks the distance and scores worst, so Pac won't walk in.
      int mind = 999;
      for (int g = 0; g < GHOST_COUNT; g++) {
        if (ghMode[g] != G_NORMAL) continue;
        int d = abs(gh[g].col - nc) + abs(gh[g].row - nr);
        if (d < mind) mind = d;
      }
      score = -mind * FLEE_W + nearestDotDist(nc, nr) * 0.2f;   // pellets break ties
    } else {
      score = (float)nearestDotDist(nc, nr);                    // free to feed
    }
    // Detour onto frightened ghosts to eat them, scared of the others or not.
    for (int g = 0; g < GHOST_COUNT; g++) {
      if (ghMode[g] != G_FRIGHT) continue;
      int d = abs(gh[g].col - nc) + abs(gh[g].row - nr);
      if (d < HUNT_RANGE) score -= (HUNT_RANGE - d) * 2.0f;
    }
    // Hold the corridor: reward staying straight, make a U-turn a last resort.
    // This is what stops the few-pixel back-and-forth jitter.
    if (dx == a.dx && dy == a.dy) score -= STRAIGHT_BONUS;
    else if (dx == -a.dx && dy == -a.dy) score += REVERSE_PEN;

    if (score < bestScore) { bestScore = score; bestDx = dx; bestDy = dy; found = true; }
  }
  if (!found) { bestDx = -a.dx; bestDy = -a.dy; }   // fully boxed: turn around
  a.dx = bestDx; a.dy = bestDy;
  a.tcol = a.col + bestDx; a.trow = a.row + bestDy;
}

// Ghost greedily heads for a target cell (or flees it when frightened), never
// reversing unless boxed.
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

// The cell a ghost is currently aiming at, per mode/phase/personality.
static void ghostTarget(int i, int& tc, int& tr) {
  if (ghMode[i] == G_EYES) { tc = PEN_COL; tr = PEN_ROW; return; }
  if (ghMode[i] == G_FRIGHT) { tc = pac.col; tr = pac.row; return; }  // dir chooser flees it
  if (ghostPhase == 0) { tc = SCATTER_C[i]; tr = SCATTER_R[i]; return; }

  int pdx = pacLastDx, pdy = pacLastDy;
  switch (i) {
    case 0:  tc = pac.col;            tr = pac.row;            break;  // Blinky: direct
    case 1:  tc = pac.col + 4 * pdx;  tr = pac.row + 4 * pdy;  break;  // Pinky: ambush ahead
    case 2:  tc = 2 * pac.col - gh[0].col; tr = 2 * pac.row - gh[0].row; break;  // Inky: mirror Blinky
    default: {                                                          // Clyde: shy
      int cd = abs(gh[3].col - pac.col) + abs(gh[3].row - pac.row);
      if (cd > 6) { tc = pac.col; tr = pac.row; }
      else { tc = SCATTER_C[3]; tr = SCATTER_R[3]; }
    }
  }
  if (tc < 0) tc = 0; else if (tc >= MZ_COLS) tc = MZ_COLS - 1;
  if (tr < 0) tr = 0; else if (tr >= MZ_ROWS) tr = MZ_ROWS - 1;
}

// Move an actor along its heading. Returns -1 while still travelling, or the
// leftover distance (>= 0) on the frame it lands on the next cell center. The
// caller picks a new heading and spends that leftover along it, so no travel is
// dropped at a cell boundary - otherwise the arrival frame under-moves and the
// crossings beat against the frame rate as a periodic stutter.
static float advanceActor(Actor& a, float speed, float dt) {
  float tx = ccx(a.tcol), ty = ccy(a.trow);
  float remain = fabsf(tx - a.x) + fabsf(ty - a.y);
  float move = speed * dt;
  if (move >= remain) {
    a.x = tx; a.y = ty;
    a.col = a.tcol; a.row = a.trow;
    return move - remain;
  }
  a.x += a.dx * move;
  a.y += a.dy * move;
  return -1.0f;
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
    // Scatter/chase wave clock: in scatter the ghosts peel off to their corners,
    // which is what gives Pac (and a human) room to breathe.
    if (now >= phaseUntil) {
      ghostPhase ^= 1;
      phaseUntil = now + (ghostPhase ? CHASE_MS : SCATTER_MS);
    }

    // ---- Pac-Man ----
    float over = advanceActor(pac, SPEED_PAC, dt);
    if (over >= 0.0f) {
      if (power[pac.row][pac.col]) {
        power[pac.row][pac.col] = false;
        powerActive = true;
        powerUntil = now + 6000;
        for (int i = 0; i < GHOST_COUNT; i++) if (ghMode[i] != G_EYES) ghMode[i] = G_FRIGHT;
      }
      if (dot[pac.row][pac.col]) { dot[pac.row][pac.col] = false; dotsLeft--; }
      if (dotsLeft == 0) { layoutBoard(); dot[pac.row][pac.col] = false; }
      choosePacDir(pac);
      pac.x += pac.dx * over; pac.y += pac.dy * over;   // spend the carry on the new heading
    }
    if (pac.dx || pac.dy) { pacLastDx = pac.dx; pacLastDy = pac.dy; }

    // ---- Ghosts ----
    for (int i = 0; i < GHOST_COUNT; i++) {
      float sp = (ghMode[i] == G_EYES) ? SPEED_EYES
                 : (ghMode[i] == G_FRIGHT) ? SPEED_FRIGHT : SPEED_GHOST;
      float gover = advanceActor(gh[i], sp, dt);
      if (gover >= 0.0f) {
        if (ghMode[i] == G_EYES && gh[i].col == PEN_COL && gh[i].row == PEN_ROW) {
          ghMode[i] = G_NORMAL;
        }
        int tc, tr;
        ghostTarget(i, tc, tr);
        chooseGhostDir(gh[i], ghMode[i], tc, tr);
        gh[i].x += gh[i].dx * gover; gh[i].y += gh[i].dy * gover;
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
  // Maze: thin outer frame (inset 1px so the perimeter corridor sprites clear
  // it) + interior pillar lattice.
  display.drawRect(1, 1, 126, 62, COL_WALL);
  for (int r = 2; r <= MZ_ROWS - 2; r += 2) {
    for (int c = 2; c <= MZ_COLS - 2; c += 2) {
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
