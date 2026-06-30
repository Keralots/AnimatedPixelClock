/*
 * AnimatedPixelClock - HUB75 RGB matrix display shim
 *
 * Thin Adafruit-GFX-compatible wrapper around ESP32-HUB75-MatrixPanel-DMA so the
 * existing OLED animation code (which calls a global `display` object) renders on
 * the RGB matrix with ZERO edits to src/clocks/*.
 *
 * Only compiled when DISPLAY_HUB75 is defined (matrix-* build envs). The OLED
 * firmware path never sees this file.
 *
 * Verified hardware config baked in (Phase 1, real panels):
 *   - 2x Waveshare P2.5 64x64 HUB75E chained = 128x64
 *   - driver FM6126A, clkphase=false (fixes dropped rightmost column)
 *   - internal-SRAM DMA only (NOT PSRAM), double-buffered
 *   - pin map identical to bringup/hello_matrix.cpp
 */

#ifndef MATRIX_DISPLAY_H
#define MATRIX_DISPLAY_H

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#define HUB75_PANEL_W 64
#define HUB75_PANEL_H 64
#define HUB75_CHAIN   2   // two panels chained -> 128x64

// Build the verified panel configuration. Returned by value at static-init time;
// no hardware is touched until display.begin() (called from initDisplay()).
inline HUB75_I2S_CFG makeMatrixConfig() {
  // i2s_pins field order is FIXED: r1,g1,b1,r2,g2,b2,a,b,c,d,e,lat,oe,clk
  HUB75_I2S_CFG::i2s_pins pins = {
      1, 2, 4,             // R1, G1, B1
      5, 6, 7,             // R2, G2, B2
      8, 9, 10, 11, 12,    // A, B, C, D, E
      14, 38, 13};         // LAT, OE, CLK
  HUB75_I2S_CFG cfg(HUB75_PANEL_W, HUB75_PANEL_H, HUB75_CHAIN, pins);
  cfg.driver = HUB75_I2S_CFG::FM6126A;  // verified Phase 1
  cfg.clkphase = false;                 // verified: fixes dropped rightmost column
  cfg.double_buff = true;               // matches clear/draw/display() frame model
  return cfg;
}

// OLED-compatible wrapper: adds the three non-GFX methods the animation code uses
// (clearDisplay / display / getBuffer) on top of the GFX-derived matrix panel.
class MatrixDisplay : public MatrixPanel_I2S_DMA {
public:
  explicit MatrixDisplay(const HUB75_I2S_CFG &cfg) : MatrixPanel_I2S_DMA(cfg) {}

  // SSD1306/SH110X frame pattern -> HUB75 equivalents
  inline void clearDisplay() { clearScreen(); }      // clear the (back) draw buffer
  inline void display() { flipDMABuffer(); }         // swap double buffers

  // Pong samples the raw 1-bit OLED framebuffer via getBuffer(); HUB75 has none.
  // Returning nullptr trips Pong's existing `if (!buffer) return;` guard, which
  // cleanly disables only its digit-shatter effect (full port deferred to Phase 3)
  // with no edit to clock_pong.cpp.
  inline uint8_t *getBuffer() { return nullptr; }
};

#endif  // MATRIX_DISPLAY_H
