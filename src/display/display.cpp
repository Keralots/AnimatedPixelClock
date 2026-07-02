/*
 * AnimatedPixelClock - Display Module
 *
 * Display initialization and brightness control for the HUB75 RGB matrix.
 */

#include "display.h"
#include "../config/config.h"
#include "../config/settings.h"
#include <time.h>

// Track last applied brightness to avoid unnecessary updates
static uint8_t lastAppliedBrightness = 255;
static unsigned long lastBrightnessCheck = 0;
const unsigned long BRIGHTNESS_CHECK_INTERVAL = 60000; // Check every minute

// Runtime override: when true, the panel is held off (e.g. via HTTP /api/display/off).
// Scheduled dimming and brightness re-applies are suppressed so they don't turn it back on.
static bool displayForcedOff = false;

// Applies a raw brightness value. Callers pass either an already-sanitized
// settings value or an intentional 0 (forced off / 0%): sanitizing here would
// turn that 0 into 1, leaving the panel faintly lit instead of off.
static void applyBrightnessLevel(uint8_t brightness) {
  if (!displayAvailable) {
    return;
  }

  display.setBrightness8(brightness);
  lastAppliedBrightness = brightness;
}

static bool resolveScheduledBrightnessTarget(uint8_t &targetBrightness) {
  targetBrightness = sanitizeBrightnessValue(settings.displayBrightness);

  if (!settings.enableScheduledDimming) {
    return true;
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return false;
  }

  const uint8_t currentHour = timeinfo.tm_hour;
  bool isDimPeriod = false;

  if (settings.dimStartHour == settings.dimEndHour) {
    isDimPeriod = false;
  } else if (settings.dimStartHour < settings.dimEndHour) {
    isDimPeriod =
        (currentHour >= settings.dimStartHour && currentHour < settings.dimEndHour);
  } else {
    isDimPeriod =
        (currentHour >= settings.dimStartHour || currentHour < settings.dimEndHour);
  }

  targetBrightness = sanitizeBrightnessValue(
      isDimPeriod ? settings.dimBrightness : settings.displayBrightness);
  return true;
}

// Initialize display - returns true on success
bool initDisplay() {
  if (!display.begin()) {
    return false;
  }
  display.setBrightness8(sanitizeBrightnessValue(settings.displayBrightness));
  display.clearScreen();
  return true;
}

// Apply display brightness from settings
void applyDisplayBrightness() {
  if (displayForcedOff) {
    return;
  }

  applyBrightnessLevel(settings.displayBrightness);
}

void refreshDisplayBrightnessNow() {
  if (displayForcedOff) {
    return;
  }

  uint8_t targetBrightness = settings.displayBrightness;
  if (settings.enableScheduledDimming &&
      !resolveScheduledBrightnessTarget(targetBrightness)) {
    return;
  }

  if (lastAppliedBrightness != targetBrightness) {
    applyBrightnessLevel(targetBrightness);
  }
}

// Check and apply time-based brightness (scheduled dimming)
void checkScheduledBrightness() {
  if (displayForcedOff) {
    return;
  }

  // Only check every minute to avoid unnecessary updates
  unsigned long currentTime = millis();
  if (currentTime - lastBrightnessCheck < BRIGHTNESS_CHECK_INTERVAL) {
    return;
  }
  lastBrightnessCheck = currentTime;

  refreshDisplayBrightnessNow();
}

// ---- Runtime display power / brightness control (HTTP API) ----

bool isDisplayForcedOff() {
  return displayForcedOff;
}

// Force the panel off (off=true) or restore normal/scheduled brightness (off=false).
void setDisplayForcedOff(bool off) {
  displayForcedOff = off;
  if (off) {
    applyBrightnessLevel(0); // panel dark
  } else {
    refreshDisplayBrightnessNow(); // re-applies normal or scheduled brightness
  }
}

// Set display brightness from a 0-100 percentage and apply immediately.
// Updates the in-RAM "normal" brightness so scheduled dimming still layers on top.
// Not persisted to flash (runtime-only, like the on/off override).
void setDisplayBrightnessPercent(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  uint8_t brightness = (uint16_t)percent * 255 / 100;
  settings.displayBrightness = brightness;
  displayForcedOff = (brightness == 0);
  applyBrightnessLevel(brightness);
}
