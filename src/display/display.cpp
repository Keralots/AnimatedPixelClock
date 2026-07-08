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
// False until the first time-resolved brightness has been applied. Keeps the
// scheduled check polling every loop pass right after boot (while NTP may still
// be syncing) so dimming/off engages the instant a valid time is available,
// instead of leaving the panel at the un-dimmed boot brightness for a minute.
static bool scheduledBrightnessApplied = false;

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

// True when minute-of-day `now` falls inside [start, end). Equal bounds = empty
// window. Handles windows that wrap past midnight (start > end). All args are
// minutes since midnight (0-1439).
static bool minuteInWindow(uint16_t now, uint16_t start, uint16_t end) {
  if (start == end) {
    return false;
  }
  if (start < end) {
    return (now >= start && now < end);
  }
  return (now >= start || now < end);
}

static inline uint16_t minuteOfDay(uint8_t hour, uint8_t minute) {
  return (uint16_t)hour * 60 + minute;
}

static bool resolveScheduledBrightnessTarget(uint8_t &targetBrightness) {
  targetBrightness = sanitizeBrightnessValue(settings.displayBrightness);

  if (!settings.enableScheduledDimming && !settings.enableScheduledOff) {
    return true;
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return false;
  }

  const uint16_t now = minuteOfDay(timeinfo.tm_hour, timeinfo.tm_min);

  // Scheduled off wins over dimming: brightness 0 (NOT sanitized - the panel
  // must go fully dark to spare the LEDs, sanitizing would floor it to 1).
  if (settings.enableScheduledOff &&
      minuteInWindow(now, minuteOfDay(settings.offStartHour, settings.offStartMinute),
                     minuteOfDay(settings.offEndHour, settings.offEndMinute))) {
    targetBrightness = 0;
    return true;
  }

  if (settings.enableScheduledDimming &&
      minuteInWindow(now, minuteOfDay(settings.dimStartHour, settings.dimStartMinute),
                     minuteOfDay(settings.dimEndHour, settings.dimEndMinute))) {
    targetBrightness = sanitizeBrightnessValue(settings.dimBrightness);
  }
  return true;
}

// True while the scheduled-off window is currently active (live time check,
// independent of the once-a-minute applied value). Reported to Home Assistant
// via /api/status so it can see the panel is dark on schedule.
bool isDisplayScheduledOff() {
  if (!settings.enableScheduledOff) {
    return false;
  }
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return false;
  }
  return minuteInWindow(minuteOfDay(timeinfo.tm_hour, timeinfo.tm_min),
                        minuteOfDay(settings.offStartHour, settings.offStartMinute),
                        minuteOfDay(settings.offEndHour, settings.offEndMinute));
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

  uint8_t targetBrightness;
  if (!resolveScheduledBrightnessTarget(targetBrightness)) {
    return; // no valid time yet - leave the current brightness untouched
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

  unsigned long currentTime = millis();

  // Throttle to once a minute only after a scheduled value has actually been
  // applied. Before that (right after boot) poll every pass so the correct
  // dim/off level engages immediately once the clock has a valid time.
  if (scheduledBrightnessApplied &&
      currentTime - lastBrightnessCheck < BRIGHTNESS_CHECK_INTERVAL) {
    return;
  }

  uint8_t target;
  if (!resolveScheduledBrightnessTarget(target)) {
    return; // no valid time yet - retry on the next loop pass
  }

  lastBrightnessCheck = currentTime;
  scheduledBrightnessApplied = true;
  if (lastAppliedBrightness != target) {
    applyBrightnessLevel(target);
  }
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
