/*
 * AnimatedPixelClock - Weather Module (Open-Meteo)
 *
 * The fetch (DNS + TLS handshake + transfer) can block for seconds, so it
 * runs in a task pinned to core 0 - never in loop(), where it would visibly
 * freeze a 60 Hz animation. The task lives for one fetch: weatherLoop()
 * starts it when a fetch is due and it deletes itself, so its stack is not
 * held in internal SRAM between fetches. Results are copied into `published`
 * under a spinlock; the render loop takes snapshots via getWeather().
 */

#include "weather.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "../config/config.h"
#include "../clocks/cycle_config.h"
#include "../network/network.h"

#define WEATHER_FETCH_INTERVAL_MS (10UL * 60UL * 1000UL)
#define WEATHER_RETRY_INTERVAL_MS (60UL * 1000UL)
#define WEATHER_IDLE_POLL_MS 5000UL
#define WEATHER_TASK_STACK 8192

static WeatherData published = {};
static portMUX_TYPE weatherMux = portMUX_INITIALIZER_UNLOCKED;

// The next check comes waitMs after waitFromMs: the full interval after a
// fetch, WEATHER_IDLE_POLL_MS while nothing can show the weather. fetchBusy is set by loop() before the task starts and cleared
// by the task last, after it has written waitFromMs and waitMs; loop() reads
// those two only while fetchBusy is clear.
static volatile bool fetchBusy = false;
static volatile bool fetchKick = false;
static volatile unsigned long waitFromMs = 0;
static volatile unsigned long waitMs = 0;  // 0: check now

bool weatherConfigured() {
  // 0,0 (middle of the Atlantic) doubles as the "unset" marker.
  return settings.weatherEnabled &&
         !(settings.weatherLat == 0 && settings.weatherLon == 0);
}

// The TLS handshake is the largest allocation this firmware makes, so it only
// runs when a screen can actually show the result.
static bool weatherOnScreen() {
  if (settings.clockStyle == 14) return true;
  if (settings.clockStyle != 9) return false;
  CycleEntry entries[CYCLE_COUNT];
  if (!parseCycleConfig(settings.cycleConfig, entries)) return true;
  for (unsigned i = 0; i < CYCLE_COUNT; ++i)
    if (entries[i].style == 14 && entries[i].seconds) return true;
  return false;
}

WeatherData getWeather() {
  portENTER_CRITICAL(&weatherMux);
  WeatherData copy = published;
  portEXIT_CRITICAL(&weatherMux);
  return copy;
}

WeatherIconKind weatherIconFromCode(int code) {
  if (code == 0) return WICON_SUN;
  if (code <= 2) return WICON_PARTCLOUD;
  if (code == 3) return WICON_CLOUD;
  if (code == 45 || code == 48) return WICON_FOG;
  if (code >= 71 && code <= 77) return WICON_SNOW;
  if (code == 85 || code == 86) return WICON_SNOW;
  if (code >= 95) return WICON_STORM;
  // Everything else in the 5x/6x/8x ranges is some form of rain/drizzle.
  return WICON_RAIN;
}

// Copy "HH:MM" out of an ISO-8601 timestamp ("2026-07-03T04:30").
static void extractClockTime(const char* iso, char out[6]) {
  out[0] = '\0';
  if (!iso) return;
  const char* t = strchr(iso, 'T');
  if (t && strlen(t) >= 6) {
    memcpy(out, t + 1, 5);
    out[5] = '\0';
  }
}

static bool fetchWeather() {
  char url[320];
  bool hasKey = settings.weatherApiKey[0] != '\0';
  // The commercial tier uses the same API on a customer- host with an apikey.
  snprintf(url, sizeof(url),
           "https://%s/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
           "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset"
           "&timezone=auto&forecast_days=1%s%s",
           hasKey ? "customer-api.open-meteo.com" : "api.open-meteo.com",
           settings.weatherLat, settings.weatherLon,
           hasKey ? "&apikey=" : "", hasKey ? settings.weatherApiKey : "");

  WiFiClientSecure client;
  client.setInsecure();  // public, non-sensitive data; saves a cert bundle
  HTTPClient http;
  http.setTimeout(10000);
  http.useHTTP10(true);
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("Weather fetch failed: HTTP %d\n", code);
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Serial.printf("Weather JSON error: %s\n", err.c_str());
    return false;
  }

  JsonObject current = doc["current"];
  JsonObject daily = doc["daily"];
  if (current.isNull() || daily.isNull()) return false;

  WeatherData fresh = {};
  fresh.valid = true;
  fresh.tempC = current["temperature_2m"] | 0.0f;
  fresh.humidity = current["relative_humidity_2m"] | 0;
  fresh.weatherCode = current["weather_code"] | 3;
  fresh.windKmh = current["wind_speed_10m"] | 0.0f;
  fresh.tempMaxC = daily["temperature_2m_max"][0] | 0.0f;
  fresh.tempMinC = daily["temperature_2m_min"][0] | 0.0f;
  extractClockTime(daily["sunrise"][0], fresh.sunrise);
  extractClockTime(daily["sunset"][0], fresh.sunset);
  fresh.fetchedAt = millis();

  portENTER_CRITICAL(&weatherMux);
  published = fresh;
  portEXIT_CRITICAL(&weatherMux);
  netMarkOutboundOk();

  Serial.printf("Weather: %.1fC code %d (RH %d%%)\n", fresh.tempC,
                fresh.weatherCode, fresh.humidity);
  return true;
}

static void weatherFetchTask(void*) {
  bool ok = fetchWeather();
  waitFromMs = millis();
  waitMs = ok ? WEATHER_FETCH_INTERVAL_MS : WEATHER_RETRY_INTERVAL_MS;
  fetchBusy = false;
  vTaskDelete(nullptr);
}

void weatherLoop() {
  if (fetchBusy) return;
  const unsigned long now = millis();
  // A settings change (new location, toggle) ends the wait early.
  if (fetchKick) {
    fetchKick = false;
    waitMs = 0;
  }
  if (now - waitFromMs < waitMs) return;
  if (!weatherConfigured() || !weatherOnScreen() ||
      WiFi.status() != WL_CONNECTED) {
    waitFromMs = now;
    waitMs = WEATHER_IDLE_POLL_MS;
    return;
  }
  fetchBusy = true;
  // Core 0: the Arduino loop (and the HUB75 DMA refresh) live on core 1.
  if (xTaskCreatePinnedToCore(weatherFetchTask, "weather", WEATHER_TASK_STACK,
                              nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println("Weather fetch task not started, retrying in a minute");
    fetchBusy = false;
    waitFromMs = now;
    waitMs = WEATHER_RETRY_INTERVAL_MS;
  }
}

void weatherSettingsChanged() {
  fetchKick = true;
}
