/*
 * AnimatedPixelClock - HUB75 panel options
 *
 * Driver chip, clock and timing choices that differ between panel batches.
 * Stored in their own Preferences namespace and applied before display.begin(),
 * so a change takes effect on the next boot.
 */

#ifndef PANEL_CONFIG_H
#define PANEL_CONFIG_H

#include <Arduino.h>

struct PanelOptions {
  uint8_t driver;        // HUB75_I2S_CFG::shift_driver
  uint8_t clockMHz;      // 8, 16 or 20
  uint8_t latchBlanking; // 1-4 clocks of OE blanking around LAT
  bool clkPhase;         // true = data clocked on the rising edge
  uint8_t colorDepth;    // bits per channel, 4-8
  uint8_t minRefresh;    // Hz, 30-200
};

extern PanelOptions panelOptions;

void loadPanelOptions();
bool savePanelOptions(const PanelOptions &opts);
void applyPanelOptions();  // must run before display.begin()
int panelRefreshRate();    // measured by the driver after begin()
// Restarts the running panel at a lower colour depth (capped at the saved
// one), which frees internal SRAM held by the DMA buffers and descriptors.
bool setPanelColorDepth(uint8_t colorDepth);

// Test pattern overlay: 0 = off, otherwise replaces the normal screen.
extern uint8_t panelTestPattern;
void drawPanelTestPattern();

#endif  // PANEL_CONFIG_H
