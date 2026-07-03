/*
 * AnimatedPixelClock - Ambient Screensaver Dispatcher
 *
 * Schedule check, style dispatch and the optional corner clock overlay.
 * The effects themselves live in ambient_*.cpp.
 */

#include "ambient.h"

#include "../clocks/clocks.h"
#include "../config/config.h"
#include "../display/display.h"

extern bool httpForceAmbient;  // defined in main.cpp

bool ambientActive() {
  if (httpForceAmbient) return true;
  if (!settings.ambientEnabled) return false;

  struct tm timeinfo;
  // Zero timeout: one non-blocking check. A 10ms timeout here blocks every
  // loop iteration while NTP is still syncing.
  if (!getTimeWithTimeout(&timeinfo, 0)) return false;

  int h = timeinfo.tm_hour;
  int s = settings.ambientStartHour;
  int e = settings.ambientEndHour;
  if (s == e) return false;  // empty window
  // Wraps midnight the same way scheduled dimming does.
  return (s < e) ? (h >= s && h < e) : (h >= s || h < e);
}

// Small HH:MM in the top-right corner, on a black backing so it stays
// readable over bright effects like the fire.
static void drawAmbientClock() {
  struct tm timeinfo;
  if (!getTimeWithTimeout(&timeinfo, 0)) return;  // non-blocking, see ambientActive()

  int displayHour, displayMin;
  bool isPM;
  formatTimeForDisplay(timeinfo.tm_hour, timeinfo.tm_min, displayHour,
                       displayMin, isPM);
  char timeStr[6];
  sprintf(timeStr, "%02d%c%02d", displayHour, shouldShowColon() ? ':' : ' ',
          displayMin);

  display.fillRect(SCREEN_WIDTH - 34, 0, 34, 10, DISPLAY_BLACK);
  display.setTextSize(1);
  display.setTextColor(DISPLAY_WHITE);
  display.setCursor(SCREEN_WIDTH - 31, 1);
  display.print(timeStr);
}

void displayAmbient() {
  switch (settings.ambientStyle) {
    case 0: ambientFireFrame(); break;
    case 1: ambientPlasmaFrame(); break;
    case 2: ambientLavaFrame(); break;
    case 3: ambientStarsFrame(); break;
    case 4: ambientAquariumFrame(); break;
    default: ambientFireFrame(); break;
  }
  if (settings.ambientShowClock) {
    drawAmbientClock();
  }
}
