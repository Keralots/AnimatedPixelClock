/*
 * AnimatedPixelClock - Frog Crossing (game mode)
 *
 * Thirteen 4px rows from y=12, the classic crossing layout: five home bays
 * cut into the hedge, five river lanes of logs and turtles, a safe median,
 * five road lanes and the start pavement. Each hop is one row (or 4px
 * sideways). On the river the frog rides whatever it stands on and drowns
 * in open water or when carried off the edge; on the road any vehicle kills
 * it. Some turtle groups dive now and then. Every frog has 30 seconds to get
 * home; a fly sometimes sits in an empty bay for a bonus. Filling all five
 * bays starts a faster level.
 *
 * Scoring: 10 per new row, 50 per frog home plus 5 per second left, 200 for
 * the fly, 1000 for a full set of bays.
 *
 * Controls: d-pad / left stick hop.
 */

#include "../config/user_config.h"

#if GAMEPAD_ENABLED

#include "../config/config.h"
#include "../display/display.h"
#include "game_mode.h"
#include "game_common.h"

// Controls card on the READY screen.
static const char HELP[] = "\x18\x19\x1b\x1a hop\ndodge cars & water";

#define FR_Y 12
#define FR_ROW 4
#define FR_ROWS 13
#define FR_HOME 0
#define FR_MEDIAN 6
#define FR_START 12
#define FR_START_X 60
#define BAYS 5
#define BAY_W 8
#define LIVES 3
#define TIME_S 30.0f
#define DEATH_S 1.2f
#define BANNER_S 1.2f
#define DIVE_PERIOD 6.0f
#define FLY_S 5.0f

#define C_WATER 0x000C
#define C_PURPLE 0x780F
#define C_HEDGE 0x0300
#define C_LOG_DARK 0x6180
#define C_LOG_LIGHT 0xC386
#define C_TURTLE_DIM 0x6000

enum LaneKind : uint8_t { L_NONE, L_LOG, L_TURTLE, L_CAR, L_RACER, L_DOZER, L_TRUCK };

struct Lane {
  uint8_t kind;
  float speed;    // px/s at level 1, + = right
  uint8_t len;    // object length in px
  uint8_t count;  // objects per period
  uint8_t gap;    // count * (len + gap) must cover 128 + len so the wrap stays off screen
  uint16_t color;
};

static const Lane LANES[FR_ROWS] = {
    {L_NONE, 0, 0, 0, 0, 0},
    {L_LOG, 10, 20, 4, 20, GC_BROWN},
    {L_TURTLE, -14, 14, 5, 18, GC_RED},  // three turtles a group
    {L_LOG, 18, 32, 3, 22, GC_BROWN},
    {L_LOG, 8, 14, 5, 18, GC_BROWN},
    {L_TURTLE, -12, 9, 6, 18, GC_RED},  // two turtles a group
    {L_NONE, 0, 0, 0, 0, 0},
    {L_TRUCK, -10, 11, 3, 43, GC_WHITE},
    {L_RACER, 28, 5, 2, 76, GC_MAGENTA},
    {L_CAR, -14, 5, 4, 36, GC_YELLOW},
    {L_DOZER, 11, 5, 4, 36, GC_ORANGE},
    {L_CAR, -16, 5, 3, 50, GC_CYAN},
    {L_NONE, 0, 0, 0, 0, 0},
};

static const uint8_t BAY_X[BAYS] = {12, 38, 64, 90, 116};  // centres

// Frog facing up, 4 bits per row (bit 3 = left). Row 0 are the eyes.
static const uint8_t FROG[4] = {0x9, 0xF, 0x6, 0x9};

static float laneOff[FR_ROWS];
static float fx;  // frog's left edge, fractional while riding
static uint8_t fr, bestRow, facing;  // facing: 0 up, 1 right, 2 down, 3 left
static bool homeFilled[BAYS];
static int8_t flyBay;
static float flyUntil, nextFly;
static float gt, timeLeft, deadUntil, bannerUntil;
static float deathX;
static uint8_t deathRow;
static bool drowned;
static uint8_t lives, level;
static uint32_t score, hiScore;
static unsigned long lastTick;
static GamePhase phase;
static bool newHi;

static int laneY(int row) { return FR_Y + row * FR_ROW; }
static bool isRiver(int row) { return row > FR_HOME && row < FR_MEDIAN; }
static float speedScale() { return min(1.0f + 0.12f * (level - 1), 2.0f); }
static float period(const Lane &l) { return (float)l.count * (l.len + l.gap); }

// Left edge of object i in lane `row`.
static float objLeft(int row, int i) {
  const Lane &l = LANES[row];
  float p = period(l);
  float x = fmodf(laneOff[row] + i * (l.len + l.gap), p);
  if (x < 0) x += p;
  return x - l.len;
}

// 0 = up, 1 = sinking/rising (still carries the frog), 2 = under.
static uint8_t turtleDive(int row, int i) {
  uint8_t every = level >= 3 ? 2 : 3;
  if (i % every) return 0;
  float t = fmodf(gt + row * 1.3f + i * 0.7f, DIVE_PERIOD);
  if (t < 3.6f) return 0;
  if (t < 4.4f || t >= 5.2f) return 1;
  return 2;
}

static void spawnFrog() {
  fr = FR_START;
  bestRow = FR_START;
  fx = FR_START_X;
  facing = 0;
  timeLeft = TIME_S;
}

static void startLevel() {
  for (auto &h : homeFilled) h = false;
  for (int r = 0; r < FR_ROWS; r++)
    if (LANES[r].kind) laneOff[r] = gameRandf(0, period(LANES[r]));
  flyBay = -1;
  nextFly = gt + gameRandf(6, 12);
  bannerUntil = gt + BANNER_S;
  spawnFrog();
}

void froggerReset() {
  hiScore = gameLoadHi("frogHi");
  gt = 0;
  score = 0;
  lives = LIVES;
  level = 1;
  deadUntil = 0;
  newHi = false;
  startLevel();
  bannerUntil = 0;
  phase = G_READY;
}

static void die(bool water) {
  deadUntil = gt + DEATH_S;
  deathX = fx;
  deathRow = fr;
  drowned = water;
  gameRumble(80, 60, 300);
}

static void reachHome() {
  float c = fx + 2;
  for (int b = 0; b < BAYS; b++) {
    if (fabsf(c - BAY_X[b]) > 3) continue;
    if (homeFilled[b]) break;
    homeFilled[b] = true;
    score += 50 + (uint32_t)timeLeft * 5;
    if (flyBay == b) {
      score += 200;
      flyBay = -1;
    }
    gameRumble(0, 40, 80);
    bool all = true;
    for (bool h : homeFilled) all &= h;
    if (all) {
      score += 1000;
      level++;
      startLevel();
    } else {
      spawnFrog();
    }
    return;
  }
  die(false);  // hedge or an occupied bay
}

static void hop(uint16_t d) {
  if (d == GP_UP && fr > 0) {
    fr--;
    facing = 0;
    if (fr < bestRow) {
      bestRow = fr;
      score += 10;
    }
  } else if (d == GP_DOWN && fr < FR_START) {
    fr++;
    facing = 2;
  } else if (d == GP_LEFT) {
    fx = max(fx - FR_ROW, 0.0f);
    facing = 3;
  } else if (d == GP_RIGHT) {
    fx = min(fx + FR_ROW, (float)(SCREEN_WIDTH - 4));
    facing = 1;
  } else {
    return;
  }
  if (fr == FR_HOME) return reachHome();
  // Off the river the frog snaps back onto the 4px grid.
  if (!isRiver(fr)) fx = constrain(roundf(fx / FR_ROW) * FR_ROW, 0.0f, (float)(SCREEN_WIDTH - 4));
}

// Where the frog stands on the river: the lane's speed if something carries it.
static bool riverCarrier(int row, float &speed) {
  const Lane &l = LANES[row];
  float c = fx + 2;
  for (int i = 0; i < l.count; i++) {
    float left = objLeft(row, i);
    if (c < left - 1 || c >= left + l.len + 1) continue;
    if (l.kind == L_TURTLE && turtleDive(row, i) == 2) continue;
    speed = l.speed * speedScale();
    return true;
  }
  return false;
}

static bool roadHit(int row) {
  const Lane &l = LANES[row];
  for (int i = 0; i < l.count; i++) {
    float left = objLeft(row, i);
    if (fx + 0.5f < left + l.len && fx + 3.5f > left) return true;
  }
  return false;
}

static void update(const GamepadState &in, float dt) {
  gt += dt;
  for (int r = 0; r < FR_ROWS; r++)
    if (LANES[r].kind) laneOff[r] = fmodf(laneOff[r] + LANES[r].speed * speedScale() * dt, period(LANES[r]));

  if (flyBay >= 0 && gt >= flyUntil) flyBay = -1;
  if (flyBay < 0 && gt >= nextFly) {
    int b = esp_random() % BAYS;
    if (!homeFilled[b]) {
      flyBay = b;
      flyUntil = gt + FLY_S;
    }
    nextFly = gt + gameRandf(8, 14);
  }

  if (deadUntil) {
    if (gt < deadUntil) return;
    deadUntil = 0;
    if (--lives == 0) {
      phase = G_OVER;
      newHi = gameSubmitScore("frogHi", score);
      if (newHi) hiScore = score;
      return;
    }
    spawnFrog();
    return;
  }

  for (uint8_t i = 0; i < in.dirCount && !deadUntil; i++) hop(in.dirs[i]);
  if (deadUntil) return;

  timeLeft -= dt;
  if (timeLeft <= 0) {
    timeLeft = 0;
    return die(false);
  }

  if (isRiver(fr)) {
    float v;
    if (!riverCarrier(fr, v)) return die(true);
    fx += v * dt;
    if (fx < -1 || fx > SCREEN_WIDTH - 3) return die(true);
  } else if (LANES[fr].kind && roadHit(fr)) {
    die(false);
  }
}

static uint16_t dim(uint16_t c) { return (c >> 1) & 0x7BEF; }

// Pixel i of an object counted from its rear, mapped onto the screen.
static void objPixel(int left, int len, bool right, int i, int y, uint16_t c) {
  display.drawPixel(right ? left + i : left + len - 1 - i, y, c);
}

static void drawVehicle(const Lane &l, int left, int y) {
  bool right = l.speed > 0;
  uint16_t c = l.color;
  switch (l.kind) {
    case L_TRUCK:
      for (int i = 0; i < 8; i++) {
        objPixel(left, l.len, right, i, y, GC_GREY);
        objPixel(left, l.len, right, i, y + 1, GC_GREY);
      }
      for (int i = 9; i < 11; i++) {
        objPixel(left, l.len, right, i, y, GC_RED);
        objPixel(left, l.len, right, i, y + 1, GC_RED);
      }
      for (int i : {1, 2, 5, 6, 9, 10}) objPixel(left, l.len, right, i, y + 2, GC_DIM);
      break;
    case L_DOZER:
      objPixel(left, l.len, right, 1, y, c);
      objPixel(left, l.len, right, 2, y, c);
      for (int i = 0; i < 4; i++) objPixel(left, l.len, right, i, y + 1, c);
      for (int i = 0; i < 4; i++) objPixel(left, l.len, right, i, y + 2, GC_DIM);
      for (int r = 0; r < 3; r++) objPixel(left, l.len, right, 4, y + r, GC_GREY);  // blade
      break;
    case L_RACER:
      objPixel(left, l.len, right, 0, y, c);
      objPixel(left, l.len, right, 3, y, GC_WHITE);
      for (int i = 0; i < 5; i++) objPixel(left, l.len, right, i, y + 1, c);
      objPixel(left, l.len, right, 1, y + 2, GC_DIM);
      objPixel(left, l.len, right, 3, y + 2, GC_DIM);
      break;
    default:  // car
      for (int i = 1; i < 4; i++) objPixel(left, l.len, right, i, y, i == 3 ? GC_WHITE : c);
      for (int i = 0; i < 5; i++) objPixel(left, l.len, right, i, y + 1, c);
      objPixel(left, l.len, right, 1, y + 2, GC_DIM);
      objPixel(left, l.len, right, 3, y + 2, GC_DIM);
      break;
  }
}

static void drawLog(int left, int len, int y) {
  display.drawFastHLine(left + 1, y, len - 2, C_LOG_LIGHT);
  display.drawFastHLine(left, y + 1, len, GC_BROWN);
  display.drawFastHLine(left + 1, y + 2, len - 2, C_LOG_DARK);
  for (int x = left + 3; x < left + len - 2; x += 6) display.drawPixel(x, y + 1, C_LOG_DARK);
}

// One turtle, 4px wide with the head on the swimming side.
static void drawTurtle(int x, int y, bool right, uint16_t c) {
  int sx = right ? x : x + 1;
  display.drawFastHLine(sx, y, 3, c);
  display.drawFastHLine(sx, y + 1, 3, c);
  display.drawFastHLine(sx, y + 2, 3, c);
  display.drawPixel(sx + 1, y + 1, dim(c));
  display.drawPixel(right ? x + 3 : x, y + 1, GC_GREEN);
}

static void drawFrog(int x, int y, uint8_t dir, uint16_t body) {
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) {
      if (!(FROG[r] & (8 >> c))) continue;
      uint16_t col = r == 0 ? GC_YELLOW : body;
      switch (dir) {
        case 0: display.drawPixel(x + c, y + r, col); break;
        case 2: display.drawPixel(x + c, y + 3 - r, col); break;
        case 3: display.drawPixel(x + r, y + c, col); break;
        default: display.drawPixel(x + 3 - r, y + c, col); break;
      }
    }
}

static void drawDeath(int x, int y) {
  bool on = ((int)(gt * 8)) % 2;
  if (drowned) {
    uint16_t c = on ? GC_WHITE : GC_CYAN;
    display.drawPixel(x + 1, y, c);
    display.drawPixel(x + 2, y + 3, c);
    display.drawPixel(x, y + 2, c);
    display.drawPixel(x + 3, y + 1, c);
  } else {
    uint16_t c = on ? GC_RED : GC_ORANGE;
    display.drawLine(x, y, x + 3, y + 3, c);
    display.drawLine(x + 3, y, x, y + 3, c);
  }
}

static void draw() {
  // Hedge with the bays cut out.
  display.fillRect(0, GAME_TOP, SCREEN_WIDTH, FR_Y + FR_ROW - GAME_TOP, C_HEDGE);
  for (int x = 0; x < SCREEN_WIDTH; x += 2) display.drawPixel(x + (x / 2) % 2, GAME_TOP, GC_DARK_GREEN);
  for (int b = 0; b < BAYS; b++) {
    int bx = BAY_X[b] - BAY_W / 2;
    display.fillRect(bx, FR_Y, BAY_W, FR_ROW, C_WATER);
    if (homeFilled[b]) {
      drawFrog(BAY_X[b] - 2, FR_Y, 2, GC_GREEN);
    } else if (flyBay == b) {
      bool wing = ((int)(gt * 6)) % 2;
      display.drawPixel(BAY_X[b], FR_Y + 2, GC_WHITE);
      display.drawPixel(BAY_X[b] - 1, FR_Y + (wing ? 1 : 2), GC_CYAN);
      display.drawPixel(BAY_X[b] + 1, FR_Y + (wing ? 1 : 2), GC_CYAN);
    }
  }

  display.fillRect(0, laneY(1), SCREEN_WIDTH, 5 * FR_ROW, C_WATER);
  display.fillRect(0, laneY(FR_MEDIAN), SCREEN_WIDTH, FR_ROW, C_PURPLE);
  display.fillRect(0, laneY(FR_START), SCREEN_WIDTH, FR_ROW, C_PURPLE);
  for (int r = FR_MEDIAN + 1; r < FR_START - 1; r++)
    for (int x = 2; x < SCREEN_WIDTH; x += 8) display.drawFastHLine(x, laneY(r) + 3, 3, GC_DIM);

  for (int r = 1; r < FR_START; r++) {
    const Lane &l = LANES[r];
    if (!l.kind) continue;
    int y = laneY(r);
    for (int i = 0; i < l.count; i++) {
      int left = (int)floorf(objLeft(r, i));
      if (left >= SCREEN_WIDTH || left + l.len <= 0) continue;
      if (l.kind == L_LOG) {
        drawLog(left, l.len, y);
      } else if (l.kind == L_TURTLE) {
        uint8_t d = turtleDive(r, i);
        if (d == 2) continue;
        for (int x = left; x < left + l.len; x += 5) drawTurtle(x, y, l.speed > 0, d ? C_TURTLE_DIM : l.color);
      } else {
        drawVehicle(l, left, y);
      }
    }
  }

  if (deadUntil) drawDeath((int)roundf(deathX), laneY(deathRow));
  else drawFrog((int)roundf(fx), laneY(fr), facing, GC_GREEN);

  if (gt < bannerUntil && phase == G_PLAY) {
    char buf[10];
    snprintf(buf, sizeof(buf), "LEVEL %u", level);
    display.fillRect(64 - strlen(buf) * 3 - 2, laneY(FR_MEDIAN) - 3, strlen(buf) * 6 + 3, 10, DISPLAY_BLACK);
    display.setTextColor(GC_YELLOW);
    gamePrintCentered(laneY(FR_MEDIAN) - 2, buf);
  }

  display.fillRect(0, 0, SCREEN_WIDTH, GAME_TOP, DISPLAY_BLACK);
  gameDrawScore(0, score);
  for (int i = 0; i < lives; i++) drawFrog(34 + i * 6, 3, 0, GC_GREEN);
  display.setTextColor(GC_GREY);
  display.setCursor(53, 1);
  display.print("L");
  display.print(level);
  int bar = (int)(22 * timeLeft / TIME_S + 0.99f);
  display.drawRect(66, 3, 24, 4, GC_DIM);
  display.fillRect(67, 4, bar, 2, timeLeft > 10 ? GC_GREEN : timeLeft > 5 ? GC_YELLOW : GC_RED);
  gameDrawClock();
}

bool froggerFrame(const GamepadState &in, bool padLost) {
  unsigned long now = millis();
  switch (gameFlow(phase, in, padLost)) {
    case STEP_QUIT:
      return false;
    case STEP_RESTART:
      froggerReset();
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
  gameDrawOverlay(phase, padLost, "FROG CROSSING", phase == G_PAUSED ? nullptr : info, newHi, HELP);
  return true;
}

#endif // GAMEPAD_ENABLED
