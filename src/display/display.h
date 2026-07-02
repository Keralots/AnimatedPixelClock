/*
 * AnimatedPixelClock - Display Module
 *
 * Display initialization and global display object (HUB75 RGB matrix).
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <Adafruit_GFX.h>
#include "../config/user_config.h"

#include "matrix_display.h"
extern MatrixDisplay display;
#ifndef DISPLAY_WHITE
  #define DISPLAY_WHITE 0xFFFF
#endif
#ifndef DISPLAY_BLACK
  #define DISPLAY_BLACK 0x0000
#endif

// ---- User-editable sprite colors ----
// Returns the configured RGB565 color for a ColorSlot. Macro expands at call
// sites where `settings` (config.h) is in scope. Slot enum + defaults live in
// config/color_slots.h.
#define SPRITE_COLOR(slot) (settings.spriteColors[(slot)])

// Initialize display - returns true on success
bool initDisplay();
void applyDisplayBrightness();
void refreshDisplayBrightnessNow();
void checkScheduledBrightness();

// Runtime display control (HTTP API) - not persisted to flash
void setDisplayForcedOff(bool off);
bool isDisplayForcedOff();
void setDisplayBrightnessPercent(uint8_t percent);

#endif // DISPLAY_H
