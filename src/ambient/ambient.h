/*
 * AnimatedPixelClock - Ambient Screensaver Module
 *
 * Full-screen ambient effects shown instead of the clock during a scheduled
 * window (or when forced via /api/mode/ambient). Each effect keeps its own
 * file-local state and renders one frame per call at the 30 Hz ambient rate
 * (a full-panel redraw+flip at 60 Hz beats against the DMA scan as rolling
 * dim bands, so ambient is gated to 30 in getOptimalRefreshRate()).
 */

#ifndef AMBIENT_H
#define AMBIENT_H

#include <Arduino.h>

// True while the ambient screen should replace the clock (schedule or forced).
bool ambientActive();

// Render one frame of the selected ambient style (plus the optional corner clock).
void displayAmbient();

// Per-effect frames (called by displayAmbient's dispatcher).
void ambientInvadersFrame();
void ambientPacmanChaseFrame();
void ambientStarsFrame();
void ambientAquariumFrame();
void ambientThisIsFineFrame();
void ambientCustomFrame();

// Drop the custom player's cached file/buffers (after upload/delete/save).
void ambientCustomInvalidate();

// True while the custom player has an animation loaded and not failed.
bool ambientCustomPlaying();

// Why the last open/playback attempt failed (0 = ok; see ambient_custom.cpp).
uint8_t ambientCustomFailReason();

// Prefetch the next animation frame from flash. Call from loop() every pass
// (cheap no-op when idle) - keeps filesystem reads out of the render tick,
// whose delayed buffer flips otherwise beat against the DMA scan.
void ambientCustomPrefetch();

#endif // AMBIENT_H
