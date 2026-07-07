/*
 * AnimatedPixelClock - Ambient: Space Invaders battle
 *
 * Self-playing attract loop (no clock, no digits). A 4x7 invader fleet marches
 * in classic lockstep - drifting sideways, dropping and reversing at the edges,
 * legs toggling on every step and speeding up as it thins out. An auto-aiming
 * cannon slides under the fleet and fires; invaders drop bombs back. Hits burst
 * into a short starburst. A UFO occasionally crosses the top for a bonus. When
 * the fleet is cleared (or reaches the cannon line) a fresh wave spawns.
 *
 * The invader sprite is reused from the Space Invaders clock
 * (drawSpaceCharacter, forceType=0 so it is always the invader, never the ship)
 * and follows the user's COL_INVADER color. Everything else is drawn here with
 * hardcoded RGB565, matching the other ambient effects.
 */

#include "ambient.h"

#include "../config/config.h"
#include "../display/display.h"
#include "../clocks/clocks.h"

// ---- Fleet geometry ----
// The fleet must be narrower than the screen so it has room to march side to
// side: at full width it would already touch both margins and just reverse +
// drop on every step (visibly frozen). 6 columns at 15px = 75px leaves ~27px
// of horizontal travel each way.
#define INV_ROWS 4
#define INV_COLS 6
#define INV_COUNT (INV_ROWS * INV_COLS)
#define INV_COL_STEP 15     // px between invader columns (sprite is 11 wide)
#define INV_ROW_STEP 12     // px between invader rows
#define INV_MARGIN 8        // keep the fleet this far from the side walls
#define INV_STEP_X 3        // horizontal jump per march step
#define INV_DROP 6          // vertical drop when reversing
#define INV_TOP_Y 10        // top row center on a fresh wave
#define CANNON_Y 60         // cannon center row

// ---- Effect colors (RGB565) ----
#define COL_CANNON   0x07E0 // green
#define COL_PBULLET  0xFFFF // player bullet (white)
#define COL_BOMB     0xFC00 // invader bomb (orange)
#define COL_UFO      0xF81F // magenta
#define COL_BOOM_A   0xFFE0 // yellow
#define COL_BOOM_B   0xFC00 // orange

struct Bullet { float x, y; bool active; };
struct Boom { float x, y; uint8_t age; bool active; };

static bool invAlive[INV_COUNT];
static uint8_t invAliveCount = 0;
static int fleetOriginX = 0;      // center x of column 0
static int fleetOriginY = INV_TOP_Y;
static int fleetDir = 1;          // +1 right, -1 left
static uint8_t legFrame = 0;
static unsigned long lastMarch = 0;

static float cannonX = SCREEN_WIDTH / 2.0f;
static unsigned long lastCannonFire = 0;
static uint8_t cannonHitTimer = 0;   // frames of cannon "destroyed" flash (0 = alive)

static Bullet pBullets[3];           // player (cannon) shots, travel up
static Bullet bombs[4];              // invader bombs, travel down
static Boom booms[10];

static struct { float x; int dir; bool active; bool alive; } ufo;
static unsigned long ufoNextSpawn = 0;

static bool invInit = false;
static unsigned long lastFrame = 0;

// Screen center x of an invader cell.
static inline int cellX(int col) { return fleetOriginX + col * INV_COL_STEP; }
static inline int cellY(int row) { return fleetOriginY + row * INV_ROW_STEP; }

static void spawnWave() {
  for (int i = 0; i < INV_COUNT; i++) invAlive[i] = true;
  invAliveCount = INV_COUNT;
  int fleetWidth = (INV_COLS - 1) * INV_COL_STEP;
  fleetOriginX = (SCREEN_WIDTH - fleetWidth) / 2;
  fleetOriginY = INV_TOP_Y;
  fleetDir = 1;
  legFrame = 0;
}

static void spawnBoom(float x, float y) {
  for (int i = 0; i < (int)(sizeof(booms) / sizeof(booms[0])); i++) {
    if (!booms[i].active) {
      booms[i] = {x, y, 0, true};
      return;
    }
  }
}

// Leftmost / rightmost living column, so the fleet reverses off the real edge.
static void livingColumnBounds(int& loCol, int& hiCol) {
  loCol = INV_COLS; hiCol = -1;
  for (int c = 0; c < INV_COLS; c++) {
    for (int r = 0; r < INV_ROWS; r++) {
      if (invAlive[r * INV_COLS + c]) {
        if (c < loCol) loCol = c;
        if (c > hiCol) hiCol = c;
        break;
      }
    }
  }
}

// Bottom-most living invader in a column (for bomb drops); -1 if none.
static int bottomRowInColumn(int col) {
  for (int r = INV_ROWS - 1; r >= 0; r--) {
    if (invAlive[r * INV_COLS + col]) return r;
  }
  return -1;
}

static void marchStep() {
  int loCol, hiCol;
  livingColumnBounds(loCol, hiCol);
  if (hiCol < 0) return;  // nothing alive (wave reset handled elsewhere)

  int leftEdge = cellX(loCol) - 5 + fleetDir * INV_STEP_X;
  int rightEdge = cellX(hiCol) + 5 + fleetDir * INV_STEP_X;

  if (leftEdge <= INV_MARGIN || rightEdge >= SCREEN_WIDTH - INV_MARGIN) {
    fleetDir = -fleetDir;
    fleetOriginY += INV_DROP;
  } else {
    fleetOriginX += fleetDir * INV_STEP_X;
  }
  legFrame ^= 1;
}

static void fireCannon() {
  for (int i = 0; i < (int)(sizeof(pBullets) / sizeof(pBullets[0])); i++) {
    if (!pBullets[i].active) {
      pBullets[i] = {cannonX, (float)(CANNON_Y - 3), true};
      return;
    }
  }
}

static void dropBomb() {
  // Pick a random living column and drop from its bottom invader.
  int tries = 6;
  while (tries--) {
    int c = random(INV_COLS);
    int r = bottomRowInColumn(c);
    if (r < 0) continue;
    for (int i = 0; i < (int)(sizeof(bombs) / sizeof(bombs[0])); i++) {
      if (!bombs[i].active) {
        bombs[i] = {(float)cellX(c), (float)(cellY(r) + 8), true};
        return;
      }
    }
    return;
  }
}

// True if a point lands on a living invader; kills it and returns true.
static bool hitInvader(float x, float y) {
  for (int r = 0; r < INV_ROWS; r++) {
    for (int c = 0; c < INV_COLS; c++) {
      int idx = r * INV_COLS + c;
      if (!invAlive[idx]) continue;
      int ix = cellX(c), iy = cellY(r);
      if (x >= ix - 6 && x <= ix + 6 && y >= iy - 5 && y <= iy + 9) {
        invAlive[idx] = false;
        invAliveCount--;
        spawnBoom(ix, iy);
        return true;
      }
    }
  }
  return false;
}

static void drawCannon(int cx, int cy) {
  if (cannonHitTimer > 0) {
    // Destroyed flash: a small scribble instead of the tidy cannon.
    if (cannonHitTimer & 1) {
      display.drawPixel(cx - 2, cy, COL_BOOM_A);
      display.drawPixel(cx + 2, cy + 2, COL_BOOM_B);
      display.drawPixel(cx, cy - 1, COL_BOOM_A);
      display.drawPixel(cx + 3, cy, COL_BOOM_B);
    }
    return;
  }
  display.fillRect(cx - 4, cy + 2, 9, 3, COL_CANNON);  // base
  display.fillRect(cx - 1, cy, 3, 2, COL_CANNON);      // turret
  display.drawPixel(cx, cy - 1, COL_CANNON);           // barrel tip
}

static void drawBoom(const Boom& b) {
  int rad = b.age;  // grows 0..3
  uint16_t col = (b.age < 2) ? COL_BOOM_A : COL_BOOM_B;
  display.drawPixel((int)b.x, (int)b.y, col);
  display.drawPixel((int)b.x - rad, (int)b.y, col);
  display.drawPixel((int)b.x + rad, (int)b.y, col);
  display.drawPixel((int)b.x, (int)b.y - rad, col);
  display.drawPixel((int)b.x, (int)b.y + rad, col);
  display.drawPixel((int)b.x - rad, (int)b.y - rad, col);
  display.drawPixel((int)b.x + rad, (int)b.y + rad, col);
  display.drawPixel((int)b.x - rad, (int)b.y + rad, col);
  display.drawPixel((int)b.x + rad, (int)b.y - rad, col);
}

static void drawUfo(int x, int y) {
  display.fillRect(x - 4, y, 9, 2, COL_UFO);       // body
  display.fillRect(x - 2, y - 1, 5, 1, COL_UFO);   // dome
  display.drawPixel(x - 5, y + 1, COL_UFO);
  display.drawPixel(x + 5, y + 1, COL_UFO);
}

void ambientInvadersFrame() {
  unsigned long now = millis();
  if (!invInit) {
    spawnWave();
    for (auto& b : pBullets) b.active = false;
    for (auto& b : bombs) b.active = false;
    for (auto& b : booms) b.active = false;
    ufo.active = false;
    ufoNextSpawn = now + random(4000, 9000);
    lastMarch = now;
    lastFrame = now;
    lastCannonFire = now;
    invInit = true;
  }

  float dt = (now - lastFrame) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  lastFrame = now;

  // ---- Fleet march (discrete steps, speeds up as invaders die) ----
  long marchInterval = 650 - (long)(INV_COUNT - invAliveCount) * 18;
  if (marchInterval < 90) marchInterval = 90;
  if (now - lastMarch >= marchInterval) {
    lastMarch = now;
    marchStep();
  }

  // Wave reset: cleared, or the fleet reached the cannon.
  if (invAliveCount == 0 ||
      cellY(INV_ROWS - 1) + 8 >= CANNON_Y - 2) {
    spawnWave();
  }

  // ---- Cannon AI: dodge incoming bombs, else line up under an invader ----
  if (cannonHitTimer > 0) {
    cannonHitTimer--;
  } else {
    // A bomb is threatening if it is above the cannon and nearly overhead.
    float threatX = 1e9f;
    for (auto& b : bombs) {
      if (!b.active) continue;
      if (b.y < CANNON_Y && fabsf(b.x - cannonX) < 12.0f) {
        if (fabsf(b.x - cannonX) < fabsf(threatX - cannonX)) threatX = b.x;
      }
    }

    float targetX = cannonX;
    float best = 1e9f;
    bool dodging = (threatX < 1e8f);
    if (dodging) {
      // Sidestep away from the bomb (toward the wall with more room).
      targetX = (threatX > cannonX) ? cannonX - 26 : cannonX + 26;
      if (targetX < 6) targetX = cannonX + 26;
      if (targetX > SCREEN_WIDTH - 6) targetX = cannonX - 26;
    } else {
      for (int r = 0; r < INV_ROWS; r++) {
        for (int c = 0; c < INV_COLS; c++) {
          if (!invAlive[r * INV_COLS + c]) continue;
          float d = fabsf(cellX(c) - cannonX);
          if (d < best) { best = d; targetX = cellX(c); }
        }
      }
    }

    float step = (dodging ? 75.0f : 55.0f) * dt;
    if (cannonX < targetX - 0.5f) cannonX += min(step, targetX - cannonX);
    else if (cannonX > targetX + 0.5f) cannonX -= min(step, cannonX - targetX);
    if (cannonX < 6) cannonX = 6;
    if (cannonX > SCREEN_WIDTH - 6) cannonX = SCREEN_WIDTH - 6;

    // Fire when lined up on a target and off cooldown (never while dodging).
    if (!dodging && now - lastCannonFire > 700 && best < 6.0f) {
      fireCannon();
      lastCannonFire = now;
    }
  }

  // ---- Player bullets (up) ----
  for (auto& b : pBullets) {
    if (!b.active) continue;
    b.y -= 130.0f * dt;
    if (b.y < 0) { b.active = false; continue; }
    if (ufo.active && b.y <= 7 &&
        b.x >= ufo.x - 5 && b.x <= ufo.x + 5) {
      spawnBoom(ufo.x, 5);
      ufo.active = false;
      b.active = false;
      continue;
    }
    if (hitInvader(b.x, b.y)) b.active = false;
  }

  // ---- Invader bombs (down) ----
  static unsigned long lastBomb = 0;
  if (now - lastBomb > 1300 && invAliveCount > 0) {
    lastBomb = now;
    dropBomb();
  }
  for (auto& b : bombs) {
    if (!b.active) continue;
    b.y += 55.0f * dt;
    if (b.y > SCREEN_HEIGHT) { b.active = false; continue; }
    if (cannonHitTimer == 0 && b.y >= CANNON_Y - 2 &&
        b.x >= cannonX - 5 && b.x <= cannonX + 5) {
      spawnBoom(cannonX, CANNON_Y);
      cannonHitTimer = 30;  // ~1s destroyed flash before respawn
      b.active = false;
    }
  }

  // ---- UFO ----
  if (!ufo.active && now >= ufoNextSpawn) {
    ufo.active = true;
    ufo.alive = true;
    ufo.dir = (random(2) == 0) ? 1 : -1;
    ufo.x = (ufo.dir == 1) ? -6 : SCREEN_WIDTH + 6;
    ufoNextSpawn = now + random(9000, 18000);
  }
  if (ufo.active) {
    ufo.x += ufo.dir * 40.0f * dt;
    if (ufo.x < -8 || ufo.x > SCREEN_WIDTH + 8) ufo.active = false;
  }

  // ---- Explosions ----
  for (auto& b : booms) {
    if (!b.active) continue;
    b.age++;
    if (b.age > 4) b.active = false;
  }

  // ================= Draw =================
  if (ufo.active) drawUfo((int)ufo.x, 5);

  for (int r = 0; r < INV_ROWS; r++) {
    for (int c = 0; c < INV_COLS; c++) {
      if (!invAlive[r * INV_COLS + c]) continue;
      drawSpaceCharacter(cellX(c), cellY(r), legFrame, 0);
    }
  }

  drawCannon((int)cannonX, CANNON_Y);

  for (auto& b : pBullets) {
    if (b.active) {
      display.drawPixel((int)b.x, (int)b.y, COL_PBULLET);
      display.drawPixel((int)b.x, (int)b.y - 1, COL_PBULLET);
    }
  }
  for (auto& b : bombs) {
    if (b.active) {
      display.drawPixel((int)b.x, (int)b.y, COL_BOMB);
      display.drawPixel((int)b.x, (int)b.y - 1, COL_BOMB);
    }
  }
  for (auto& b : booms) {
    if (b.active) drawBoom(b);
  }
}
