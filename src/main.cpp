/*
 * AnimatedPixelClock - Main Entry Point
 *
 * ESP32-S3 with 128x64 HUB75 RGB matrix (2x 64x64 panels chained)
 * Dual-mode: PC monitoring metrics OR animated clock displays
 */

// ========== User Configuration ==========
// Edit src/config/user_config.h to configure WiFi and device options
#include "config/user_config.h"

#include <Adafruit_GFX.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "display/matrix_display.h" // HUB75 RGB matrix (ESP32-S3)
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_task_wdt.h>
#include <time.h>


#include "config/config.h"
#include "utils/utils.h"
#include "timezones.h"

// ========== External Objects ==========
extern WiFiUDP udp;              // Defined in network.cpp
extern WebServer server;         // Defined in web.cpp
extern Preferences preferences;  // Defined in settings.cpp

// ========== Display Object ==========
// HUB75 RGB matrix (128x64). The shim adds the OLED-era clearDisplay()/
// display() frame methods so the animation code runs unchanged.
// DISPLAY_WHITE/DISPLAY_BLACK come from display.h (centralized).
MatrixDisplay display(makeMatrixConfig());

// ========== Global State ==========
Settings settings;
MetricData metricData;
bool displayAvailable = false;
bool ntpSynced = false;
unsigned long lastNtpSyncTime = 0;
unsigned long lastReceived = 0;
unsigned long wifiDisconnectTime = 0;
unsigned long nextDisplayUpdate = 0;
bool wifiConnected = false;  // WiFi connection status for icon display
bool httpForceClock = false;  // HTTP override to force clock mode (via /api/mode/clock)
bool httpForceAmbient = false;  // HTTP override to force the ambient screen (via /api/mode/ambient)
bool httpForceViz = false;  // HTTP override to force the audio visualizer (via /api/mode/viz)

// ========== Forward Declarations ==========
// Redundant forward declarations removed (covered by headers)
void displayStats();
void displayStatsCompactGrid();
void displayMetricCompact(Metric *m);
void drawProgressBar(int x, int y, int width, Metric *m);
int getOptimalRefreshRate();
// ========== Module Includes ==========
#include "ambient/ambient.h"
#include "ambient/anim_store.h"
#include "display/display.h"
#include "clocks/clocks.h"
#include "clocks/clock_globals.h"
#include "metrics/metrics.h"
#include "network/network.h"
#include "notify/notify.h"
#include "viz/visualizer.h"
#include "weather/weather.h"
#include "web/web.h"


// ========== Helper Functions ==========

// Helper function to get time with short timeout
bool getTimeWithTimeout(struct tm *timeinfo, unsigned long timeout_ms) {
  if (!ntpSynced) {
    if (getLocalTime(timeinfo, timeout_ms)) {
      // Verify time is reasonable (year > 2020) before accepting
      if (timeinfo->tm_year > 120) { // tm_year is years since 1900
        ntpSynced = true;
        lastNtpSyncTime = millis();
        Serial.println("NTP successfully synchronized");
        return true;
      }
    }
    return false;
  }
  return getLocalTime(timeinfo, timeout_ms);
}

// Returns optimal refresh rate in Hz based on current display mode
int getOptimalRefreshRate() {
  // Always adaptive. The manual fixed-Hz override (and its web control) was
  // removed - a user-pinned low rate only made animations choppy. The adaptive
  // rates below are what keep motion smooth.

  // A notification banner may scroll over any screen - keep it silky.
  if (notifyActive()) {
    return 60;
  }

  // Audio visualizer: 60 Hz render smooths the ~25 Hz packet stream.
  if (httpForceViz && vizShouldDisplay()) {
    return 60;
  }

  if (!metricData.online || httpForceClock || httpForceAmbient) {
    // Clock mode (offline OR forced via HTTP)

    // Ambient effects (fire, plasma...) redraw the FULL panel every frame.
    // 30 Hz, not 60: the effects look identical, the per-frame pixel load
    // halves, and the buffer-flip rate stops beating against the panel's
    // DMA scan (visible as rolling dim bands on full-screen content).
    if (ambientActive()) {
      return 30;
    }

    // Boost to 60 Hz during active motion for silky-smooth animation (always on;
    // the old opt-out checkbox was removed).
    if (isAnimationActive()) {
      return 60;
    }

    int rate;
    if (settings.clockStyle == 0 || settings.clockStyle == 3 ||
        settings.clockStyle == 4 || settings.clockStyle == 5 ||
        settings.clockStyle == 6 || settings.clockStyle == 7 ||
        settings.clockStyle == 8 || settings.clockStyle == 9 ||
        settings.clockStyle == 10 || settings.clockStyle == 11 ||
        settings.clockStyle == 12 || settings.clockStyle == 13 ||
        settings.clockStyle == 14) {
      // Animated clocks (Mario, Space Invaders, Space Ship, Pong, Pac-Man, Snake, Tetris, Cycle, Asteroids, Dino, Matrix, Missile, Weather)
      rate = 20; // 20 Hz keeps character movement smooth
    } else {
      // Static clocks (Standard, Large)
      rate = 2; // 2 Hz is plenty for clock that updates once/second
    }
    // A seasonal overlay (snow, fireworks...) animating over a static clock
    // needs a floor or it turns into a slideshow.
    if (seasonalOverlayActive() && rate < 30) rate = 30;
    return rate;
  } else {
    // Metrics mode (online)
    return 10; // 10 Hz for PC stats (updates every 500ms from Python)
  }
}

// --- Clock screen cycle state ---
int lastMinuteBlock = -1;
int currentScreen = 0;
bool firstTimeSynced = false;

void cycleClockScreens() {
    struct tm timeinfo;

    // Advance the cycle only when we have valid time. When time is not yet
    // available the per-screen draw functions below render their own
    // "Syncing time..." message, so the display is never left blank.
    if (getTimeWithTimeout(&timeinfo)) {
        // Determine which 5-minute block we are in
        int minuteBlock = timeinfo.tm_min / 5;

        // First valid time -> initialize block WITHOUT advancing screen
        if (!firstTimeSynced) {
            lastMinuteBlock = minuteBlock;
            firstTimeSynced = true;
        }

        // After that, normal cycling. The weather screen (index 12) joins the
        // rotation only when weather is enabled and configured.
        if (minuteBlock != lastMinuteBlock) {
            lastMinuteBlock = minuteBlock;
            int screenCount = weatherConfigured() ? 13 : 12;
            currentScreen = (currentScreen + 1) % screenCount;
            resetClockAnimationState(); // Reset animation state when changing screens
        }
    }

    // Draw the current screen (each draw function handles the no-time case)
    switch (currentScreen) {
        case 0: displayStandardClock(); break;
        case 1: displayClockWithMario(); break;
        case 2: displayClockWithSpaceInvader(); break;
        case 3: displayLargeClock(); break;
        case 4: displayClockWithPong(); break;
        case 5: displayClockWithPacman(); break;
        case 6: displayClockWithSnake(); break;
        case 7: displayClockWithTetris(); break;
        case 8: displayClockWithAsteroids(); break;
        case 9: displayClockWithDino(); break;
        case 10: displayClockWithMatrixRain(); break;
        case 11: displayClockWithMissileCommand(); break;
        case 12: displayClockWithWeather(); break;
    }
}

// ========== setup() ==========
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Load settings from flash
  loadSettings();

  // Mount the animation filesystem (formats the partition on first use)
  animStoreInit();

  // Initialize display
  displayAvailable = initDisplay();

  // Apply saved brightness setting
  if (displayAvailable) {
    applyDisplayBrightness();
  }

  if (!displayAvailable) {
    Serial.println("WARNING: Display not available, continuing without display");
  } else {
    display.clearDisplay();
    display.setTextColor(DISPLAY_WHITE);
    display.setTextSize(1);
    display.setCursor(10, 20);
    display.println("PC Monitor");
    display.setCursor(10, 35);
    display.println("Starting...");
    display.display();
  }

  // Check if hardcoded WiFi credentials are provided
  bool useManualWiFi = (strlen(HARDCODED_WIFI_SSID) > 0);

  if (useManualWiFi) {
    Serial.println("\n*** USING HARDCODED WIFI CREDENTIALS ***");
    if (!connectManualWiFi(HARDCODED_WIFI_SSID, HARDCODED_WIFI_PASSWORD)) {
      Serial.println("Manual WiFi connection failed!");
      Serial.println("Falling back to WiFiManager portal...");
      useManualWiFi = false;
    }
  }

  if (!useManualWiFi) {
    initNetwork();
  }

  // Keep the radio awake: WiFi modem sleep delays inbound ACKs to the beacon
  // interval, which stalls large web-page transfers for seconds. Power draw
  // is irrelevant next to the LED matrix.
  WiFi.setSleep(false);

  // Initialize NTP
  initNTP();

  // Initialize WiFi connection status flag
  wifiConnected = (WiFi.status() == WL_CONNECTED);

  // Configure hardware watchdog timer
  esp_task_wdt_init(15, true);
  esp_task_wdt_add(NULL);

  // Initialize metricData
  metricData.count = 0;
  metricData.online = false;
  metricData.status = 0;  // No status received yet
  Serial.println("Waiting for PC stats data...");

  // Setup web server
  setupWebServer();

  // Background weather fetcher (idles cheaply while weather is disabled)
  startWeatherTask();

  // Show IP address for 5 seconds (configurable via web interface)
  if (displayAvailable && settings.showIPAtBoot) {
    displayConnected();
    delay(5000);
  }
}

// ========== loop() ==========
void loop() {
  // Feed watchdog
  esp_task_wdt_reset();

  // Check and apply scheduled brightness (time-based dimming)
  checkScheduledBrightness();

  // Handle web server requests
  server.handleClient();

  // Handle UDP packets - always process to track PC online status accurately
  handleUDP();

  // Check timeout
  if (millis() - lastReceived > TIMEOUT && metricData.online) {
    metricData.online = false;
    Serial.println("PC stats timeout - switching to clock mode");
  }

  // Retry NTP sync periodically if not synced
  if (!ntpSynced && millis() - lastNtpSyncTime > 30000) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100)) {
      if (timeinfo.tm_year > 120) {
        ntpSynced = true;
        lastNtpSyncTime = millis();
        Serial.println("NTP sync successful (retry)");
      }
    } else {
      applyTimezone();  // SNTP client might be dead - restart it
      Serial.println("NTP retry: restarted SNTP client");
    }
    lastNtpSyncTime = millis();
  }

  // Periodic NTP re-sync even when already synced (safety net). SNTP keeps the
  // system clock valid between refreshes, so refresh the timezone/SNTP client
  // without clearing ntpSynced - dropping it would needlessly re-anchor the
  // clocks (below) every hour and could flash "Syncing time..." for a frame.
  if (ntpSynced && millis() - lastNtpSyncTime > NTP_RESYNC_INTERVAL) {
    applyTimezone();
    lastNtpSyncTime = millis();
    Serial.println("Periodic NTP re-sync triggered");
  }

  // Re-anchor the animated clocks when NTP first becomes valid (or after a
  // reconnect resync). At boot the clocks seed displayed_hour/min with a 00:00
  // fallback; if the first sync lands inside a clock's minute-change window the
  // animation advances from 00:00 instead of jumping to the real time, leaving
  // the clock stuck near 00:00 until a reboot or clock-cycle. Resetting the
  // animation state clears the stale time override, and syncDisplayedTime()
  // forces displayed_hour/min to match reality.
  static bool prevNtpSynced = false;
  if (ntpSynced && !prevNtpSynced) {
    resetClockAnimationState();
    struct tm now_tm;
    if (getLocalTime(&now_tm, 10)) {
      syncDisplayedTime(&now_tm);
    }
  }
  prevNtpSynced = ntpSynced;

  // Display update with adaptive refresh rate
  int targetHz = getOptimalRefreshRate();
  unsigned long frameInterval = 1000 / targetHz;

  if (millis() >= nextDisplayUpdate && displayAvailable && !isDisplayForcedOff()) {
    // Schedule from the previous deadline, not from "now": scheduling from now
    // adds the loop latency to every frame, so frames drift off the fixed grid
    // and the animation tick gates beat against them (visible micro-stutter).
    nextDisplayUpdate += frameInterval;
    if (millis() >= nextDisplayUpdate) {
      // Fell behind (stall or refresh-rate change) - resync to now.
      nextDisplayUpdate = millis() + frameInterval;
    }

    display.clearDisplay();

    // Visualizer wins over everything while forced AND fed; when the packet
    // stream dies for 10s it falls through (and auto-resumes when it's back).
    bool showViz = httpForceViz && vizShouldDisplay();
    bool showStats =
        !showViz && metricData.online && !httpForceClock && !httpForceAmbient;

    if (showViz) {
      displayVisualizer();
    } else
    // Show error status if PC is connected but LHM has issues
    if (showStats && metricData.status != STATUS_OK && metricData.status != 0) {
      displayErrorStatus(metricData.status);
    } else if (showStats) {
      displayStats();
    } else if (ambientActive()) {
      // Scheduled (or forced) ambient screensaver replaces the clock.
      displayAmbient();
    } else {
      switch (settings.clockStyle) {
      case 0:
        displayClockWithMario();
        break;
      case 1:
        displayStandardClock();
        break;
      case 2:
        displayLargeClock();
        break;
      case 3:
      case 4:
        displayClockWithSpaceInvader();
        break;
      case 5:
        displayClockWithPong();
        break;
      case 6:
        displayClockWithPacman();
        break;
      case 7:
        displayClockWithSnake();
        break;
      case 8:
        displayClockWithTetris();
        break;
      case 9:
        cycleClockScreens();
        break;
      case 10:
        displayClockWithAsteroids();
        break;
      case 11:
        displayClockWithDino();
        break;
      case 12:
        displayClockWithMatrixRain();
        break;
      case 13:
        displayClockWithMissileCommand();
        break;
      case 14:
        displayClockWithWeather();
        break;
      default:
        displayStandardClock();
        break;
      }

      // Date-driven holiday overlay (no-op outside its windows). Clock
      // styles only - never over PC stats or ambient scenes.
      drawSeasonalOverlay();
    }

    // Notification banner draws over whatever screen is active.
    if (notifyActive()) {
      drawNotifyOverlay();
    }

    display.display();

    // Right after the flip = maximum headroom before the next render tick;
    // the custom animation reads its next frame from flash here so the I/O
    // never delays a flip (delayed flips beat against the DMA scan).
    ambientCustomPrefetch();
  }

  // WiFi reconnection handling
  handleWiFiReconnection();
}
