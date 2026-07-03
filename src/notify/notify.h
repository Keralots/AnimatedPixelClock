/*
 * AnimatedPixelClock - Notification Banner Module
 *
 * Scrolling text banner shown over any screen (clock or PC stats), pushed
 * via POST /api/notify. State is file-local in notify.cpp.
 */

#ifndef NOTIFY_H
#define NOTIFY_H

#include <Arduino.h>

// Maximum notification text length (excluding terminator)
#define NOTIFY_TEXT_MAX 200

// Show a notification. iconId: index into the built-in icon set, -1 = none.
// position: 0 = bottom band, 1 = top band. durationMs is clamped by the caller.
void notifySet(const char* text, uint16_t color565, int8_t iconId,
               uint32_t durationMs, uint8_t position);

// Clear the current notification immediately.
void notifyDismiss();

// True while a notification is on screen (drives the 60 Hz refresh boost).
bool notifyActive();

// Draw the banner. Call after the main screen render, before display.display().
void drawNotifyOverlay();

// Look up a built-in icon by name ("bell", "mail", ...). Returns -1 if unknown.
int notifyIconIdByName(const char* name);

// Comma-separated list of valid icon names (for API error messages / docs).
const char* notifyIconNames();

#endif // NOTIFY_H
