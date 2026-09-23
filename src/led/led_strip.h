/*
 * AnimatedPixelClock - WS2812B accent strip
 *
 * Optional strip on one spare GPIO, driven from RMT. Effects in led_strip.cpp.
 */

#ifndef LED_STRIP_H
#define LED_STRIP_H

#include <Arduino.h>

// Boards with a known-free header pin override this in platformio.ini.
#ifndef LED_STRIP_DEFAULT_PIN
#define LED_STRIP_DEFAULT_PIN 21
#endif

#define LED_STRIP_MAX_COUNT 300
#define LED_STRIP_DEFAULT_COUNT 28
// 0 = uncapped. Default fits a 5V/3A supply shared with the panels.
#define LED_STRIP_DEFAULT_MA 1500
#define LED_STRIP_MAX_MA 10000

// Settings::ledEffect ids. Saved state: append only.
#define LED_EFFECT_SOLID 0
#define LED_EFFECT_RETIRED_BREATHE 1  // loads as Solid
#define LED_EFFECT_WAVE 2
#define LED_EFFECT_VU 3
#define LED_EFFECT_CLOCK 4
#define LED_EFFECT_WEATHER 5
#define LED_EFFECT_RAINBOW 6
#define LED_EFFECT_FIRE 7
#define LED_EFFECT_METEOR 8
#define LED_EFFECT_SCANNER 9
#define LED_EFFECT_COUNT 10

// Rainbow size, fire sparking, tail length (percent).
#define LED_INTENSITY_DEFAULT 50

// VU gain percent. Stored in a uint8_t, so the max must stay <= 255.
#define LED_VU_GAIN_MIN 25
#define LED_VU_GAIN_MAX 250
#define LED_VU_GAIN_DEFAULT 100

// VU arming, seconds.
#define LED_VU_START_MAX 30
#define LED_VU_START_DEFAULT 3
#define LED_VU_STOP_MIN 1
#define LED_VU_STOP_MAX 120
#define LED_VU_STOP_DEFAULT 5
// Settings::ledVuIdle: any non-VU effect id, or this for dark.
#define LED_VU_IDLE_OFF 255
#define LED_VU_IDLE_DEFAULT LED_EFFECT_SOLID

// Measured: 28 LEDs drew 1.4 A at full white.
#define LED_MA_FULL_WHITE 50

// Real GPIO, not GPIO0/45, HUB75, flash/PSRAM, USB or UART0.
bool ledPinUsable(int pin);
bool ledEffectValid(int effect);
bool ledVuIdleValid(int effect);

void ledInit();
// Re-reads pin and count, reinstalling RMT when they changed. Safe from a save.
void ledApplySettings();
void ledLoop();

#endif  // LED_STRIP_H
