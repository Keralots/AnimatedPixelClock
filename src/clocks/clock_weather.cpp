/*
 * AnimatedPixelClock - Weather Clock (style 14)
 *
 * Time on top, animated condition icon + big temperature in the middle,
 * details row (min/max + humidity alternating with sunrise/sunset) at the
 * bottom. Icon animation phases run off millis() so there is no state to
 * reset between style switches.
 */

#include "clocks.h"
#include "clock_globals.h"
#include "../display/display.h"
#include "../weather/weather.h"

// Layout
#define WTIME_Y 2
#define WICON_X 10
#define WICON_Y 24
#define WICON_SIZE 24
#define WDETAIL_Y 55
#define WDETAIL_SWAP_MS 5000

// Above this the rain and snow slant, clouds drift faster and streaks blow past
#define WWIND_KMH 30.0f

struct IconScene {
  int code;          // raw WMO code, for precipitation intensity
  bool night;
  bool windy;
  float t;           // seconds
  unsigned long ms;
};

static uint16_t scaleColor(uint16_t c, int num) {
  int r = ((c >> 11) & 31) * num / 255;
  int g = ((c >> 5) & 63) * num / 255;
  int b = (c & 31) * num / 255;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// Halfway to white, for the cloud lit by a lightning flash
static uint16_t lightenColor(uint16_t c) {
  int r = (((c >> 11) & 31) + 31) / 2;
  int g = (((c >> 5) & 63) + 63) / 2;
  int b = ((c & 31) + 31) / 2;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU;
  x ^= x >> 15; x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

static int parseHHMM(const char* s) {
  if (!s || strlen(s) < 5 || s[2] != ':') return -1;
  return ((s[0] - '0') * 10 + (s[1] - '0')) * 60 + (s[3] - '0') * 10 + (s[4] - '0');
}

static void drawCloudShape(int cx, int cy, uint16_t color) {
  // Puffy cloud built from three discs on a base bar, ~20px wide.
  display.fillCircle(cx - 6, cy + 2, 4, color);
  display.fillCircle(cx, cy - 1, 5, color);
  display.fillCircle(cx + 6, cy + 2, 4, color);
  display.fillRect(cx - 6, cy + 2, 13, 4, color);
}

// Disc that breathes by one pixel, with rays turning slowly around it.
// Alternate rays are shorter so the rotation reads.
static void drawSun(int cx, int cy, int r, float t, uint16_t color) {
  int disc = r + ((sinf(t * 1.6f) > 0.6f) ? 1 : 0);
  display.fillCircle(cx, cy, disc, color);
  float spin = t * 0.5f;
  for (int i = 0; i < 8; i++) {
    float a = spin + i * (PI / 4.0f);
    int len = r + ((i % 2) ? 4 : 5);
    display.drawLine(cx + (int)roundf(cosf(a) * (r + 2)), cy + (int)roundf(sinf(a) * (r + 2)),
                     cx + (int)roundf(cosf(a) * len), cy + (int)roundf(sinf(a) * len), color);
  }
}

static void drawMoon(int cx, int cy, int r, uint16_t color) {
  display.fillCircle(cx, cy, r, color);
  display.fillCircle(cx + r / 2 + 1, cy - r / 2, r - 1, DISPLAY_BLACK);
}

// Fixed star field around the moon, each star fading in and out on its own beat
static void drawStars(int x, int y, float t, int count) {
  static const int8_t STARS[][2] = {{2, 3}, {20, 1}, {24, 12}, {1, 18}, {22, 22}, {12, 0}};
  for (int i = 0; i < count && i < 6; i++) {
    float s = sinf(t * (0.9f + i * 0.23f) + i * 1.7f);
    if (s < -0.2f) continue;
    uint16_t c = scaleColor(DISPLAY_WHITE, s > 0.6f ? 255 : 110);
    int sx = x + STARS[i][0], sy = y + STARS[i][1];
    display.drawPixel(sx, sy, c);
    if (s > 0.9f) {
      uint16_t d = scaleColor(DISPLAY_WHITE, 90);
      display.drawPixel(sx - 1, sy, d);
      display.drawPixel(sx + 1, sy, d);
      display.drawPixel(sx, sy - 1, d);
      display.drawPixel(sx, sy + 1, d);
    }
  }
}

// Precipitation strength from the WMO code: 0 light, 1 moderate, 2 heavy
static int precipLevel(int code) {
  switch (code) {
    case 51: case 56: case 61: case 66: case 71: case 77: case 80: case 85:
      return 0;
    case 55: case 57: case 65: case 67: case 75: case 82: case 86: case 99:
      return 2;
    default:
      return 1;
  }
}

static bool isDrizzle(int code) { return code >= 51 && code <= 57; }

static void drawRain(int cx, int top, int bottom, const IconScene& s, int level, uint16_t color) {
  static const int COUNT[] = {3, 5, 7};
  static const int STEP_MS[] = {60, 45, 32};
  int n = COUNT[level];
  int len = isDrizzle(s.code) ? 1 : (level == 2 ? 3 : 2);
  int fallH = bottom - top;
  uint16_t splash = scaleColor(color, 140);
  for (int i = 0; i < n; i++) {
    int x0 = cx - 8 + (n > 1 ? i * 16 / (n - 1) : 8);
    int fy = (int)((s.ms / STEP_MS[level] + (hash32(i + 7) % fallH)) % fallH);
    int slant = s.windy ? fy / 3 : 0;
    int x = x0 - slant;
    if (s.windy) display.drawLine(x, top + fy, x + len / 2 + 1, top + fy - len + 1, color);
    else display.drawFastVLine(x, top + fy, len, color);
    if (!isDrizzle(s.code) && fy >= fallH - 2) {
      display.drawPixel(x - 1, bottom + 1, splash);
      display.drawPixel(x + 1, bottom + 1, splash);
    }
  }
}

static void drawSnow(int cx, int top, int bottom, const IconScene& s, int level, uint16_t color) {
  static const int COUNT[] = {4, 6, 8};
  int n = COUNT[level];
  int fallH = bottom - top;
  for (int i = 0; i < n; i++) {
    int x0 = cx - 9 + (n > 1 ? i * 18 / (n - 1) : 9);
    int fy = (int)((s.ms / 110 + (hash32(i + 3) % fallH)) % fallH);
    int x = x0 + (int)roundf(1.5f * sinf(s.t * 2.0f + i * 1.3f)) - (s.windy ? fy / 2 : 0);
    int y = top + fy;
    if (i % 3 == 0) {
      display.drawPixel(x, y, color);
      display.drawPixel(x - 1, y, color);
      display.drawPixel(x + 1, y, color);
      display.drawPixel(x, y - 1, color);
      display.drawPixel(x, y + 1, color);
    } else {
      display.drawPixel(x, y, color);
    }
  }
}

// Short gust lines blowing right to left across the icon area
static void drawWindStreaks(int x, int y, const IconScene& s) {
  uint16_t c = scaleColor(DISPLAY_WHITE, 120);
  static const int ROWS[] = {2, 13, 22};
  for (int i = 0; i < 3; i++) {
    int span = 70;
    int pos = (int)((s.ms / 18 + i * 23) % span);
    int sx = x + 44 - pos;
    int len = 4 + i % 2 * 2;
    for (int k = 0; k < len; k++) {
      int px = sx + k;
      if (px >= 0 && px < x + 46) display.drawPixel(px, y + ROWS[i], c);
    }
  }
}

// Lightning comes in random 500ms slots, about one in eight, as a double flash
static bool lightningOn(unsigned long ms, int* boltShift) {
  uint32_t slot = ms / 500;
  uint32_t h = hash32(slot * 2654435761U);
  if (h % 8 != 0) return false;
  unsigned long in = ms % 500;
  if (boltShift) *boltShift = (int)(h / 8 % 3) * 4 - 4;
  return in < 80 || (in >= 150 && in < 260);
}

static void drawWeatherIcon(int x, int y, WeatherIconKind kind, const IconScene& s) {
  uint16_t body = SPRITE_COLOR(COL_WEATHER_ICON);
  uint16_t accent = SPRITE_COLOR(COL_WEATHER_ACCENT);
  int cx = x + WICON_SIZE / 2;
  int cy = y + WICON_SIZE / 2;
  float drift = s.windy ? 1.1f : 0.5f;

  switch (kind) {
    case WICON_SUN: {
      if (s.night) {
        drawStars(x - 2, y - 2, s.t, 6);
        drawMoon(cx, cy, 7, body);
      } else {
        drawSun(cx, cy, 6, s.t, body);
      }
      break;
    }
    case WICON_PARTCLOUD: {
      if (s.night) {
        drawStars(x - 2, y - 2, s.t, 3);
        drawMoon(cx - 4, cy - 4, 5, body);
      } else {
        drawSun(cx - 4, cy - 4, 4, s.t, body);
      }
      int off = (int)roundf(3.0f * sinf(s.t * drift));
      drawCloudShape(cx + 3 + off, cy + 4, accent);
      break;
    }
    case WICON_CLOUD: {
      int back = (int)roundf(2.0f * sinf(s.t * drift * 0.7f));
      int front = (int)roundf(3.0f * sinf(s.t * drift + 1.0f));
      drawCloudShape(cx - 4 + back, cy - 5, scaleColor(body, 150));
      drawCloudShape(cx + 3 + front, cy + 2, body);
      break;
    }
    case WICON_FOG: {
      // Four haze lines swaying in alternating directions.
      for (int i = 0; i < 4; i++) {
        int yy = y + 5 + i * 5;
        int shift = (int)roundf(3.0f * sinf(s.t * drift * 1.6f + i * 0.9f));
        if (i % 2) shift = -shift;
        display.drawFastHLine(x + 3 + shift, yy, 16, i % 2 ? accent : body);
      }
      break;
    }
    case WICON_RAIN: {
      drawRain(cx, cy + 1, y + WICON_SIZE + 2, s, precipLevel(s.code), accent);
      drawCloudShape(cx + (int)roundf(sinf(s.t * drift)), cy - 6, body);
      break;
    }
    case WICON_SNOW: {
      drawSnow(cx, cy + 1, y + WICON_SIZE + 2, s, precipLevel(s.code), accent);
      drawCloudShape(cx + (int)roundf(sinf(s.t * drift)), cy - 6, body);
      break;
    }
    case WICON_STORM: {
      int shift = 0;
      bool flash = lightningOn(s.ms, &shift);
      drawRain(cx, cy + 1, y + WICON_SIZE + 2, s, 1, accent);
      drawCloudShape(cx, cy - 6, flash ? lightenColor(body) : body);
      if (flash) {
        int bx = cx + shift;
        display.drawLine(bx + 1, cy - 1, bx - 2, cy + 5, DISPLAY_WHITE);
        display.drawLine(bx - 2, cy + 5, bx + 2, cy + 5, DISPLAY_WHITE);
        display.drawLine(bx + 2, cy + 5, bx - 1, cy + 12, DISPLAY_WHITE);
      }
      break;
    }
  }

  if (s.windy) drawWindStreaks(x - 8, y - 4, s);
}

// Big temperature: size-2 digits plus a small degree circle and unit letter.
static void drawTemperature(int x, int y, float tempC) {
  float t = settings.weatherUseFahrenheit ? tempC * 9.0f / 5.0f + 32.0f : tempC;
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", (int)roundf(t));

  display.setTextSize(3);
  display.setTextColor(SPRITE_COLOR(COL_WEATHER_TEMP));
  display.setCursor(x, y);
  display.print(buf);

  int endX = x + strlen(buf) * 18;
  display.drawCircle(endX + 2, y + 1, 2, SPRITE_COLOR(COL_WEATHER_TEMP));
  display.setTextSize(1);
  display.setCursor(endX + 7, y);
  display.print(settings.weatherUseFahrenheit ? "F" : "C");
  display.setTextColor(DISPLAY_WHITE);
}

void displayClockWithWeather() {
  struct tm timeinfo;
  bool haveTime = getTimeWithTimeout(&timeinfo);

  // --- Time row (size 2, centered) ---
  if (haveTime) {
    int displayHour, displayMin;
    bool isPM;
    formatTimeForDisplay(timeinfo.tm_hour, timeinfo.tm_min, displayHour,
                         displayMin, isPM);
    char timeStr[9];
    char separator = shouldShowColon() ? ':' : ' ';
    sprintf(timeStr, "%02d%c%02d", displayHour, separator, displayMin);

    display.setTextSize(2);
    display.setCursor((SCREEN_WIDTH - 5 * 12) / 2, WTIME_Y);
    display.setTextColor(digitColor());
    display.print(timeStr);
    display.setTextColor(DISPLAY_WHITE);
    drawMeridiemIndicator(112, WTIME_Y + 4, isPM);
  } else {
    display.setTextSize(1);
    display.setCursor(20, WTIME_Y + 4);
    display.print(!ntpSynced ? "Syncing time..." : "Time Error");
  }

  // --- Weather block ---
  WeatherData wx = getWeather();
  display.setTextSize(1);

  if (!settings.weatherEnabled || !weatherConfigured()) {
    display.setCursor(22, 34);
    display.print("Weather not set up");
    display.setCursor(13, 46);
    display.print("Enable it in the web UI");
    return;
  }
  if (!wx.valid) {
    display.setCursor(28, 38);
    display.print("Fetching weather...");
    return;
  }

  IconScene scene;
  scene.code = wx.weatherCode;
  scene.ms = millis();
  scene.t = scene.ms / 1000.0f;
  scene.windy = wx.windKmh >= WWIND_KMH;
  scene.night = false;
  int rise = parseHHMM(wx.sunrise), set = parseHHMM(wx.sunset);
  if (haveTime && rise >= 0 && set > rise) {
    int nowMin = timeinfo.tm_hour * 60 + timeinfo.tm_min;
    scene.night = nowMin < rise || nowMin >= set;
  }
  drawWeatherIcon(WICON_X, WICON_Y, weatherIconFromCode(wx.weatherCode), scene);
  drawTemperature(52, WICON_Y + 3, wx.tempC);

  // --- Details row: min/max + humidity alternating with sun times ---
  char line[30];
  if ((millis() / WDETAIL_SWAP_MS) % 2 == 0) {
    float lo = wx.tempMinC, hi = wx.tempMaxC;
    if (settings.weatherUseFahrenheit) {
      lo = lo * 9.0f / 5.0f + 32.0f;
      hi = hi * 9.0f / 5.0f + 32.0f;
    }
    snprintf(line, sizeof(line), "%d\x18 %d\x19  %d%% RH", (int)roundf(hi),
             (int)roundf(lo), wx.humidity);
  } else {
    snprintf(line, sizeof(line), "\x18%s  \x19%s", wx.sunrise, wx.sunset);
  }
  int w = strlen(line) * 6;
  display.setTextColor(SPRITE_COLOR(COL_WEATHER_DETAIL));
  display.setCursor((SCREEN_WIDTH - w) / 2, WDETAIL_Y);
  display.print(line);
  display.setTextColor(DISPLAY_WHITE);

  if (!wifiConnected) {
    drawNoWiFiIcon(0, 0);
  }
}
