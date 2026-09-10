/*
 * AnimatedPixelClock - HUB75 RGB matrix display shim
 *
 * Thin Adafruit-GFX-compatible wrapper around ESP32-HUB75-MatrixPanel-DMA. The
 * animation code calls a global `display` object with the OLED-era frame model
 * (clearDisplay / draw / display), which this class maps onto the DMA panel.
 *
 * Verified hardware config baked in (Phase 1, real panels):
 *   - 2x Waveshare P2.5 64x64 HUB75E chained = 128x64
 *   - driver FM6126A, clkphase=false (fixes dropped rightmost column)
 *   - internal-SRAM DMA only (NOT PSRAM), double-buffered
 *   - pin map from hub75_pins.h, shared with bringup/hello_matrix.cpp
 */

#ifndef MATRIX_DISPLAY_H
#define MATRIX_DISPLAY_H

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "hub75_pins.h"

#define HUB75_PANEL_W 64
#define HUB75_PANEL_H 64
#define HUB75_CHAIN   2   // two panels chained -> 128x64

// Build the verified panel configuration. Returned by value at static-init time;
// no hardware is touched until display.begin() (called from initDisplay()).
inline HUB75_I2S_CFG makeMatrixConfig() {
  // i2s_pins field order is FIXED: r1,g1,b1,r2,g2,b2,a,b,c,d,e,lat,oe,clk
  HUB75_I2S_CFG::i2s_pins pins = {
      HUB75_PIN_R1, HUB75_PIN_G1, HUB75_PIN_B1,
      HUB75_PIN_R2, HUB75_PIN_G2, HUB75_PIN_B2,
      HUB75_PIN_A, HUB75_PIN_B, HUB75_PIN_C, HUB75_PIN_D, HUB75_PIN_E,
      HUB75_PIN_LAT, HUB75_PIN_OE, HUB75_PIN_CLK};
  HUB75_I2S_CFG cfg(HUB75_PANEL_W, HUB75_PANEL_H, HUB75_CHAIN, pins);
  cfg.driver = HUB75_I2S_CFG::FM6126A;  // verified Phase 1
  cfg.clkphase = false;                 // verified: fixes dropped rightmost column
  cfg.double_buff = true;               // matches clear/draw/display() frame model
  return cfg;
}

// Adds the non-GFX frame methods the animation code uses (clearDisplay /
// display) on top of the GFX-derived matrix panel.
class MatrixDisplay : public MatrixPanel_I2S_DMA {
public:
  explicit MatrixDisplay(const HUB75_I2S_CFG &cfg) : MatrixPanel_I2S_DMA(cfg) {}

  inline void clearDisplay() { clearScreen(); }      // clear the (back) draw buffer
  inline void display() {
    flipDMABuffer();
    lastFlipUs = micros();
    hasFlipped = true;
  }

  // The S3 driver queues a DMA chain switch but does not wait for EOF.
  // A full scan after the request guarantees the old front buffer is free.
  inline void waitForScanCompletion() {
    if (!hasFlipped || calculated_refresh_rate <= 0) return;
    const uint32_t scanUs = (1000000UL + calculated_refresh_rate - 1) /
                            calculated_refresh_rate + 100;
    const uint32_t elapsed = micros() - lastFlipUs;
    if (elapsed < scanUs) delayMicroseconds(scanUs - elapsed);
  }

  // No readable framebuffer on the DMA panel; nothing samples it anymore
  // (Pong's digit shatter reads the 5x7 glyph font instead), kept only so any
  // future caller fails safe on a null check rather than a build error.
  inline uint8_t *getBuffer() { return nullptr; }
private:
  uint32_t lastFlipUs = 0;
  bool hasFlipped = false;
};

#endif  // MATRIX_DISPLAY_H
