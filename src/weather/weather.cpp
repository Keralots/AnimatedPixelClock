/*
 * AnimatedPixelClock - Weather Module (Open-Meteo)
 *
 * The fetch (DNS + TLS handshake + transfer) can block for seconds, so it
 * runs in its own task pinned to core 0 - never in loop(), where it would
 * visibly freeze a 60 Hz animation. Results are copied into `published`
 * under a spinlock; the render loop takes snapshots via getWeather().
 */

#include "weather.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "../config/config.h"

#define WEATHER_FETCH_INTERVAL_MS (10UL * 60UL * 1000UL)
#define WEATHER_RETRY_INTERVAL_MS (60UL * 1000UL)
#define WEATHER_IDLE_POLL_MS 5000UL

static WeatherData published = {};
static portMUX_TYPE weatherMux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t weatherTaskHandle = nullptr;

bool weatherConfigured() {
  // 0,0 (middle of the Atlantic) doubles as the "unset" marker.
  return settings.weatherEnabled &&
         !(settings.weatherLat == 0 && settings.weatherLon == 0);
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
  if (!http.begin(client, url)) return false;

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("Weather fetch failed: HTTP %d\n", code);
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
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

  Serial.printf("Weather: %.1fC code %d (RH %d%%)\n", fresh.tempC,
                fresh.weatherCode, fresh.humidity);
  return true;
}

static void weatherTask(void*) {
  for (;;) {
    if (!weatherConfigured() || WiFi.status() != WL_CONNECTED) {
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(WEATHER_IDLE_POLL_MS));
      continue;
    }
    bool ok = fetchWeather();
    // Sleeps the full interval, but a settings change (new location, toggle)
    // kicks the task awake early via weatherSettingsChanged().
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ok ? WEATHER_FETCH_INTERVAL_MS
                                              : WEATHER_RETRY_INTERVAL_MS));
  }
}

void weatherSettingsChanged() {
  if (weatherTaskHandle) xTaskNotifyGive(weatherTaskHandle);
}

void startWeatherTask() {
  if (weatherTaskHandle) return;
  // Core 0: the Arduino loop (and the HUB75 DMA refresh) live on core 1.
  xTaskCreatePinnedToCore(weatherTask, "weather", 8192, nullptr, 1,
                          &weatherTaskHandle, 0);
}
