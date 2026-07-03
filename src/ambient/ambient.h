/*
 * AnimatedPixelClock - Ambient Screensaver Module
 *
 * Full-screen ambient effects shown instead of the clock during a scheduled
 * window (or when forced via /api/mode/ambient). Each effect keeps its own
 * file-local state and renders one frame per call at the 60 Hz boost rate.
 */

#ifndef AMBIENT_H
#define AMBIENT_H

#include <Arduino.h>

// True while the ambient screen should replace the clock (schedule or forced).
bool ambientActive();

// Render one frame of the selected ambient style (plus the optional corner clock).
void displayAmbient();

// Per-effect frames (called by displayAmbient's dispatcher).
void ambientFireFrame();
void ambientPlasmaFrame();
void ambientLavaFrame();
void ambientStarsFrame();
void ambientAquariumFrame();
void ambientThisIsFineFrame();

// ---- Seasonal holiday overlays (independent of ambient) ----

// True while a date-driven overlay is animating (drives the 30 Hz refresh floor).
bool seasonalOverlayActive();

// Draw the active overlay over the current clock frame (no-op outside windows).
void drawSeasonalOverlay();

#endif // AMBIENT_H
