/*
 * AnimatedPixelClock - Settings Module
 *
 * Load and save settings to ESP32 Preferences (flash storage).
 */

#include "settings.h"
#include "../config/config.h"
#include "../timezones.h"
#include <Preferences.h>


Preferences preferences;

// Default sprite-color palette (RGB565), indexed by ColorSlot (color_slots.h).
// Real definition for the header's extern. APPEND-ONLY: keep in sync with the enum.
const uint16_t SPRITE_COLOR_DEFAULTS[] = {
    /* COL_DIGITS         */ 0xFFFF,  // white
    /* COL_MARIO_HAT      */ 0xF800,  // red
    /* COL_MARIO_OVERALLS */ 0x001F,  // blue
    /* COL_MARIO_SKIN     */ 0xFDB8,  // tan
    /* COL_MARIO_SHOES    */ 0xA145,  // brown
    /* COL_PACMAN         */ 0xFFE0,  // yellow
    /* COL_PELLET         */ 0xFFE0,  // yellow
    /* COL_SNAKE          */ 0x07E0,  // green
    /* COL_INVADER        */ 0x07E0,  // green
    /* COL_LASER          */ 0xF800,  // red
    /* COL_SNAKE_FOOD     */ 0xF800,  // red
    /* COL_TET_I          */ 0x07FF,  // cyan
    /* COL_TET_O          */ 0xFFE0,  // yellow
    /* COL_TET_T          */ 0x8010,  // purple
    /* COL_TET_S          */ 0x07E0,  // green
    /* COL_TET_Z          */ 0xF800,  // red
    /* COL_TET_J          */ 0x001F,  // blue
    /* COL_TET_L          */ 0xFC00,  // orange
    /* COL_DINO           */ 0xFFFF,  // white
    /* COL_DINO_CACTUS    */ 0x07E0,  // green
    /* COL_DINO_PTERO     */ 0xFFFF,  // white
    /* COL_DINO_GROUND    */ 0xFFFF,  // white
    /* COL_DINO_CLOUD     */ 0xFFFF,  // white
    /* COL_AST_SHIP       */ 0x07FF,  // cyan
    /* COL_AST_ROCK       */ 0xA145,  // tan
    /* COL_PONG_BALL      */ 0xFFFF,  // white
    /* COL_PONG_PADDLE    */ 0x07FF,  // cyan
    /* COL_GOOMBA         */ 0xA145,  // brown
    /* COL_SPINY          */ 0xFC00,  // orange
    /* COL_KOOPA          */ 0x07E0,  // green
    /* COL_COIN           */ 0xFFE0,  // yellow
    /* COL_STAR           */ 0xFFE0,  // yellow
    /* COL_MUSHROOM       */ 0xF800,  // red
    /* COL_FIREBALL       */ 0xFC00,  // orange
    /* COL_STAT_TEXT      */ 0xFFFF,  // white
    /* COL_STAT_BAR       */ 0xFFFF,  // white
    /* COL_STAT_BAR_BG    */ 0xFFFF,  // white
    /* COL_MATRIX_RAIN    */ 0x07E8,  // matrix green (#00ff41)
    /* COL_MATRIX_HEAD    */ 0xDFFB,  // white-green head (#d8ffd8)
    /* COL_MC_MISSILE     */ 0xF800,  // red
    /* COL_MC_COUNTER     */ 0x07FF,  // cyan
    /* COL_MC_EXPLOSION   */ 0xFFE0,  // yellow
    /* COL_MC_CITY        */ 0x34DF,  // blue (#3399ff)
    /* COL_MC_GROUND      */ 0xBC40,  // ochre (#bf8800)
    /* COL_DIGITS_S0..S13 */ 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,
                             0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF,  // per-style digits, white
};
// Every ColorSlot must have a default here, else it silently defaults to black.
static_assert(sizeof(SPRITE_COLOR_DEFAULTS) / sizeof(SPRITE_COLOR_DEFAULTS[0]) == COL_COUNT,
              "every ColorSlot needs a default in SPRITE_COLOR_DEFAULTS");
// digitColor() relies on the 14 per-style digit slots being contiguous, in order.
static_assert(COL_DIGITS_S13 - COL_DIGITS_S0 == 13,
              "COL_DIGITS_S0..S13 must be 14 contiguous slots in ascending order");

// Zero saved brightness would leave the panel permanently dark with no local
// control to recover it (runtime off goes through /api/display, not settings),
// so persisted brightness values are clamped to a minimum of 1.
uint8_t sanitizeBrightnessValue(uint8_t value) {
  return value == 0 ? 1 : value;
}

bool isZeroBrightnessAllowed() {
  return false;
}

void sanitizeBrightnessSettings() {
  settings.displayBrightness = sanitizeBrightnessValue(settings.displayBrightness);
  settings.dimBrightness = sanitizeBrightnessValue(settings.dimBrightness);
}

void loadSettings() {
  // Try to open preferences namespace (create if doesn't exist)
  if (!preferences.begin("pcmonitor",
                         false)) { // Read-write mode to create if needed
    Serial.println("WARNING: Failed to open preferences, using defaults");
    // Initialize with defaults
    settings.clockStyle = 0;
    settings.gmtOffset = 60;  // GMT+1 (Central European)
    settings.daylightSaving = true;
    strcpy(settings.timezoneString, "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00"); // Default: Central European
    settings.use24Hour = true;
    settings.dateFormat = 0;
    settings.clockPosition = 0; // Center by default
    settings.clockOffset = 0;   // No offset by default
    settings.showClock = true;
    settings.displayRowMode = 0;         // Default: 5 rows with more spacing
    settings.useRpmKFormat = false;      // Default: Full RPM format (1800RPM)
    settings.useNetworkMBFormat = false; // Default: Full KB/s format
    strcpy(settings.deviceName, "pixelclock");
    settings.colonBlinkMode = 1;         // Default: Blink
    settings.colonBlinkRate = 10; // Default: 1.0 Hz (10 = 1.0Hz in tenths)
    settings.refreshRateMode = 0; // Default: Auto
    settings.refreshRateHz = 10;  // Default manual rate: 10 Hz
    settings.boostAnimationRefresh =
        true;                        // Default: Enable smooth animation boost
    settings.displayBrightness = sanitizeBrightnessValue(255);
    settings.enableScheduledDimming = false;
    settings.dimStartHour = 22;
    settings.dimEndHour = 7;
    settings.dimBrightness = sanitizeBrightnessValue(50);
    settings.marioBounceHeight = 35; // Default: 3.5 (35 = 3.5 in tenths)
    settings.marioBounceSpeed = 6;   // Default: 0.6 (6 = 0.6 in tenths)
    settings.marioSmoothAnimation = false; // Default: 2-frame animation
    settings.marioWalkSpeed = 20;    // Default: 2.0 (20 = 2.0 in tenths)
    settings.pongBallSpeed = 18;     // Default: 18 (1.125 px/frame)
    settings.pongBounceStrength = 3; // Default: 0.3 (3 = 0.3 in tenths)
    settings.pongBounceDamping = 85; // Default: 0.85 (85 = 0.85 in hundredths)
    settings.pongPaddleWidth = 20;   // Default: 20 pixels
    settings.pongHorizontalBounce = true; // Default: enabled
    settings.pongDigitShatter = true; // Default: shatter/reassemble enabled
    settings.spaceCharacterType = 1; // Default: Ship (1 = Ship, 0 = Invader)
    settings.spacePatrolSpeed = 5;   // Default: 0.5 (5 = 0.5 in tenths)
    settings.spaceAttackSpeed = 25;  // Default: 2.5 (25 = 2.5 in tenths)
    settings.spaceLaserSpeed = 40;   // Default: 4.0 (40 = 4.0 in tenths)
    settings.spaceExplosionGravity = 5; // Default: 0.5 (5 = 0.5 in tenths)
    // Initialize all metrics with defaults
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricLabels[i][0] = '\0'; // Empty = use Python name
      settings.metricNames[i][0] = '\0';  // Empty = no stored name
      settings.metricOrder[i] = i;        // Default order
      settings.metricCompanions[i] = 0;   // No companion by default
      settings.metricPositions[i] =
          255; // Default: None/Hidden (user must assign position)
      settings.metricBarPositions[i] = 255; // Default: No progress bar
      settings.metricBarMin[i] = 0;
      settings.metricBarMax[i] = 100;
      settings.metricBarWidths[i] =
          60; // Default width (60px for left, 64px for right)
      settings.metricBarOffsets[i] = 0; // Default: no offset
    }
    Serial.println("Settings initialized with defaults");
    return;
  }

  // Check if this is a fresh namespace (no settings saved yet)
  if (!preferences.isKey("clockStyle")) {
    Serial.println(
        "Fresh preferences namespace detected, initializing with defaults...");
    // Write defaults to NVS
    preferences.putInt("clockStyle", 0);
    preferences.putInt("gmtOffset", 60); // GMT+1 (Central European)
    preferences.putBool("dst", true);
    preferences.putString("tz", "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00"); // Default: Central European
    preferences.putBool("use24Hour", true);
    preferences.putInt("dateFormat", 0);
    preferences.putInt("clockPos", 0);    // Center
    preferences.putInt("clockOffset", 0); // No offset
    preferences.putBool("showClock", true);
    preferences.putInt("rowMode", 0);         // Default: 5 rows
    preferences.putBool("rpmKFormat", false); // Default: Full RPM format
    preferences.putUChar("colonBlink", 1);    // Default: Blink
    preferences.putUChar("colonRate", 10);    // Default: 1.0 Hz
    preferences.putUChar("refreshMode", 0);   // Default: Auto
    preferences.putUChar("refreshHz", 10);    // Default: 10 Hz
    preferences.putBool("boostAnim", true);   // Default: Enable animation boost
    preferences.putUChar("brightness", sanitizeBrightnessValue(255));
    preferences.putBool("schedDim", false);
    preferences.putUChar("dimStart", 22);
    preferences.putUChar("dimEnd", 7);
    preferences.putUChar("dimBright", sanitizeBrightnessValue(50));
    preferences.putUChar("marioBnceH", 35);   // Default: 3.5
    preferences.putUChar("marioBnceS", 6);    // Default: 0.6
    preferences.putBool("marioSmooth", false); // Default: 2-frame animation
    preferences.putUChar("marioWalkSpd", 20); // Default: 2.0
    preferences.putBool("marioEnctr", false); // Default: no idle encounters
    preferences.putUChar("marioEncFrq", 1);   // Default: Normal frequency
    preferences.putUChar("marioEncSpd", 1);   // Default: Normal speed
    preferences.putUChar("pongBallSpd", 18);  // Default: 18
    preferences.putUChar("pongBncStr", 3);    // Default: 0.3
    preferences.putUChar("pongBncDmp", 85);   // Default: 0.85
    preferences.putUChar("pongPadWid", 20);   // Default: 20
    preferences.putUChar("spaceChar", 1);     // Default: Ship
    preferences.putUChar("spacePatrol", 5);   // Default: 0.5
    preferences.putUChar("spaceAttack", 25);  // Default: 2.5
    preferences.putUChar("spaceLaser", 40);   // Default: 4.0
    preferences.putUChar("spaceExpGrv", 5);   // Default: 0.5

    // Initialize all metrics with default values
    uint8_t defaultOrder[MAX_METRICS];
    uint8_t defaultCompanions[MAX_METRICS];
    for (int i = 0; i < MAX_METRICS; i++) {
      defaultOrder[i] = i;
      defaultCompanions[i] = 0; // No companion
    }
    preferences.putBytes("metricOrd", defaultOrder, MAX_METRICS);
    preferences.putBytes("metricComp", defaultCompanions, MAX_METRICS);

    Serial.println("Default settings written to NVS");
  }

  settings.clockStyle = preferences.getInt("clockStyle", 0); // Default: Mario

  // gmtOffset migration: convert old hours to new minutes format
  int loadedOffset = preferences.getInt("gmtOffset", 60);
  if (loadedOffset >= -12 && loadedOffset <= 14 && loadedOffset != 0) {
    // Old format (hours): convert to minutes
    settings.gmtOffset = loadedOffset * 60;
    preferences.putInt("gmtOffset", settings.gmtOffset); // Save new format
  } else {
    settings.gmtOffset = loadedOffset; // Already in minutes
  }

  settings.daylightSaving = preferences.getBool("dst", true);  // Default: true

  // Timezone migration: migrate from old gmtOffset + dst to new timezoneString
  if (preferences.isKey("tz")) {
    // New format exists, load it
    String loadedTz = preferences.getString("tz", "");
    if (loadedTz.length() > 0) {
      strncpy(settings.timezoneString, loadedTz.c_str(), 63);
      settings.timezoneString[63] = '\0';
      settings.timezoneIndex = preferences.getUChar("tzIdx", 255);

      // Auto-heal: if the user picked a region from the dropdown (a valid
      // index was stored), re-derive the POSIX string from the current
      // database. This lets corrected DST/offset rules (e.g. Turkey now
      // UTC+3) take effect after a firmware update without the user having
      // to re-select. Migrated or imported configs use index 255 and keep
      // their stored string untouched.
      if (settings.timezoneIndex < 255) {
        size_t tzCount = 0;
        const TimezoneRegion* regions = getSupportedTimezones(&tzCount);
        if (settings.timezoneIndex < tzCount) {
          strncpy(settings.timezoneString, regions[settings.timezoneIndex].posixString, 63);
          settings.timezoneString[63] = '\0';
          settings.gmtOffset = regions[settings.timezoneIndex].gmtOffsetMinutes;
        }
      }

      Serial.printf("Loaded timezone string: %s (index: %d)\n", settings.timezoneString, settings.timezoneIndex);
    } else {
      // Key exists but empty, set default
      strcpy(settings.timezoneString, "CET-1CEST,M3.5.0/02:00,M10.5.0/03:00");
    }
  } else {
    // Old format: migrate to default timezone based on gmtOffset
    const char* defaultTz = getDefaultTimezoneForOffset(settings.gmtOffset);
    if (defaultTz != nullptr) {
      strncpy(settings.timezoneString, defaultTz, 63);
      settings.timezoneString[63] = '\0';
      // Save new format
      preferences.putString("tz", settings.timezoneString);
      Serial.printf("Migrated gmtOffset %d + DST %d to timezone: %s\n",
                    settings.gmtOffset, settings.daylightSaving, settings.timezoneString);
    } else {
      // No automatic mapping available, default to UTC
      strcpy(settings.timezoneString, "UTC0");
      Serial.printf("Warning: No automatic timezone for gmtOffset %d, defaulting to UTC\n",
                    settings.gmtOffset);
    }
  }

  settings.use24Hour = preferences.getBool("use24Hour", true); // Default: 24h
  settings.dateFormat =
      preferences.getInt("dateFormat", 0); // Default: DD/MM/YYYY
  settings.clockPosition = preferences.getInt("clockPos", 0); // Default: Center
  settings.clockOffset =
      preferences.getInt("clockOffset", 0); // Default: No offset
  settings.showClock = preferences.getBool("showClock", true);
  settings.displayRowMode = preferences.getInt("rowMode", 0); // Default: 5 rows
  settings.useRpmKFormat =
      preferences.getBool("rpmKFormat", false); // Default: Full RPM format
  settings.useNetworkMBFormat =
      preferences.getBool("netMBFormat", false); // Default: Full KB/s format
  settings.colonBlinkMode =
      preferences.getUChar("colonBlink", 1); // Default: Blink
  settings.colonBlinkRate =
      preferences.getUChar("colonRate", 10); // Default: 1.0 Hz
  settings.refreshRateMode =
      preferences.getUChar("refreshMode", 0); // Default: Auto
  settings.refreshRateHz =
      preferences.getUChar("refreshHz", 10); // Default: 10 Hz
  settings.boostAnimationRefresh =
      preferences.getBool("boostAnim", true); // Default: Enable
  settings.displayBrightness =
      preferences.getUChar("brightness", 255); // Default: 255 (max)
  settings.enableScheduledDimming =
      preferences.getBool("schedDim", false); // Default: Disabled
  settings.dimStartHour =
      preferences.getUChar("dimStart", 22); // Default: 10 PM
  settings.dimEndHour =
      preferences.getUChar("dimEnd", 7); // Default: 7 AM
  settings.dimBrightness =
      preferences.getUChar("dimBright", 50); // Default: ~20% (50/255)
  settings.marioBounceHeight =
      preferences.getUChar("marioBnceH", 35); // Default: 3.5
  settings.marioBounceSpeed =
      preferences.getUChar("marioBnceS", 6); // Default: 0.6
  settings.marioSmoothAnimation =
      preferences.getBool("marioSmooth", false); // Default: 2-frame
  settings.marioWalkSpeed =
      preferences.getUChar("marioWalkSpd", 20); // Default: 2.0
  settings.marioIdleEncounters =
      preferences.getBool("marioEnctr", false); // Default: disabled
  settings.marioEncounterFreq =
      preferences.getUChar("marioEncFrq", 1); // Default: Normal
  settings.marioEncounterSpeed =
      preferences.getUChar("marioEncSpd", 1); // Default: Normal
  settings.pongBallSpeed =
      preferences.getUChar("pongBallSpd", 18); // Default: 18
  settings.pongBounceStrength =
      preferences.getUChar("pongBncStr", 3); // Default: 0.3
  settings.pongBounceDamping =
      preferences.getUChar("pongBncDmp", 85); // Default: 0.85
  settings.pongPaddleWidth =
      preferences.getUChar("pongPadWid", 20); // Default: 20
  settings.pongHorizontalBounce =
      preferences.getBool("pongHorizBnc", true); // Default: true
  settings.pongDigitShatter =
      preferences.getBool("pongShatter", true); // Default: true
  settings.pacmanSpeed =
      preferences.getUChar("pacmanSpeed", 10); // Default: 1.0 patrol speed
  settings.pacmanEatingSpeed =
      preferences.getUChar("pacmanEatSpeed", 20); // Default: 2.0 eating speed
  settings.pacmanMouthSpeed =
      preferences.getUChar("pacmanMouthSpd", 10); // Default: 1.0 Hz (shortened key name)
  settings.pacmanPelletCount =
      preferences.getUChar("pacmanPellCount", 8); // Default: 8 (shortened key name)
  settings.pacmanPelletRandomSpacing =
      preferences.getBool("pacmanPellRand", true); // Default: true (shortened key name)
  settings.pacmanBounceEnabled =
      preferences.getBool("pacmanBounce", true); // Default: true
  settings.spaceCharacterType =
      preferences.getUChar("spaceChar", 1); // Default: Ship
  settings.spacePatrolSpeed =
      preferences.getUChar("spacePatrol", 5); // Default: 0.5
  settings.spaceAttackSpeed =
      preferences.getUChar("spaceAttack", 25); // Default: 2.5
  settings.spaceLaserSpeed =
      preferences.getUChar("spaceLaser", 40); // Default: 4.0
  settings.spaceExplosionGravity =
      preferences.getUChar("spaceExpGrv", 5); // Default: 0.5
  settings.snakeSpeed =
      preferences.getUChar("snakeSpeed", 12); // Default: 1.2 px/frame
  settings.snakeLength =
      preferences.getUChar("snakeLen", 8); // Default: 8 segments
  settings.snakeWallBorder =
      preferences.getBool("snakeBorder", false); // Default: no frame
  settings.snakeShowDate =
      preferences.getBool("snakeDate", false); // Default: hidden (centred clock)
  settings.tetrisFallSpeed =
      preferences.getUChar("tetFallSpd", 12); // Default: 1.2
  settings.tetrisBlockStyle =
      preferences.getUChar("tetBlockSty", 0); // Default: LCD grid
  settings.tetrisIdleTumble =
      preferences.getBool("tetIdleTmbl", true); // Default: on
  settings.tetrisAnimStyle =
      preferences.getUChar("tetAnimSty", 1); // Default: falling dots
  settings.tetrisShowDate =
      preferences.getBool("tetShowDate", true); // Default: show date
  settings.tetrisDatePosition =
      preferences.getUChar("tetDatePos", 1); // Default: bottom
  settings.tetrisDotSpeed =
      preferences.getUChar("tetDotSpd", 12); // Default: 1.2
  settings.tetrisDotOrder =
      preferences.getUChar("tetDotOrd", 0); // Default: bottom-up
  settings.tetrisDigitBounce =
      preferences.getBool("tetBounce", true); // Default: bounce on
  settings.tetrisSmoothGame =
      preferences.getBool("tetSmooth", false); // Default: real-game (off)
  settings.asteroidsShipSpeed =
      preferences.getUChar("astShipSpd", 12); // Default: 1.2
  settings.asteroidsRockCount =
      preferences.getUChar("astRocks", 2); // Default: 2 rocks
  settings.asteroidsRockSpeed =
      preferences.getUChar("astRockSpd", 8); // Default: 0.8
  settings.asteroidsShowDate =
      preferences.getBool("astDate", false); // Default: hidden (centred clock)
  settings.asteroidsTransparent =
      preferences.getBool("astTransp", true); // Default: transparent digits
  settings.dinoSpeed =
      preferences.getUChar("dinoSpeed", 12); // Default: 1.2
  settings.dinoCactusFreq =
      preferences.getUChar("dinoCactus", 1); // Default: normal
  settings.dinoShowClouds =
      preferences.getBool("dinoClouds", true); // Default: clouds on
  settings.dinoShowDate =
      preferences.getBool("dinoDate", false); // Default: hidden (centred clock)
  settings.matrixRainSpeed =
      preferences.getUChar("mxSpeed", 12); // Default: 1.2
  settings.matrixRainDensity =
      preferences.getUChar("mxDensity", 1); // Default: normal
  settings.matrixShowDate =
      preferences.getBool("mxDate", false); // Default: hidden (centred clock)
  settings.matrixTransparent =
      preferences.getBool("mxTransp", false); // Default: solid digit plates
  settings.mcMissileSpeed =
      preferences.getUChar("mcSpeed", 12); // Default: 1.2
  settings.mcMissileFreq =
      preferences.getUChar("mcFreq", 1); // Default: normal volleys
  settings.mcShowDate =
      preferences.getBool("mcDate", false); // Default: hidden (centred clock)

  bool brightnessSettingsSanitized = false;
  uint8_t sanitizedDisplayBrightness =
      sanitizeBrightnessValue(settings.displayBrightness);
  if (sanitizedDisplayBrightness != settings.displayBrightness) {
    settings.displayBrightness = sanitizedDisplayBrightness;
    brightnessSettingsSanitized = true;
  }

  uint8_t sanitizedDimBrightness = sanitizeBrightnessValue(settings.dimBrightness);
  if (sanitizedDimBrightness != settings.dimBrightness) {
    settings.dimBrightness = sanitizedDimBrightness;
    brightnessSettingsSanitized = true;
  }

  // Load network configuration
  String loadedDeviceName = preferences.getString("deviceName", "pixelclock");
  strncpy(settings.deviceName, loadedDeviceName.c_str(), 31);
  settings.deviceName[31] = '\0';
  settings.showIPAtBoot =
      preferences.getBool("showIPBoot", true); // Default: Show IP at startup
  settings.useStaticIP =
      preferences.getBool("useStaticIP", false); // Default: DHCP
  String loadedIP = preferences.getString("staticIP", "192.168.1.100");
  String loadedGW = preferences.getString("gateway", "192.168.1.1");
  String loadedSN = preferences.getString("subnet", "255.255.255.0");
  String loadedDNS1 = preferences.getString("dns1", "8.8.8.8");
  String loadedDNS2 = preferences.getString("dns2", "8.8.4.4");

  strncpy(settings.staticIP, loadedIP.c_str(), 15);
  settings.staticIP[15] = '\0';
  strncpy(settings.gateway, loadedGW.c_str(), 15);
  settings.gateway[15] = '\0';
  strncpy(settings.subnet, loadedSN.c_str(), 15);
  settings.subnet[15] = '\0';
  strncpy(settings.dns1, loadedDNS1.c_str(), 15);
  settings.dns1[15] = '\0';
  strncpy(settings.dns2, loadedDNS2.c_str(), 15);
  settings.dns2[15] = '\0';

  // Load metric display order
  size_t orderSize = preferences.getBytesLength("metricOrd");
  if (orderSize == MAX_METRICS) {
    preferences.getBytes("metricOrd", settings.metricOrder, MAX_METRICS);
    Serial.println("Loaded metric order from NVS");
  } else {
    // Default sequential order if not found
    Serial.println("Initializing metric order to default (0-11)");
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricOrder[i] = i;
    }
    preferences.putBytes("metricOrd", settings.metricOrder, MAX_METRICS);
  }

  // Load companion metrics
  size_t companionSize = preferences.getBytesLength("metricComp");
  if (companionSize == MAX_METRICS) {
    preferences.getBytes("metricComp", settings.metricCompanions, MAX_METRICS);
    Serial.println("Loaded metric companions from NVS");
  } else {
    // Default no companions if not found
    Serial.println("Initializing companions to none (0)");
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricCompanions[i] = 0;
    }
    preferences.putBytes("metricComp", settings.metricCompanions, MAX_METRICS);
  }

  // Load metric positions
  size_t posSize = preferences.getBytesLength("metricPos");
  if (posSize == MAX_METRICS) {
    preferences.getBytes("metricPos", settings.metricPositions, MAX_METRICS);
    Serial.println("Loaded metric positions from NVS");
  } else {
    // Default: all positions set to None (255)
    Serial.println("Initializing positions to None (255)");
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricPositions[i] = 255; // None/Hidden by default
    }
    preferences.putBytes("metricPos", settings.metricPositions, MAX_METRICS);
  }

  // Load progress bar settings
  size_t barPosSize = preferences.getBytesLength("metricBarPos");
  if (barPosSize == MAX_METRICS) {
    preferences.getBytes("metricBarPos", settings.metricBarPositions,
                         MAX_METRICS);
    preferences.getBytes("barMin", settings.metricBarMin,
                         MAX_METRICS * sizeof(int));
    preferences.getBytes("barMax", settings.metricBarMax,
                         MAX_METRICS * sizeof(int));
    preferences.getBytes("barWidths", settings.metricBarWidths,
                         MAX_METRICS * sizeof(int));
    preferences.getBytes("barOffsets", settings.metricBarOffsets,
                         MAX_METRICS * sizeof(int));
    Serial.println("Loaded progress bar settings from NVS");
  } else {
    // Default: no progress bars
    for (int i = 0; i < MAX_METRICS; i++) {
      settings.metricBarPositions[i] = 255; // None
      settings.metricBarMin[i] = 0;
      settings.metricBarMax[i] = 100;
      settings.metricBarWidths[i] = 60; // Default width
      settings.metricBarOffsets[i] = 0; // Default: no offset
    }
    // CRITICAL FIX: Save default bar settings to NVS so they persist across reboots
    preferences.putBytes("metricBarPos", settings.metricBarPositions, MAX_METRICS);
    preferences.putBytes("barMin", settings.metricBarMin, MAX_METRICS * sizeof(int));
    preferences.putBytes("barMax", settings.metricBarMax, MAX_METRICS * sizeof(int));
    preferences.putBytes("barWidths", settings.metricBarWidths, MAX_METRICS * sizeof(int));
    preferences.putBytes("barOffsets", settings.metricBarOffsets, MAX_METRICS * sizeof(int));
    Serial.println("Initialized and saved default progress bar settings to NVS");
  }

  // Load custom metric labels
  for (int i = 0; i < MAX_METRICS; i++) {
    String key = "label" + String(i);
    String label = preferences.getString(key.c_str(), "");
    if (label.length() > 0) {
      strncpy(settings.metricLabels[i], label.c_str(), METRIC_NAME_LEN - 1);
      settings.metricLabels[i][METRIC_NAME_LEN - 1] = '\0';
    } else {
      settings.metricLabels[i][0] = '\0'; // Empty = use Python name
    }
  }

  // Load metric names (for validation)
  for (int i = 0; i < MAX_METRICS; i++) {
    String key = "name" + String(i);
    String name = preferences.getString(key.c_str(), "");
    if (name.length() > 0) {
      strncpy(settings.metricNames[i], name.c_str(), METRIC_NAME_LEN - 1);
      settings.metricNames[i][METRIC_NAME_LEN - 1] = '\0';
    } else {
      settings.metricNames[i][0] = '\0'; // Empty = no stored name
    }
  }

  // Sprite colors: fill from defaults, then overlay any stored slots. Length-based
  // + append-only -> a firmware with MORE slots keeps the user's existing colors
  // and gets defaults for the new ones (no garbage from a shorter stored blob).
  for (int i = 0; i < COL_COUNT; i++) {
    settings.spriteColors[i] = SPRITE_COLOR_DEFAULTS[i];
  }
  // getBytes copies min(stored, requested) bytes, leaving extra slots at default.
  size_t storedColorBytes = preferences.getBytesLength("spriteCols");
  preferences.getBytes("spriteCols", settings.spriteColors,
                       COL_COUNT * sizeof(uint16_t));
  // Upgrade path: blobs saved before per-style digit colors existed end before the
  // COL_DIGITS_S* slots. Seed each style's digit color from the old single global
  // COL_DIGITS so existing devices keep whatever digit color the user had.
  if (storedColorBytes <= COL_DIGITS_S0 * sizeof(uint16_t)) {
    for (int s = COL_DIGITS_S0; s <= COL_DIGITS_S13; s++) {
      settings.spriteColors[s] = settings.spriteColors[COL_DIGITS];
    }
  }

  preferences.end();

  if (brightnessSettingsSanitized) {
    saveSettings();
    Serial.println("Brightness settings sanitized for this hardware build");
  }

  Serial.println("Settings loaded (v2.0 - Compact Grid Layout)");
}

void saveSettings() {
  sanitizeBrightnessSettings();
  preferences.begin("pcmonitor", false); // Read-write
  preferences.putInt("clockStyle", settings.clockStyle);
  preferences.putInt("gmtOffset", settings.gmtOffset); // Keep for backward compatibility
  preferences.putBool("dst", settings.daylightSaving);  // Keep for backward compatibility
  preferences.putString("tz", settings.timezoneString); // New timezone string
  preferences.putUChar("tzIdx", settings.timezoneIndex); // Timezone region index
  preferences.putBool("use24Hour", settings.use24Hour);
  preferences.putInt("dateFormat", settings.dateFormat);
  preferences.putInt("clockPos", settings.clockPosition);
  preferences.putInt("clockOffset", settings.clockOffset);
  preferences.putBool("showClock", settings.showClock);
  preferences.putInt("rowMode", settings.displayRowMode);
  preferences.putBool("rpmKFormat", settings.useRpmKFormat);
  preferences.putBool("netMBFormat", settings.useNetworkMBFormat);
  preferences.putUChar("colonBlink", settings.colonBlinkMode);
  preferences.putUChar("colonRate", settings.colonBlinkRate);
  preferences.putUChar("refreshMode", settings.refreshRateMode);
  preferences.putUChar("refreshHz", settings.refreshRateHz);
  preferences.putBool("boostAnim", settings.boostAnimationRefresh);
  preferences.putUChar("brightness", settings.displayBrightness);
  preferences.putBool("schedDim", settings.enableScheduledDimming);
  preferences.putUChar("dimStart", settings.dimStartHour);
  preferences.putUChar("dimEnd", settings.dimEndHour);
  preferences.putUChar("dimBright", settings.dimBrightness);
  preferences.putUChar("marioBnceH", settings.marioBounceHeight);
  preferences.putUChar("marioBnceS", settings.marioBounceSpeed);
  preferences.putBool("marioSmooth", settings.marioSmoothAnimation);
  preferences.putUChar("marioWalkSpd", settings.marioWalkSpeed);
  preferences.putBool("marioEnctr", settings.marioIdleEncounters);
  preferences.putUChar("marioEncFrq", settings.marioEncounterFreq);
  preferences.putUChar("marioEncSpd", settings.marioEncounterSpeed);
  preferences.putUChar("pongBallSpd", settings.pongBallSpeed);
  preferences.putUChar("pongBncStr", settings.pongBounceStrength);
  preferences.putUChar("pongBncDmp", settings.pongBounceDamping);
  preferences.putUChar("pongPadWid", settings.pongPaddleWidth);
  preferences.putBool("pongHorizBnc", settings.pongHorizontalBounce);
  preferences.putBool("pongShatter", settings.pongDigitShatter);
  preferences.putUChar("pacmanSpeed", settings.pacmanSpeed);
  preferences.putUChar("pacmanEatSpeed", settings.pacmanEatingSpeed);
  preferences.putUChar("pacmanMouthSpd", settings.pacmanMouthSpeed);
  preferences.putUChar("pacmanPellCount", settings.pacmanPelletCount);
  preferences.putBool("pacmanPellRand", settings.pacmanPelletRandomSpacing);
  preferences.putBool("pacmanBounce", settings.pacmanBounceEnabled);
  preferences.putUChar("spaceChar", settings.spaceCharacterType);
  preferences.putUChar("spacePatrol", settings.spacePatrolSpeed);
  preferences.putUChar("spaceAttack", settings.spaceAttackSpeed);
  preferences.putUChar("spaceLaser", settings.spaceLaserSpeed);
  preferences.putUChar("spaceExpGrv", settings.spaceExplosionGravity);
  preferences.putUChar("snakeSpeed", settings.snakeSpeed);
  preferences.putUChar("snakeLen", settings.snakeLength);
  preferences.putBool("snakeBorder", settings.snakeWallBorder);
  preferences.putBool("snakeDate", settings.snakeShowDate);
  preferences.putUChar("tetFallSpd", settings.tetrisFallSpeed);
  preferences.putUChar("tetBlockSty", settings.tetrisBlockStyle);
  preferences.putBool("tetIdleTmbl", settings.tetrisIdleTumble);
  preferences.putUChar("tetAnimSty", settings.tetrisAnimStyle);
  preferences.putBool("tetShowDate", settings.tetrisShowDate);
  preferences.putUChar("tetDatePos", settings.tetrisDatePosition);
  preferences.putUChar("tetDotSpd", settings.tetrisDotSpeed);
  preferences.putUChar("tetDotOrd", settings.tetrisDotOrder);
  preferences.putBool("tetBounce", settings.tetrisDigitBounce);
  preferences.putBool("tetSmooth", settings.tetrisSmoothGame);
  preferences.putUChar("astShipSpd", settings.asteroidsShipSpeed);
  preferences.putUChar("astRocks", settings.asteroidsRockCount);
  preferences.putUChar("astRockSpd", settings.asteroidsRockSpeed);
  preferences.putBool("astDate", settings.asteroidsShowDate);
  preferences.putBool("astTransp", settings.asteroidsTransparent);
  preferences.putUChar("dinoSpeed", settings.dinoSpeed);
  preferences.putUChar("dinoCactus", settings.dinoCactusFreq);
  preferences.putBool("dinoClouds", settings.dinoShowClouds);
  preferences.putBool("dinoDate", settings.dinoShowDate);
  preferences.putUChar("mxSpeed", settings.matrixRainSpeed);
  preferences.putUChar("mxDensity", settings.matrixRainDensity);
  preferences.putBool("mxDate", settings.matrixShowDate);
  preferences.putBool("mxTransp", settings.matrixTransparent);
  preferences.putUChar("mcSpeed", settings.mcMissileSpeed);
  preferences.putUChar("mcFreq", settings.mcMissileFreq);
  preferences.putBool("mcDate", settings.mcShowDate);

  // Save network configuration
  preferences.putString("deviceName", settings.deviceName);
  preferences.putBool("showIPBoot", settings.showIPAtBoot);
  preferences.putBool("useStaticIP", settings.useStaticIP);
  preferences.putString("staticIP", settings.staticIP);
  preferences.putString("gateway", settings.gateway);
  preferences.putString("subnet", settings.subnet);
  preferences.putString("dns1", settings.dns1);
  preferences.putString("dns2", settings.dns2);

  // Save user-editable sprite colors (RGB565 array, indexed by ColorSlot)
  preferences.putBytes("spriteCols", settings.spriteColors,
                       COL_COUNT * sizeof(uint16_t));

  // Save metric display order
  preferences.putBytes("metricOrd", settings.metricOrder, MAX_METRICS);

  // Save metric companions
  preferences.putBytes("metricComp", settings.metricCompanions, MAX_METRICS);

  // Save metric positions (255 = hidden, 0-11 = visible at position)
  preferences.putBytes("metricPos", settings.metricPositions, MAX_METRICS);

  // Save progress bar settings
  preferences.putBytes("metricBarPos", settings.metricBarPositions,
                       MAX_METRICS);
  preferences.putBytes("barMin", settings.metricBarMin,
                       MAX_METRICS * sizeof(int));
  preferences.putBytes("barMax", settings.metricBarMax,
                       MAX_METRICS * sizeof(int));
  preferences.putBytes("barWidths", settings.metricBarWidths,
                       MAX_METRICS * sizeof(int));
  preferences.putBytes("barOffsets", settings.metricBarOffsets,
                       MAX_METRICS * sizeof(int));

  // Save custom metric labels
  for (int i = 0; i < MAX_METRICS; i++) {
    String key = "label" + String(i);
    if (settings.metricLabels[i][0] != '\0') {
      preferences.putString(key.c_str(), settings.metricLabels[i]);
    } else {
      preferences.remove(key.c_str()); // Remove if empty
    }
  }

  // Save metric names (for validation)
  for (int i = 0; i < MAX_METRICS; i++) {
    String key = "name" + String(i);
    if (settings.metricNames[i][0] != '\0') {
      preferences.putString(key.c_str(), settings.metricNames[i]);
    } else {
      preferences.remove(key.c_str()); // Remove if empty
    }
  }

  preferences.end();

  Serial.println("Settings saved (v2.0)!");
}
