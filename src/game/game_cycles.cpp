/*
 * AnimatedPixelClock - Light Cycles (game mode)
 *
 * The player's cycle against one driven by the clock, in a walled arena
 * under the top band. Both leave a solid trail; running into a wall or any
 * trail ends the round. Win a round and the next one is faster and the
 * computer plays harder; lose three and the game is over. Running into the
 * same cell at once is a draw and the round is replayed.
 *
 * The computer looks at how much open floor each turn leaves it (a capped
 * flood fill), so it does not drive into a pocket it cannot leave. It also
 * steers to cut in front of the player, harder each round, and in the first
 * three rounds it now and then makes a careless turn.
 *
 * Colours follow the TRON clock's two cycle colours.
 *
 * Controls: d-pad / left stick steer.
 */

#include "../config/user_config.h"

#if GAMEPAD_ENABLED

#include "../config/config.h"
#include "../display/display.h"
#include "game_mode.h"
#include "game_common.h"

// Controls card on the READY screen.
static const char HELP[] = "\x18\x19\x1b\x1a steer\ntrap it, don't crash";

#define LC_W 63  // cells; a cell is 2px, its trail pixel sits on the even column/row
#define LC_H 25
#define LC_X0 1
#define LC_Y0 13
#define LC_LIVES 3
#define LC_START_MS 75
#define LC_MIN_MS 40
#define LC_SPEEDUP_MS 5
#define LC_BANNER_S 1.1f
#define LC_CRASH_S 1.4f
#define LC_AREA_CAP 400  // flood fill stops counting here: past it, every turn is roomy

// Cell bits: owner, and whether the trail continues to the right / downward
// neighbour, so the trails draw as unbroken lines with a gap between lanes.
#define CELL_OWNER 0x03
#define CELL_RIGHT 0x04
#define CELL_DOWN 0x08

static const int8_t DX[4] = {1, 0, -1, 0};
static const int8_t DY[4] = {0, 1, 0, -1};

struct Cycle {
  int8_t x, y, dir;
  bool crashed;
};

static uint8_t grid[LC_H][LC_W];
static Cycle cyc[2];  // 0 = player, 1 = computer
static int8_t queued[2];
static uint8_t queuedCount;
static uint16_t stepMs;
static float stepAcc;
static float gt, bannerUntil, crashUntil;
static uint8_t lives, round_;
static uint32_t score, hiScore;
static unsigned long lastTick;
static GamePhase phase;
static bool newHi;
static int8_t outcome;  // after a crash: 1 won, -1 lost, 0 draw

// Flood fill scratch.
static uint16_t fillQueue[LC_AREA_CAP];
static uint32_t fillSeen[LC_H][2];

static uint16_t cycleColor(int n) { return SPRITE_COLOR(n == 0 ? COL_TRON_BLUE : COL_TRON_ORANGE); }

static uint16_t dim(uint16_t c, int level) {
  return ((((c >> 11) & 31) * level / 255) << 11) | ((((c >> 5) & 63) * level / 255) << 5) |
         ((c & 31) * level / 255);
}

static bool blocked(int x, int y) {
  return x < 0 || x >= LC_W || y < 0 || y >= LC_H || (grid[y][x] & CELL_OWNER);
}

static void occupy(int n, int fromX, int fromY, int dir) {
  Cycle &c = cyc[n];
  grid[c.y][c.x] |= n + 1;
  if (dir == 0) grid[fromY][fromX] |= CELL_RIGHT;
  else if (dir == 2) grid[c.y][c.x] |= CELL_RIGHT;
  else if (dir == 1) grid[fromY][fromX] |= CELL_DOWN;
  else grid[c.y][c.x] |= CELL_DOWN;
}

static void startRound() {
  memset(grid, 0, sizeof(grid));
  cyc[0] = {6, LC_H / 2, 0, false};
  cyc[1] = {LC_W - 7, LC_H / 2, 2, false};
  for (int n = 0; n < 2; n++) grid[cyc[n].y][cyc[n].x] = n + 1;
  queuedCount = 0;
  stepMs = max(LC_MIN_MS, LC_START_MS - (round_ - 1) * LC_SPEEDUP_MS);
  stepAcc = 0;
  bannerUntil = gt + LC_BANNER_S;
  crashUntil = 0;
}

void cyclesReset() {
  hiScore = gameLoadHi("cyclesHi");
  gt = 0;
  score = 0;
  lives = LC_LIVES;
  round_ = 1;
  newHi = false;
  startRound();
  phase = G_READY;
}

static void queueTurn(int8_t d) {
  int8_t last = queuedCount ? queued[queuedCount - 1] : cyc[0].dir;
  if (d == last || d == (last + 2) % 4 || queuedCount >= 2) return;
  queued[queuedCount++] = d;
}

// Open cells reachable from (x, y), capped at LC_AREA_CAP.
static int floodArea(int x, int y) {
  memset(fillSeen, 0, sizeof(fillSeen));
  int head = 0, tail = 0;
  fillSeen[y][x >> 5] |= 1u << (x & 31);
  fillQueue[tail++] = y * LC_W + x;
  while (head < tail && tail < LC_AREA_CAP) {
    int cx = fillQueue[head] % LC_W, cy = fillQueue[head] / LC_W;
    head++;
    for (int d = 0; d < 4 && tail < LC_AREA_CAP; d++) {
      int nx = cx + DX[d], ny = cy + DY[d];
      if (blocked(nx, ny) || (fillSeen[ny][nx >> 5] & (1u << (nx & 31)))) continue;
      fillSeen[ny][nx >> 5] |= 1u << (nx & 31);
      fillQueue[tail++] = ny * LC_W + nx;
    }
  }
  return tail;
}

static int straightRun(int x, int y, int d) {
  int n = 0;
  for (; n < 12 && !blocked(x + DX[d] * (n + 1), y + DY[d] * (n + 1)); n++) {
  }
  return n;
}

static void steerComputer() {
  Cycle &me = cyc[1];
  const Cycle &you = cyc[0];
  int options[3] = {me.dir, (me.dir + 1) % 4, (me.dir + 3) % 4};
  int safe[3], safeCount = 0;
  for (int d : options)
    if (!blocked(me.x + DX[d], me.y + DY[d])) safe[safeCount++] = d;
  if (!safeCount) return;  // boxed in: drive on and crash

  // Early rounds: now and then a turn without looking ahead.
  int careless = round_ < 4 ? 4 - round_ : 0;
  if ((int)(esp_random() % 100) < careless) {
    me.dir = safe[esp_random() % safeCount];
    return;
  }

  int aggression = min((int)round_, 5);
  int tx = you.x + DX[you.dir] * 4, ty = you.y + DY[you.dir] * 4;
  int best = INT32_MIN, bestDir = safe[0];
  for (int i = 0; i < safeCount; i++) {
    int d = safe[i];
    int nx = me.x + DX[d], ny = me.y + DY[d];
    int value = floodArea(nx, ny) * 16 + straightRun(nx, ny, d) * 3;
    value -= (abs(nx - tx) + abs(ny - ty)) * aggression;
    if (d == me.dir) value += 4;  // no needless zig-zag
    value += esp_random() % 4;
    if (value > best) {
      best = value;
      bestDir = d;
    }
  }
  me.dir = bestDir;
}

static void endRound(int8_t result) {
  outcome = result;
  crashUntil = gt + LC_CRASH_S;
  if (result > 0) {
    score += 100 * round_;
    gameRumble(40, 40, 200);
  } else if (result < 0) {
    lives--;
    gameRumble(100, 80, 450);
  } else {
    gameRumble(60, 60, 300);
  }
}

static void stepCycles() {
  if (queuedCount) {
    cyc[0].dir = queued[0];
    queued[0] = queued[1];
    queuedCount--;
  }
  steerComputer();

  int nx[2], ny[2];
  for (int n = 0; n < 2; n++) {
    nx[n] = cyc[n].x + DX[cyc[n].dir];
    ny[n] = cyc[n].y + DY[cyc[n].dir];
    cyc[n].crashed = blocked(nx[n], ny[n]);
  }
  if (nx[0] == nx[1] && ny[0] == ny[1]) cyc[0].crashed = cyc[1].crashed = true;

  if (cyc[0].crashed || cyc[1].crashed) {
    endRound(cyc[0].crashed == cyc[1].crashed ? 0 : cyc[0].crashed ? -1 : 1);
    return;
  }
  for (int n = 0; n < 2; n++) {
    int fx = cyc[n].x, fy = cyc[n].y;
    cyc[n].x = nx[n];
    cyc[n].y = ny[n];
    occupy(n, fx, fy, cyc[n].dir);
  }
  score++;
}

static void update(const GamepadState &in, float dt) {
  gt += dt;
  for (uint8_t i = 0; i < in.dirCount; i++) {
    uint16_t d = in.dirs[i];
    if (d == GP_UP) queueTurn(3);
    if (d == GP_DOWN) queueTurn(1);
    if (d == GP_LEFT) queueTurn(2);
    if (d == GP_RIGHT) queueTurn(0);
  }

  if (crashUntil) {
    if (gt < crashUntil) return;
    if (!lives) {
      phase = G_OVER;
      gameRumble(90, 70, 500);
      newHi = gameSubmitScore("cyclesHi", score);
      if (newHi) hiScore = score;
      return;
    }
    if (outcome > 0) round_++;
    startRound();
    return;
  }
  if (gt < bannerUntil) return;  // turns pressed now still count from the go

  stepAcc += dt * 1000;
  while (stepAcc >= stepMs && !crashUntil && phase == G_PLAY) {
    stepAcc -= stepMs;
    stepCycles();
  }
}

static void draw() {
  display.drawRect(0, LC_Y0 - 1, SCREEN_WIDTH, SCREEN_HEIGHT - LC_Y0 + 1, GC_DIM);

  // A crashed cycle's trail fades out while the result shows.
  int fade[2] = {255, 255};
  if (crashUntil)
    for (int n = 0; n < 2; n++)
      if (cyc[n].crashed) fade[n] = 60 + (int)(195 * max(0.0f, crashUntil - gt) / LC_CRASH_S);
  uint16_t trail[2] = {dim(cycleColor(0), fade[0]), dim(cycleColor(1), fade[1])};

  for (int y = 0; y < LC_H; y++)
    for (int x = 0; x < LC_W; x++) {
      uint8_t c = grid[y][x];
      if (!(c & CELL_OWNER)) continue;
      uint16_t col = trail[(c & CELL_OWNER) - 1];
      int sx = LC_X0 + x * 2, sy = LC_Y0 + y * 2;
      display.drawPixel(sx, sy, col);
      if (c & CELL_RIGHT) display.drawPixel(sx + 1, sy, col);
      if (c & CELL_DOWN) display.drawPixel(sx, sy + 1, col);
    }

  for (int n = 0; n < 2; n++) {
    const Cycle &c = cyc[n];
    int sx = LC_X0 + c.x * 2, sy = LC_Y0 + c.y * 2;
    uint16_t col = cycleColor(n);
    if (crashUntil && c.crashed) {
      int r = 2 + (int)((LC_CRASH_S - (crashUntil - gt)) * 14);
      for (int d = 0; d < 4; d++) {
        display.drawPixel(sx + DX[d] * r, sy + DY[d] * r, col);
        display.drawPixel(sx + (DX[d] - DY[d]) * r * 2 / 3, sy + (DY[d] + DX[d]) * r * 2 / 3,
                          GC_WHITE);
      }
      continue;
    }
    // Cycle head: bright body around a white core, a nose one pixel ahead.
    display.fillRect(sx - 1, sy - 1, 3, 3, col);
    display.drawPixel(sx, sy, GC_WHITE);
    display.drawPixel(sx + DX[c.dir] * 2, sy + DY[c.dir] * 2, GC_WHITE);
  }

  if (phase == G_PLAY && (gt < bannerUntil || crashUntil)) {
    char buf[12];
    uint16_t col = GC_YELLOW;
    if (!crashUntil) {
      snprintf(buf, sizeof(buf), "ROUND %u", round_);
    } else if (outcome > 0) {
      strcpy(buf, "ROUND WON");
      col = GC_GREEN;
    } else if (outcome < 0) {
      strcpy(buf, lives ? "CRASHED" : "OUT");
      col = GC_RED;
    } else {
      strcpy(buf, "DRAW");
    }
    int w = strlen(buf) * 6;
    display.fillRect(64 - w / 2 - 3, 32, w + 5, 11, DISPLAY_BLACK);
    display.drawRect(64 - w / 2 - 3, 32, w + 5, 11, col);
    display.setTextColor(col);
    gamePrintCentered(34, buf);
  }

  display.fillRect(0, 0, SCREEN_WIDTH, GAME_TOP, DISPLAY_BLACK);
  gameDrawScore(0, score);
  for (int i = 0; i < lives; i++) display.fillRect(52 + i * 5, 4, 3, 3, cycleColor(0));
  display.setTextColor(GC_GREY);
  display.setCursor(70, 1);
  display.print("R");
  display.print(round_);
  gameDrawClock();
}

bool cyclesFrame(const GamepadState &in, bool padLost) {
  unsigned long now = millis();
  switch (gameFlow(phase, in, padLost)) {
    case STEP_QUIT:
      return false;
    case STEP_RESTART:
      cyclesReset();
      phase = G_PLAY;
      lastTick = now;
      break;
    case STEP_RESUMED:
      lastTick = now;
      break;
    case STEP_PLAY:
      update(in, gameDt(lastTick, now));
      break;
    case STEP_HOLD:
      break;
  }

  draw();
  char info[16];
  snprintf(info, sizeof(info), phase == G_READY ? "HI %lu" : "%lu pts",
           (unsigned long)(phase == G_READY ? hiScore : score));
  gameDrawOverlay(phase, padLost, "LIGHT CYCLES", phase == G_PAUSED ? nullptr : info, newHi,
                  HELP);
  return true;
}

#endif // GAMEPAD_ENABLED
