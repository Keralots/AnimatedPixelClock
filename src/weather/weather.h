/*
 * AnimatedPixelClock - Weather Module (Open-Meteo)
 *
 * Background FreeRTOS task fetches current conditions + today's range every
 * 10 minutes and publishes them into a small struct the render loop reads.
 */

#ifndef WEATHER_H
#define WEATHER_H

#include <Arduino.h>

// Condition groups the icon renderer understands (mapped from WMO codes).
enum WeatherIconKind : uint8_t {
  WICON_SUN = 0,
  WICON_PARTCLOUD,
  WICON_CLOUD,
  WICON_FOG,
  WICON_RAIN,
  WICON_SNOW,
  WICON_STORM,
};

struct WeatherData {
  bool valid;            // true once at least one fetch succeeded
  float tempC;           // current temperature (Celsius)
  float tempMinC;        // today's minimum
  float tempMaxC;        // today's maximum
  int humidity;          // relative humidity %
  float windKmh;         // wind speed km/h
  int weatherCode;       // raw WMO code
  char sunrise[6];       // "HH:MM"
  char sunset[6];        // "HH:MM"
  unsigned long fetchedAt;  // millis() of the last successful fetch
};

// Call from loop(). When a fetch is due it starts a task for that one fetch,
// and the task deletes itself when done. Returns at once when weather is
// disabled or unconfigured, so it is safe to call unconditionally.
void weatherLoop();

// Thread-safe snapshot of the latest data.
WeatherData getWeather();

// True when the user has enabled weather and set a location.
bool weatherConfigured();

// Fetch on the next weatherLoop() (call after weather settings change).
void weatherSettingsChanged();

// Map a WMO weather code to an icon kind.
WeatherIconKind weatherIconFromCode(int wmoCode);

#endif // WEATHER_H
