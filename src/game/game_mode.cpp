/*
 * AnimatedPixelClock - Game Mode
 *
 * Owns the pad lifecycle around the games: a pairing screen until a pad
 * connects, then the game menu (last pick remembered) and the chosen game,
 * with a pause overlay while the pad is gone. Gives up (and frees the radio) if no pad shows up, a lost pad
 * does not come back, or nobody touches the pad for gameIdleExitMin minutes.
 * The panel is held at normal brightness for the whole time (no night
 * schedule) and handed back to the schedule on exit.
 */

#include "../config/user_config.h"

#if GAMEPAD_ENABLED

#include "../config/config.h"
#include "../display/display.h"
#include "../display/panel_config.h"
#include "../notify/notify.h"
#include "game_mode.h"
#include "game_common.h"
#include <Preferences.h>

// Colour depth while BLE is up. NimBLE needs ~45KB of internal SRAM, which the
// panel's full-depth DMA buffers and descriptors do not leave; 5 bits frees ~60KB.
#define GAME_PANEL_DEPTH 5
#define GAME_PAIR_TIMEOUT_MS 120000UL
#define GAME_LOST_TIMEOUT_MS 60000UL
#define GAME_STICK_ACTIVITY 8000   // deflection that counts as input for the idle timer
#define GAME_TRIGGER_ACTIVITY 100

static bool active = false;
static bool restorePanel = false;  // back to full depth once BLE has let go of its memory
static bool everConnected = false;
static unsigned long enteredAt = 0;
static unsigned long lostAt = 0;
static unsigned long lastInputAt = 0;
static uint16_t lastButtons = 0;

struct GameDef {
  const char *id;  // API / export name
  const char *name;
  const char *hiKey;
  void (*reset)();
  bool (*frame)(const GamepadState &, bool);
};
static const GameDef GAMES[] = {
    {"blocks", "Falling Blocks", "blocksHi", blocksReset, blocksFrame},
    {"snake", "Snake", "snakeHi", snakeReset, snakeFrame},
    {"bricks", "Bricks", "bricksHi", bricksReset, bricksFrame},
    {"rocks", "Space Rocks", "rocksHi", rocksReset, rocksFrame},
    {"runner", "Runner", "runnerHi", runnerReset, runnerFrame},
};
static const uint8_t GAME_COUNT = sizeof(GAMES) / sizeof(GAMES[0]);

static int8_t current = -1;  // running game, -1 = menu
static uint8_t selected = 0;
static uint32_t menuHi[GAME_COUNT];

static void openMenu() {
  current = -1;
  for (uint8_t i = 0; i < GAME_COUNT; i++) menuHi[i] = gameLoadHi(GAMES[i].hiKey);
}

uint8_t gameCount() { return GAME_COUNT; }
const char *gameId(uint8_t i) { return GAMES[i].id; }
const char *gameName(uint8_t i) { return GAMES[i].name; }
uint32_t gameHiScore(uint8_t i) { return gameLoadHi(GAMES[i].hiKey); }

void gameSetHiScore(uint8_t i, uint32_t score) {
  gameStoreHi(GAMES[i].hiKey, score);
  menuHi[i] = score;
}

static void launchGame(uint8_t i) {
  current = i;
  GAMES[i].reset();
  Preferences p;
  p.begin("game", false);
  p.putUChar("lastGame", i);
  p.end();
}

void gameModeStart() {
  if (active) return;
  if (!setPanelColorDepth(GAME_PANEL_DEPTH)) {
    Serial.println("Game mode: panel restart failed");
    return;
  }
  active = true;
  restorePanel = false;
  notifyDismiss();
  everConnected = false;
  lostAt = 0;
  enteredAt = millis();
  lastInputAt = enteredAt;
  lastButtons = 0;
  setDisplayGameOverride(true);
  Preferences p;
  selected = p.begin("game", true) ? p.getUChar("lastGame", 0) : 0;
  p.end();
  if (selected >= GAME_COUNT) selected = 0;
  openMenu();
  gamepadStart();
  Serial.println("Game mode: started");
}

void gameModeStop() {
  if (!active) return;
  active = false;
  gamepadStop();
  restorePanel = true;
  setDisplayGameOverride(false);
  Serial.println("Game mode: stopped");
}

bool gameModeActive() { return active; }

void gameModeLoop() {
  if (!restorePanel || !gamepadIdle()) return;
  restorePanel = false;
  setPanelColorDepth(panelOptions.colorDepth);
}

static void drawPairingScreen(GamepadLink link, unsigned long now) {
  display.setTextSize(1);
  display.setTextColor(GC_YELLOW);
  gamePrintCentered(0, "GAME MODE");
  display.drawFastHLine(0, 10, 128, GC_DIM);
  display.setTextColor(DISPLAY_WHITE);
  gamePrintCentered(14, "New pad: hold pair");
  gamePrintCentered(24, "button for 3 s");
  gamePrintCentered(34, "Paired: press Xbox");

  char status[22];
  uint8_t dots = (now / 400) % 4;
  const char *what = link == GP_LINK_CONNECTING ? "Pairing" : "Searching";
  snprintf(status, sizeof(status), "%s%.*s", what, dots, "...");
  display.setTextColor(GC_CYAN);
  display.setCursor(0, 54);
  display.print(status);
  unsigned long left = (GAME_PAIR_TIMEOUT_MS - (now - enteredAt)) / 1000;
  display.setTextColor(GC_GREY);
  display.setCursor(104, 54);
  display.print(left);
  display.print("s");
}

// Pad battery between the title and the clock: red and blinking when low.
static void drawMenuBattery() {
  uint8_t b = gamepadBattery();
  if (!b) return;
  bool low = gameBatteryLow();
  if (low && (millis() / 500) % 2) return;
  uint16_t c = low ? GC_RED : b <= 50 ? GC_YELLOW : GC_GREEN;
  const int x = 35, y = 2;
  display.drawRect(x, y, 11, 6, c);
  display.drawFastVLine(x + 11, y + 2, 2, c);
  int w = (b * 9 + 99) / 100;
  if (w) display.fillRect(x + 1, y + 1, w, 4, c);
  display.setTextColor(c);
  display.setCursor(x + 15, 1);
  display.print(b);
  display.print(low ? "% LOW" : "%");
}

// Game list under the band; d-pad picks, A starts, View leaves game mode.
static bool menuFrame(const GamepadState &in, bool padLost) {
  if (!padLost) {
    if (in.pressed & GP_UP) selected = (selected + GAME_COUNT - 1) % GAME_COUNT;
    if (in.pressed & GP_DOWN) selected = (selected + 1) % GAME_COUNT;
    if (in.pressed & GP_VIEW) return false;
    if (in.pressed & (GP_A | GP_MENU)) {
      launchGame(selected);
      return true;
    }
  }

  display.setTextSize(1);
  display.setTextColor(GC_YELLOW);
  display.setCursor(0, 1);
  display.print("GAMES");
  drawMenuBattery();
  gameDrawClock(false);
  display.drawFastHLine(0, 10, SCREEN_WIDTH, GC_DIM);
  for (uint8_t i = 0; i < GAME_COUNT; i++) {
    int y = 12 + i * 10;
    bool sel = i == selected;
    if (sel) display.fillRect(0, y, SCREEN_WIDTH, 10, GC_NAVY);
    display.setTextColor(sel ? GC_WHITE : GC_GREY);
    display.setCursor(3, y + 1);
    display.print(GAMES[i].name);
    if (menuHi[i]) {
      char hi[11];
      snprintf(hi, sizeof(hi), "%lu", (unsigned long)menuHi[i]);
      display.setTextColor(sel ? GC_YELLOW : GC_DIM);
      display.setCursor(SCREEN_WIDTH - 3 - strlen(hi) * 6, y + 1);
      display.print(hi);
    }
  }
  display.setTextColor(DISPLAY_WHITE);
  if (padLost) gameDrawOverlay(G_PAUSED, true, nullptr, nullptr, false);
  return true;
}

// Runs the menu or the current game; a game handing back false returns to the menu.
static bool runFrame(const GamepadState &in, bool padLost) {
  if (current < 0) return menuFrame(in, padLost);
  if (!GAMES[current].frame(in, padLost)) openMenu();
  return true;
}

void displayGameMode() {
  unsigned long now = millis();
  GamepadLink link = gamepadLink();
  GamepadState in;
  gamepadRead(&in);

  if (link == GP_LINK_CONNECTED) {
    if (!everConnected || lostAt) lastInputAt = now;
    everConnected = true;
    lostAt = 0;
    if (in.pressed || in.buttons != lastButtons || abs(in.rx) > GAME_STICK_ACTIVITY ||
        abs(in.ry) > GAME_STICK_ACTIVITY || in.lt > GAME_TRIGGER_ACTIVITY ||
        in.rt > GAME_TRIGGER_ACTIVITY)
      lastInputAt = now;
    lastButtons = in.buttons;
    if (!runFrame(in, false)) {
      gameModeStop();
    } else if (settings.gameIdleExitMin &&
               now - lastInputAt >= settings.gameIdleExitMin * 60000UL) {
      Serial.println("Game mode: idle timeout");
      gameModeStop();
    }
    return;
  }

  if (!everConnected) {
    if (now - enteredAt >= GAME_PAIR_TIMEOUT_MS) {
      gameModeStop();
      return;
    }
    drawPairingScreen(link, now);
    return;
  }

  if (!lostAt) lostAt = now;
  runFrame(in, true);
  if (now - lostAt >= GAME_LOST_TIMEOUT_MS) gameModeStop();
}

#endif // GAMEPAD_ENABLED
