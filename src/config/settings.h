/*
 * AnimatedPixelClock - Settings Module Header
 *
 * Declarations for settings persistence functions.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include "../config/config.h"
#include <Preferences.h>

// Initialize settings (load from NVS or set defaults)
void loadSettings();

// Save current settings to NVS
void saveSettings();

// Brightness helpers
uint8_t sanitizeBrightnessValue(uint8_t value);
bool isZeroBrightnessAllowed();
void sanitizeBrightnessSettings();

// WiFi transmit power, in the 0.25 dBm units of wifi_power_t
#define WIFI_TX_POWER_DEFAULT 78  // 19.5 dBm
uint8_t sanitizeWifiTxPower(uint8_t quarterDbm);

extern Preferences preferences;

#endif // SETTINGS_H
