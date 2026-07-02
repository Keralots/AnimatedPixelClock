#include "improv_setup.h"

#if IMPROV_SETUP_ENABLED

#include <Arduino.h>
#include <WiFi.h>
#include <ImprovWiFiLibrary.h>

#include "../config/config.h"

static ImprovWiFi improvSerial(&Serial);
static bool improvInited = false;
static bool improvSucceeded = false;
static uint32_t improvDeadline = 0;

static ImprovTypes::ChipFamily detectChipFamily() {
#if CONFIG_IDF_TARGET_ESP32S3
  return ImprovTypes::ChipFamily::CF_ESP32_S3;
#elif CONFIG_IDF_TARGET_ESP32C3
  return ImprovTypes::ChipFamily::CF_ESP32_C3;
#elif CONFIG_IDF_TARGET_ESP32S2
  return ImprovTypes::ChipFamily::CF_ESP32_S2;
#else
  return ImprovTypes::ChipFamily::CF_ESP32;
#endif
}

static String buildDeviceName() {
  uint32_t mac = (uint32_t)(ESP.getEfuseMac() & 0xFFFF);
  char buf[32];
  snprintf(buf, sizeof(buf), "PixelClock-%04X", mac);
  return String(buf);
}

static void onImprovError(ImprovTypes::Error err) {
  Serial.printf("Improv: error %d\n", (int)err);
}

static void onImprovConnected(const char *ssid, const char *password) {
  // The library has already established STA via our custom connect callback,
  // which persisted the credentials to the ESP WiFi NVS (WiFi.persistent).
  // WiFiManager.autoConnect() picks them up on the next boot - no separate
  // settings save is needed.
  Serial.printf("Improv: connected as %s, credentials saved\n", ssid);
  improvSucceeded = true;
}

// Custom connect keeps WIFI_AP_STA mode intact so the WiFiManager captive
// portal AP (already running) stays reachable while we attempt the STA
// connection.
static bool improvCustomConnect(const char *ssid, const char *password) {
  Serial.printf("Improv: attempting STA connect to %s\n", ssid);
  WiFi.persistent(true);  // store creds so the next boot connects silently
  WiFi.begin(ssid, password);
  const uint32_t deadline = millis() + 15000;  // 15s STA connect timeout
  while (WiFi.status() != WL_CONNECTED &&
         (int32_t)(millis() - deadline) < 0) {
    delay(100);
  }
  return WiFi.status() == WL_CONNECTED;
}

void improvSetupBegin(uint32_t windowMs) {
  if (improvInited) return;

  String deviceName = buildDeviceName();
  improvSerial.setDeviceInfo(
      detectChipFamily(),
      "AnimatedPixelClock",
      FIRMWARE_VERSION,
      deviceName.c_str(),
      "http://{LOCAL_IPV4}/");
  improvSerial.onImprovError(onImprovError);
  improvSerial.onImprovConnected(onImprovConnected);
  improvSerial.setCustomConnectWiFi(improvCustomConnect);

  improvDeadline = millis() + windowMs;
  improvSucceeded = false;
  improvInited = true;

  Serial.printf("Improv: listening on Serial for up to %lus "
                "(AP captive portal stays up in parallel)\n",
                (unsigned long)(windowMs / 1000));
}

bool improvSetupTick() {
  if (!improvInited) return false;
  improvSerial.handleSerial();
  if (improvSucceeded) {
    improvSucceeded = false;  // one-shot
    return true;
  }
  return false;
}

bool improvSetupExpired() {
  return improvInited && (int32_t)(millis() - improvDeadline) >= 0;
}

void improvSetupEnd() {
  if (!improvInited) return;
  improvInited = false;
  Serial.println("Improv: setup window closed (AP stays up)");
}

#endif // IMPROV_SETUP_ENABLED
