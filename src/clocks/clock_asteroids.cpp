/*
 * SmallOLED-PCMonitor - Asteroids Clock (clockStyle 10)
 *
 * A vector-style wireframe homage to Asteroids. A triangular ship drifts
 * around the screen with real inertia (thrust bursts, slow turns, screen
 * wrap-around) while a few jagged wireframe rocks tumble past. Every so
 * often the ship lines up a shot and splits a rock - big rocks break into
 * two small ones, small ones burst into line-shard debris - so something
 * is always happening even mid-minute.
 *
 * At the top of each minute the ship turns gunner: it aims at each changed
 * digit in turn, fires, and the old digit shatters into spinning line
 * fragments that drift off. Once the debris clears, the new digit drops in
 * with the shared digit bounce. The ship and rocks fly *behind* the digits
 * (the digit boxes are masked before the glyphs are drawn), which reads as
 * the time being solid plates floating in space.
 *
 * All state is file-local. resetAsteroidsAnimation() (called from
 * resetClockAnimationState) returns everything to a clean baseline.
 */

#include "../config/config.h"
#include "../display/display.h"
#include "clocks.h"
#include "clock_globals.h"

#include <math.h>

// ========== Layout / tuning ==========
#define AST_TIME_Y_TOP 16        // digit top when the date row is shown
#define AST_TIME_Y_CENTER 21     // digit top when centred (date off)
#define AST_TRIGGER_SECOND 56
#define AST_DIGIT_W 16           // size-3 digit box width
#define AST_DIGIT_H 21           // size-3 digit box height
#define AST_MAX_ROCKS 8          // pool: configured count + split children
#define AST_ROCK_VERTS 7
#define AST_MAX_SHARDS 14
#define AST_SHARD_LIFE 0.9f      // seconds a digit shard lives
#define AST_BULLET_SPEED 140.0f  // px/s
#define AST_BULLET_LIFE 1.5f     // seconds before a stray shot expires
#define AST_AIM_RATE 4.5f        // rad/s turn rate while aiming
#define AST_AIM_TIMEOUT 2.0f     // fire anyway after this long aiming
#define AST_MAX_REFIRES 2        // re-shots at a digit before force-shatter
#define AST_PELLET_PITCH 3       // glyph cell pitch (size-3 numerals)

enum AstPhase { AST_IDLE, AST_AIM, AST_FIRE, AST_SHATTER };

struct AstRock {
  bool active;
  bool big;                      // big rocks split, small ones burst
  float x, y, vx, vy;
  float angle, spin;
  uint8_t vertRadius[AST_ROCK_VERTS];  // per-vertex radius (jagged outline)
};

struct AstShard {
  bool active;
  bool fromDigit;                // true = digit shatter (COL_DIGITS), false = rock burst (COL_AST_ROCK)
  float x, y, vx, vy;
  float angle, spin;
  float life;                    // seconds remaining
};

// 5x7 numeral glyphs (same font as the Snake/Tetris pellet placement) -
// used to spawn the shards a shattered digit leaves behind.
static const uint8_t astDigitGlyph[10][7] = {
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

static const int AST_DIGIT_IDX[4] = {0, 1, 3, 4};  // digit slots (skip colon)

// Ship
static AstPhase ast_phase = AST_IDLE;
static float ast_ship_x = 30.0f, ast_ship_y = 50.0f;
static float ast_ship_vx = 0.0f, ast_ship_vy = 0.0f;
static float ast_ship_heading = 0.0f;     // radians, 0 = +X
static bool ast_thrusting = false;
static float ast_idle_timer = 0.0f;       // until the next idle decision
static float ast_idle_turn_target = 0.0f;
static bool ast_idle_turning = false;
static float ast_thrust_timer = 0.0f;     // remaining thrust burst
static float ast_shot_timer = 5.0f;       // until the next idle rock shot

// Rocks
static AstRock ast_rocks[AST_MAX_ROCKS];
static float ast_respawn_timer = 0.0f;

// Bullet (one in flight at a time)
static bool ast_bullet_active = false;
static float ast_bullet_x = 0, ast_bullet_y = 0;
static float ast_bullet_vx = 0, ast_bullet_vy = 0;
static float ast_bullet_life = 0.0f;
static int ast_bullet_rock = -1;          // idle shot: rock index, or -1 = digit

// Shards (digit debris + rock bursts share the pool)
static AstShard ast_shards[AST_MAX_SHARDS];

// Minute-change bookkeeping
static int ast_change_idx[4];
static uint8_t ast_change_val[4];
static int ast_num_changes = 0;
static int ast_cur_change = 0;
static float ast_aim_timer = 0.0f;
static int ast_refires = 0;
static int last_minute_ast = -1;
static bool ast_triggered = false;

static unsigned long last_ast_update = 0;
static bool ast_init_done = false;

// ========== Geometry helpers ==========
static int astTimeY() {
  return settings.asteroidsShowDate ? AST_TIME_Y_TOP : AST_TIME_Y_CENTER;
}

static float astRandf(float lo, float hi) {
  return lo + (hi - lo) * (random(0, 1001) / 1000.0f);
}

static void astWrap(float &x, float &y, float margin) {
  if (x < -margin) x += SCREEN_WIDTH + 2 * margin;
  if (x > SCREEN_WIDTH + margin) x -= SCREEN_WIDTH + 2 * margin;
  if (y < -margin) y += SCREEN_HEIGHT + 2 * margin;
  if (y > SCREEN_HEIGHT + margin) y -= SCREEN_HEIGHT + 2 * margin;
}

// Smallest signed angle from a to b
static float astAngleDiff(float a, float b) {
  float d = b - a;
  while (d > PI) d -= TWO_PI;
  while (d < -PI) d += TWO_PI;
  return d;
}

// ========== Rocks ==========
static int astActiveRockCount() {
  int n = 0;
  for (int i = 0; i < AST_MAX_ROCKS; i++)
    if (ast_rocks[i].active) n++;
  return n;
}

static void astInitRockShape(AstRock &r, float baseRadius) {
  for (int v = 0; v < AST_ROCK_VERTS; v++) {
    r.vertRadius[v] = (uint8_t)(baseRadius * astRandf(0.7f, 1.3f) + 0.5f);
    if (r.vertRadius[v] < 2) r.vertRadius[v] = 2;
  }
}

// Spawn a rock drifting in from a random screen edge.
static void astSpawnRock() {
  for (int i = 0; i < AST_MAX_ROCKS; i++) {
    if (ast_rocks[i].active) continue;
    AstRock &r = ast_rocks[i];
    r.active = true;
    r.big = true;
    int edge = random(0, 4);
    switch (edge) {
      case 0:  r.x = -8;                 r.y = astRandf(8, 56);  break;
      case 1:  r.x = SCREEN_WIDTH + 8;   r.y = astRandf(8, 56);  break;
      case 2:  r.x = astRandf(8, 120);   r.y = -8;               break;
      default: r.x = astRandf(8, 120);   r.y = SCREEN_HEIGHT + 8; break;
    }
    float speed = 10.0f * (settings.asteroidsRockSpeed / 10.0f);
    // Head loosely toward the screen centre so the rock actually enters view
    float ang = atan2f(32.0f - r.y, 64.0f - r.x) + astRandf(-0.6f, 0.6f);
    r.vx = cosf(ang) * speed;
    r.vy = sinf(ang) * speed;
    r.angle = astRandf(0, TWO_PI);
    r.spin = astRandf(0.5f, 1.5f) * (random(0, 2) ? 1.0f : -1.0f);
    astInitRockShape(r, 7.0f);
    return;
  }
}

// Burst a rock into a few line shards (also used for split children debris).
static void astSpawnBurst(float x, float y, int count, float speedScale) {
  for (int s = 0; s < AST_MAX_SHARDS && count > 0; s++) {
    if (ast_shards[s].active) continue;
    AstShard &sh = ast_shards[s];
    sh.active = true;
    sh.fromDigit = false;  // rock-burst debris
    sh.x = x;
    sh.y = y;
    float ang = astRandf(0, TWO_PI);
    float spd = astRandf(15.0f, 45.0f) * speedScale;
    sh.vx = cosf(ang) * spd;
    sh.vy = sinf(ang) * spd;
    sh.angle = astRandf(0, TWO_PI);
    sh.spin = astRandf(-4.0f, 4.0f);
    sh.life = AST_SHARD_LIFE * astRandf(0.6f, 1.0f);
    count--;
  }
}

// Bullet hit: big rock splits into two small ones, small rock bursts.
static void astHitRock(int idx) {
  AstRock &r = ast_rocks[idx];
  if (r.big) {
    r.big = false;
    astInitRockShape(r, 4.0f);
    float ang = atan2f(r.vy, r.vx) + HALF_PI;
    float kick = 8.0f;
    float kx = cosf(ang) * kick, ky = sinf(ang) * kick;
    // Child rock heads the other way
    for (int i = 0; i < AST_MAX_ROCKS; i++) {
      if (ast_rocks[i].active) continue;
      AstRock &c = ast_rocks[i];
      c = r;
      c.vx = r.vx - kx;
      c.vy = r.vy - ky;
      c.spin = -r.spin;
      astInitRockShape(c, 4.0f);
      break;
    }
    r.vx += kx;
    r.vy += ky;
    astSpawnBurst(r.x, r.y, 3, 0.6f);
  } else {
    r.active = false;
    astSpawnBurst(r.x, r.y, 5, 1.0f);
  }
}

// ========== Digit shatter ==========
// Convert the old digit's lit glyph cells into outward-flying line shards.
static void astShatterDigit(int digitIdx) {
  uint8_t oldVal = getDisplayedDigitValue(digitIdx);
  int gy = astTimeY();
  float cx = DIGIT_X[digitIdx] + AST_DIGIT_W / 2.0f;
  float cy = gy + AST_DIGIT_H / 2.0f;

  // Collect lit cells, then keep an even spread of up to 10 as shards
  int litX[35], litY[35], litN = 0;
  for (int row = 0; row < 7; row++) {
    uint8_t bits = astDigitGlyph[oldVal][row];
    for (int col = 0; col < 5; col++) {
      if ((bits >> (4 - col)) & 1) {
        litX[litN] = DIGIT_X[digitIdx] + col * AST_PELLET_PITCH;
        litY[litN] = gy + row * AST_PELLET_PITCH;
        litN++;
      }
    }
  }
  int target = 10;
  if (target > litN) target = litN;
  int spawned = 0;
  for (int s = 0; s < AST_MAX_SHARDS && spawned < target; s++) {
    if (ast_shards[s].active) continue;
    AstShard &sh = ast_shards[s];
    int src = (spawned * litN) / target;
    sh.active = true;
    sh.fromDigit = true;  // digit-shatter debris
    sh.x = litX[src];
    sh.y = litY[src];
    // Fly outward from the digit centre, plus a little randomness
    float ang = atan2f(sh.y - cy, sh.x - cx) + astRandf(-0.5f, 0.5f);
    float spd = astRandf(20.0f, 50.0f);
    sh.vx = cosf(ang) * spd;
    sh.vy = sinf(ang) * spd;
    sh.angle = astRandf(0, TWO_PI);
    sh.spin = astRandf(-4.0f, 4.0f);
    sh.life = AST_SHARD_LIFE * astRandf(0.7f, 1.0f);
    spawned++;
  }
  ast_phase = AST_SHATTER;
}

static bool astAllShardsDead() {
  for (int i = 0; i < AST_MAX_SHARDS; i++)
    if (ast_shards[i].active) return false;
  return true;
}

// Reveal the new digit and move to the next change (or back to idle).
static void astRevealAndAdvance() {
  updateDisplayedTimeDigit(ast_change_idx[ast_cur_change],
                           ast_change_val[ast_cur_change]);
  triggerDigitBounce(ast_change_idx[ast_cur_change]);
  ast_cur_change++;
  if (ast_cur_change < ast_num_changes) {
    ast_phase = AST_AIM;
    ast_aim_timer = 0.0f;
    ast_refires = 0;
  } else {
    ast_phase = AST_IDLE;
    ast_idle_timer = 0.5f;
  }
}

// ========== Reset ==========
void resetAsteroidsAnimation() {
  ast_phase = AST_IDLE;
  ast_ship_x = 30.0f;
  ast_ship_y = 50.0f;
  ast_ship_vx = 8.0f;
  ast_ship_vy = -3.0f;
  ast_ship_heading = -0.4f;
  ast_thrusting = false;
  ast_idle_timer = 1.0f;
  ast_idle_turning = false;
  ast_thrust_timer = 0.0f;
  ast_shot_timer = astRandf(4.0f, 8.0f);
  ast_respawn_timer = 0.0f;
  ast_bullet_active = false;
  ast_bullet_rock = -1;
  for (int i = 0; i < AST_MAX_ROCKS; i++) ast_rocks[i].active = false;
  for (int i = 0; i < AST_MAX_SHARDS; i++) ast_shards[i].active = false;
  int rocks = constrain((int)settings.asteroidsRockCount, 1, 4);
  for (int i = 0; i < rocks; i++) astSpawnRock();
  ast_num_changes = 0;
  ast_cur_change = 0;
  ast_triggered = false;
  last_minute_ast = -1;
  last_ast_update = 0;
  ast_init_done = true;
}

// ========== Update ==========
static void astFireBullet(float tx, float ty, int rockIdx) {
  float ang = atan2f(ty - ast_ship_y, tx - ast_ship_x);
  ast_bullet_active = true;
  ast_bullet_x = ast_ship_x + cosf(ast_ship_heading) * 6.0f;
  ast_bullet_y = ast_ship_y + sinf(ast_ship_heading) * 6.0f;
  ast_bullet_vx = cosf(ang) * AST_BULLET_SPEED;
  ast_bullet_vy = sinf(ang) * AST_BULLET_SPEED;
  ast_bullet_life = AST_BULLET_LIFE;
  ast_bullet_rock = rockIdx;
}

// Soft repulsion that keeps the ship out of the masked digit/date plates so
// it stays visible instead of cruising hidden behind the time. Strength beats
// thrust (90 > 40*scale max ~48 px/s^2 at default), so the ship can pierce a
// few px past the band edge at full tilt but always gets shoved back out -
// it reads as skimming a force field around the time plates.
static void astAvoidPlates(float dt) {
  // Transparent mode: nothing is hidden, so let the ship fly straight
  // through the digits instead of dodging them.
  if (settings.asteroidsTransparent) return;
  int gy = astTimeY();
  float bandTop = gy - 6.0f;
  float bandBot = gy + AST_DIGIT_H + 6.0f;
  float bandL = DIGIT_X[0] - 6.0f;
  float bandR = DIGIT_X[4] + AST_DIGIT_W + 6.0f;
  if (ast_ship_x > bandL && ast_ship_x < bandR &&
      ast_ship_y > bandTop && ast_ship_y < bandBot) {
    float centerY = (bandTop + bandBot) * 0.5f;
    ast_ship_vy += (ast_ship_y < centerY ? -90.0f : 90.0f) * dt;
    // gentle shove toward the nearer side exit too
    float centerX = (bandL + bandR) * 0.5f;
    ast_ship_vx += (ast_ship_x < centerX ? -25.0f : 25.0f) * dt;
  }
  if (settings.asteroidsShowDate) {
    // Date plate sits top-centre; push the ship down and out of it
    if (ast_ship_y < 16.0f && ast_ship_x > 28.0f && ast_ship_x < 100.0f) {
      ast_ship_vy += 90.0f * dt;
    }
  }
}

static void updateAsteroidsAnimation(struct tm *timeinfo) {
  unsigned long now = millis();
  updateDigitBounce();

  float dt = (now - last_ast_update) / 1000.0f;
  if (dt > 0.1f || last_ast_update == 0) dt = 0.025f;
  last_ast_update = now;

  float shipScale = settings.asteroidsShipSpeed / 10.0f;

  // ----- Minute-change trigger (same scheme as Snake/Tetris) -----
  int seconds = timeinfo->tm_sec;
  int minute = timeinfo->tm_min;
  if (minute != last_minute_ast) {
    last_minute_ast = minute;
    ast_triggered = false;
  }
  if (seconds >= AST_TRIGGER_SECOND && !ast_triggered && ast_phase == AST_IDLE) {
    ast_triggered = true;
    time_overridden = true;
    time_override_start = millis();
    calculateTargetDigits(displayed_hour, displayed_min, displayed_is_pm);

    ast_num_changes = 0;
    for (int i = 0; i < num_targets; i++) {
      if (target_digit_index[i] != 2) {  // skip the colon
        ast_change_idx[ast_num_changes] = target_digit_index[i];
        ast_change_val[ast_num_changes] = target_digit_values[i];
        ast_num_changes++;
      }
    }
    ast_cur_change = 0;
    if (ast_num_changes > 0) {
      ast_bullet_active = false;  // drop any idle shot mid-flight
      ast_phase = AST_AIM;
      ast_aim_timer = 0.0f;
      ast_refires = 0;
    } else {
      time_overridden = false;  // only the colon changed
    }
  }

  // ----- Ship behaviour -----
  if (ast_phase == AST_IDLE) {
    ast_idle_timer -= dt;
    ast_shot_timer -= dt;

    if (ast_idle_turning) {
      float d = astAngleDiff(ast_ship_heading, ast_idle_turn_target);
      float step = 2.5f * dt;
      if (fabsf(d) <= step) {
        ast_ship_heading = ast_idle_turn_target;
        ast_idle_turning = false;
      } else {
        ast_ship_heading += (d > 0 ? step : -step);
      }
    }

    if (ast_thrust_timer > 0) {
      ast_thrust_timer -= dt;
      ast_thrusting = ast_thrust_timer > 0;
    }

    if (ast_idle_timer <= 0) {
      ast_idle_timer = astRandf(1.2f, 3.0f);
      int action = random(0, 100);
      if (action < 40) {
        ast_idle_turn_target = astRandf(0, TWO_PI);
        ast_idle_turning = true;
      } else if (action < 75) {
        ast_thrust_timer = astRandf(0.3f, 0.7f);
        ast_thrusting = true;
      }
      // else: coast
    }

    // Take an occasional potshot at a rock
    if (ast_shot_timer <= 0 && !ast_bullet_active) {
      ast_shot_timer = astRandf(6.0f, 14.0f);
      int best = -1;
      for (int i = 0; i < AST_MAX_ROCKS; i++) {
        if (!ast_rocks[i].active) continue;
        if (ast_rocks[i].x < 4 || ast_rocks[i].x > SCREEN_WIDTH - 4 ||
            ast_rocks[i].y < 4 || ast_rocks[i].y > SCREEN_HEIGHT - 4) continue;
        if (best < 0 || (ast_rocks[i].big && !ast_rocks[best].big)) best = i;
      }
      if (best >= 0) {
        ast_ship_heading = atan2f(ast_rocks[best].y - ast_ship_y,
                                  ast_rocks[best].x - ast_ship_x);
        ast_idle_turning = false;
        astFireBullet(ast_rocks[best].x, ast_rocks[best].y, best);
      }
    }
  } else if (ast_phase == AST_AIM) {
    // Rotate toward the doomed digit, braking gently while lining up
    ast_thrusting = false;
    ast_ship_vx *= (1.0f - 1.5f * dt);
    ast_ship_vy *= (1.0f - 1.5f * dt);
    int didx = ast_change_idx[ast_cur_change];
    float tx = DIGIT_X[didx] + AST_DIGIT_W / 2.0f;
    float ty = astTimeY() + AST_DIGIT_H / 2.0f;
    float want = atan2f(ty - ast_ship_y, tx - ast_ship_x);
    float d = astAngleDiff(ast_ship_heading, want);
    float step = AST_AIM_RATE * dt;
    ast_aim_timer += dt;
    if (fabsf(d) <= step || ast_aim_timer > AST_AIM_TIMEOUT) {
      ast_ship_heading = want;
      astFireBullet(tx, ty, -1);
      ast_phase = AST_FIRE;
    } else {
      ast_ship_heading += (d > 0 ? step : -step);
    }
  } else if (ast_phase == AST_FIRE) {
    // Wait for the bullet; if it somehow dies without hitting, re-fire
    if (!ast_bullet_active) {
      if (ast_refires < AST_MAX_REFIRES) {
        ast_refires++;
        ast_phase = AST_AIM;
        ast_aim_timer = 0.0f;
      } else {
        astShatterDigit(ast_change_idx[ast_cur_change]);  // force the change
      }
    }
  } else if (ast_phase == AST_SHATTER) {
    if (astAllShardsDead()) astRevealAndAdvance();
  }

  // Thrust + integrate ship motion (drag keeps speed bounded)
  if (ast_thrusting && ast_phase == AST_IDLE) {
    ast_ship_vx += cosf(ast_ship_heading) * 40.0f * shipScale * dt;
    ast_ship_vy += sinf(ast_ship_heading) * 40.0f * shipScale * dt;
  }
  astAvoidPlates(dt);
  float maxSpd = 35.0f * shipScale;
  float spd2 = ast_ship_vx * ast_ship_vx + ast_ship_vy * ast_ship_vy;
  if (spd2 > maxSpd * maxSpd) {
    float k = maxSpd / sqrtf(spd2);
    ast_ship_vx *= k;
    ast_ship_vy *= k;
  }
  ast_ship_vx *= (1.0f - 0.15f * dt);
  ast_ship_vy *= (1.0f - 0.15f * dt);
  ast_ship_x += ast_ship_vx * dt;
  ast_ship_y += ast_ship_vy * dt;
  astWrap(ast_ship_x, ast_ship_y, 6.0f);

  // ----- Rocks -----
  for (int i = 0; i < AST_MAX_ROCKS; i++) {
    if (!ast_rocks[i].active) continue;
    AstRock &r = ast_rocks[i];
    r.x += r.vx * dt;
    r.y += r.vy * dt;
    r.angle += r.spin * dt;
    astWrap(r.x, r.y, 10.0f);
  }

  // Keep the configured number of rocks in play
  int wantRocks = constrain((int)settings.asteroidsRockCount, 1, 4);
  if (astActiveRockCount() < wantRocks) {
    ast_respawn_timer -= dt;
    if (ast_respawn_timer <= 0) {
      astSpawnRock();
      ast_respawn_timer = astRandf(2.0f, 5.0f);
    }
  }

  // ----- Bullet -----
  if (ast_bullet_active) {
    ast_bullet_x += ast_bullet_vx * dt;
    ast_bullet_y += ast_bullet_vy * dt;
    ast_bullet_life -= dt;
    if (ast_bullet_life <= 0) {
      ast_bullet_active = false;
    } else if (ast_bullet_rock >= 0) {
      // Idle shot: hit test against the targeted rock
      AstRock &r = ast_rocks[ast_bullet_rock];
      if (r.active) {
        float dx = ast_bullet_x - r.x, dy = ast_bullet_y - r.y;
        float rad = r.big ? 8.0f : 5.0f;
        if (dx * dx + dy * dy < rad * rad) {
          astHitRock(ast_bullet_rock);
          ast_bullet_active = false;
        }
      } else {
        ast_bullet_active = false;
      }
    } else if (ast_phase == AST_FIRE) {
      // Digit shot: hit test against the doomed digit's box
      int didx = ast_change_idx[ast_cur_change];
      int gy = astTimeY();
      if (ast_bullet_x >= DIGIT_X[didx] - 1 &&
          ast_bullet_x <= DIGIT_X[didx] + AST_DIGIT_W + 1 &&
          ast_bullet_y >= gy - 1 && ast_bullet_y <= gy + AST_DIGIT_H + 1) {
        ast_bullet_active = false;
        astShatterDigit(didx);
      }
    }
  }

  // ----- Shards -----
  for (int i = 0; i < AST_MAX_SHARDS; i++) {
    if (!ast_shards[i].active) continue;
    AstShard &sh = ast_shards[i];
    sh.x += sh.vx * dt;
    sh.y += sh.vy * dt;
    sh.angle += sh.spin * dt;
    sh.life -= dt;
    if (sh.life <= 0 || sh.x < -8 || sh.x > SCREEN_WIDTH + 8 ||
        sh.y < -8 || sh.y > SCREEN_HEIGHT + 8) {
      sh.active = false;
    }
  }
}

// ========== Drawing ==========
static void drawAstShip() {
  uint16_t col = SPRITE_COLOR(COL_AST_SHIP);
  // Classic Asteroids triangle: nose + two rear corners around the centre
  float c = cosf(ast_ship_heading), s = sinf(ast_ship_heading);
  float nx = ast_ship_x + c * 5.0f,            ny = ast_ship_y + s * 5.0f;
  float lx = ast_ship_x - c * 4.0f - s * 3.5f, ly = ast_ship_y - s * 4.0f + c * 3.5f;
  float rx = ast_ship_x - c * 4.0f + s * 3.5f, ry = ast_ship_y - s * 4.0f - c * 3.5f;
  display.drawLine((int)nx, (int)ny, (int)lx, (int)ly, col);
  display.drawLine((int)nx, (int)ny, (int)rx, (int)ry, col);
  display.drawLine((int)lx, (int)ly, (int)rx, (int)ry, col);

  // Flickering thrust flame out the back
  if (ast_thrusting && (millis() / 60) % 2 == 0) {
    float fx = ast_ship_x - c * 8.0f, fy = ast_ship_y - s * 8.0f;
    float bx = ast_ship_x - c * 4.0f, by = ast_ship_y - s * 4.0f;
    display.drawLine((int)bx, (int)by, (int)fx, (int)fy, col);
  }
}

static void drawAstRock(const AstRock &r) {
  // Walk the outline; iteration AST_ROCK_VERTS revisits vertex 0 to close it
  int px = 0, py = 0;
  for (int v = 0; v <= AST_ROCK_VERTS; v++) {
    int idx = v % AST_ROCK_VERTS;
    float a = r.angle + (TWO_PI * idx) / AST_ROCK_VERTS;
    int x = (int)(r.x + cosf(a) * r.vertRadius[idx]);
    int y = (int)(r.y + sinf(a) * r.vertRadius[idx]);
    if (v > 0) display.drawLine(px, py, x, y, SPRITE_COLOR(COL_AST_ROCK));
    px = x; py = y;
  }
}

static void drawAstShards() {
  for (int i = 0; i < AST_MAX_SHARDS; i++) {
    if (!ast_shards[i].active) continue;
    const AstShard &sh = ast_shards[i];
    // Line shard shrinks as it burns out
    float halfLen = 3.0f * (sh.life / AST_SHARD_LIFE);
    if (halfLen < 0.5f) halfLen = 0.5f;
    float c = cosf(sh.angle) * halfLen, s = sinf(sh.angle) * halfLen;
    display.drawLine((int)(sh.x - c), (int)(sh.y - s),
                     (int)(sh.x + c), (int)(sh.y + s),
                     sh.fromDigit ? SPRITE_COLOR(COL_DIGITS) : SPRITE_COLOR(COL_AST_ROCK));
  }
}

void displayClockWithAsteroids() {
  if (!ast_init_done) resetAsteroidsAnimation();

  struct tm timeinfo;
  if (!getTimeWithTimeout(&timeinfo)) {
    display.setTextSize(1);
    display.setCursor(20, 28);
    display.print(ntpSynced ? "Time Error" : "Syncing time...");
    return;
  }

  updateAsteroidsAnimation(&timeinfo);

  if (!time_overridden) syncDisplayedTime(&timeinfo);
  maintainTimeOverride(&timeinfo, ast_phase == AST_IDLE);

  int gy = astTimeY();

  // Space layer: rocks, debris, bullet, ship - all drawn first so the
  // digit plates mask them where they overlap.
  for (int i = 0; i < AST_MAX_ROCKS; i++) {
    if (ast_rocks[i].active) drawAstRock(ast_rocks[i]);
  }
  drawAstShards();
  drawAstShip();

  // Time digits (size 3). Mask each visible digit's box so the space layer
  // reads as flying *behind* the time, then print the glyph. The digit
  // being shattered stays hidden until its debris clears.
  display.setTextSize(3);
  display.setTextColor(SPRITE_COLOR(COL_DIGITS));
  char dch[5];
  dch[0] = '0' + displayed_hour / 10;
  dch[1] = '0' + displayed_hour % 10;
  dch[2] = shouldShowColon() ? ':' : ' ';
  dch[3] = '0' + displayed_min / 10;
  dch[4] = '0' + displayed_min % 10;

  int shatterIdx = -1;
  if (ast_phase == AST_SHATTER) shatterIdx = ast_change_idx[ast_cur_change];

  for (int i = 0; i < 5; i++) {
    if (i == shatterIdx) continue;
    int dx = DIGIT_X[i];
    int dy = gy + ((i == 2) ? 0 : (int)digit_offset_y[i]);
    // Solid plates mask the space layer behind each digit; transparent mode
    // lets the rocks and ship show through the glyph gaps instead.
    if (!settings.asteroidsTransparent) {
      display.fillRect(dx - 1, dy - 1, AST_DIGIT_W + 2, AST_DIGIT_H + 2,
                       DISPLAY_BLACK);
    }
    display.setCursor(dx, dy);
    display.print(dch[i]);
  }
  display.setTextColor(DISPLAY_WHITE);  // restore for date chrome

  // Bullet drawn over the plates as a 2px tracer, so a shot crossing the
  // time never blinks out of existence mid-flight
  if (ast_bullet_active) {
    uint16_t bcol = SPRITE_COLOR(COL_AST_SHIP);  // ship's shot
    display.drawPixel((int)ast_bullet_x, (int)ast_bullet_y, bcol);
    display.drawPixel((int)(ast_bullet_x - ast_bullet_vx * 0.01f),
                      (int)(ast_bullet_y - ast_bullet_vy * 0.01f), bcol);
  }

  // Optional date row (top), kept above the action
  if (settings.asteroidsShowDate) {
    display.setTextSize(1);
    char dateStr[12];
    switch (settings.dateFormat) {
      case 0: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
      case 1: sprintf(dateStr, "%02d/%02d/%04d", timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_year + 1900); break;
      case 2: sprintf(dateStr, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday); break;
      case 3: sprintf(dateStr, "%02d.%02d.%04d", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900); break;
    }
    int dateX = (SCREEN_WIDTH - 60) / 2;
    if (!settings.asteroidsTransparent) {
      display.fillRect(dateX - 1, 3, 62, 9, DISPLAY_BLACK);
    }
    display.setCursor(dateX, 4);
    display.print(dateStr);
  }
  drawMeridiemIndicator(110, 4, displayed_is_pm);

  if (!wifiConnected) drawNoWiFiIcon(0, 0);
}
