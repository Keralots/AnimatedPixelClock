/*
 * hello_matrix.cpp - HUB75 RGB matrix hardware bring-up (Phase 1)
 * Project: AnimatedPixelClock
 *
 * Target: Waveshare ESP32-S3-Zero driving 2x Waveshare P2.5 64x64 HUB75E
 *         panels chained (panel1 JOUT -> panel2 JIN) = 128x64 RGB.
 *
 * Build / flash:  platformio run -e matrix-s3 --target upload
 *   (hold BOOT/GPIO0 while plugging USB to enter download mode on the S3-Zero)
 *
 * Purpose: prove the hardware path BEFORE porting any clock animation -
 * pin map, color order, 1/32 scan, panel chaining/seam, and FM6126A init.
 *
 * --- IF THE SCREEN IS DEAD-BLACK ---
 *   With PANELS=2 a blank screen is undiagnosable (panel1? panel2? ribbon?
 *   FM6126A? ESP wiring?). First recovery step: set PANELS to 1 below,
 *   reflash, and drive ONLY the panel wired to the ESP. Once that single
 *   panel lights, restore PANELS=2; the seam test (pattern 6) then validates
 *   the chain. Also try toggling USE_FM6126A.
 */

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

// ---- Panel geometry ----
#define PANEL_W 64    // single module width
#define PANEL_H 64    // single module height
#define PANELS  2     // chain length: 2 = full 128x64 (set to 1 to isolate)

// ---- Driver init ----
// Waveshare 64x64 units commonly use FM6126A, which needs an init sequence.
// KNOWN UNKNOWN until verified against the panel's IC markings: if the screen
// stays blank with this ON, set it to 0 (and vice-versa).
#define USE_FM6126A 1

// Clock phase. Default-true drops the RIGHTMOST column on some panels (esp.
// FM6126A Waveshare) - the missing right-edge column / corner. Set false to fix.
// If instead the FIRST column doubles or the image shifts, set this back to true.
#define CLK_PHASE false

// ---- ESP32-S3-Zero -> HUB75 pin map (locked; see docs/HUB75_WIRING.md) ----
#define PIN_R1  1
#define PIN_G1  2
#define PIN_B1  4
#define PIN_R2  5
#define PIN_G2  6
#define PIN_B2  7
#define PIN_A   8
#define PIN_B   9
#define PIN_C   10
#define PIN_D   11
#define PIN_E   12   // mandatory for 64x64 (1/32 scan)
#define PIN_CLK 13
#define PIN_LAT 14
#define PIN_OE  38

static MatrixPanel_I2S_DMA *dma = nullptr;
static const uint16_t TOTAL_W = PANEL_W * PANELS;   // 128 with PANELS=2

static void hold(const char *what, uint32_t ms) {
  Serial.printf("[bringup] %s\n", what);
  delay(ms);
}

void setup() {
  Serial.begin(115200);
  // Native USB-CDC enumerates AFTER boot; without this wait the first prints
  // are lost before the host attaches.
  while (!Serial && millis() < 2000) {
    delay(10);
  }
  Serial.println();
  Serial.printf("[bringup] HUB75 %dx%d (chain=%d) FM6126A=%d\n",
                TOTAL_W, PANEL_H, PANELS, USE_FM6126A);

  // i2s_pins field order is FIXED: r1,g1,b1,r2,g2,b2,a,b,c,d,e,lat,oe,clk
  HUB75_I2S_CFG::i2s_pins pins = {
      PIN_R1, PIN_G1, PIN_B1,
      PIN_R2, PIN_G2, PIN_B2,
      PIN_A, PIN_B, PIN_C, PIN_D, PIN_E,
      PIN_LAT, PIN_OE, PIN_CLK};

  HUB75_I2S_CFG mxconfig(PANEL_W, PANEL_H, PANELS, pins);
#if USE_FM6126A
  mxconfig.driver = HUB75_I2S_CFG::FM6126A;
#endif
  mxconfig.clkphase = CLK_PHASE;   // fixes dropped rightmost column (see define)
  // Internal SRAM DMA only - do NOT route the buffer through the S3-Zero's
  // slow Quad-SPI PSRAM (library default is internal SRAM, so leave as-is).

  dma = new MatrixPanel_I2S_DMA(mxconfig);
  if (!dma->begin()) {
    Serial.println("[bringup] dma->begin() FAILED (DMA alloc?) - halting");
    while (true) {
      delay(1000);
    }
  }
  dma->setBrightness8(90);   // ~35% - cap current on first power-on
  dma->clearScreen();
  Serial.println("[bringup] begin() OK - cycling test patterns");
}

void loop() {
  const uint16_t RED   = dma->color565(255, 0, 0);
  const uint16_t GREEN = dma->color565(0, 255, 0);
  const uint16_t BLUE  = dma->color565(0, 0, 255);
  const uint16_t WHITE = dma->color565(255, 255, 255);

  // 1. Solid R / G / B - verifies color order + every pixel lights
  dma->fillScreen(RED);    hold("fill RED", 2000);
  dma->fillScreen(GREEN);  hold("fill GREEN", 2000);
  dma->fillScreen(BLUE);   hold("fill BLUE", 2000);

  // 2. Full white - brief current sanity check (max draw on both panels)
  dma->fillScreen(WHITE);  hold("fill WHITE (brief)", 800);

  // 3. 1px border - edges + correct 1/32 scan. drawRect args = (x, y, w, h)
  dma->clearScreen();
  dma->drawRect(0, 0, TOTAL_W, PANEL_H, WHITE);
  hold("border rect", 2000);

  // 4. Corner pixels - origin + canvas extents
  dma->clearScreen();
  dma->drawPixel(0, 0, WHITE);
  dma->drawPixel(TOTAL_W - 1, 0, WHITE);
  dma->drawPixel(0, PANEL_H - 1, WHITE);
  dma->drawPixel(TOTAL_W - 1, PANEL_H - 1, WHITE);
  hold("4 corners", 2000);

  // 5. Diagonal - both panels read as one continuous 128-wide canvas
  dma->clearScreen();
  dma->drawLine(0, 0, TOTAL_W - 1, PANEL_H - 1, WHITE);
  hold("diagonal", 2000);

  // 6. Seam test - left half RED, right half BLUE. Confirms JOUT->JIN chain
  //    order and a clean panel boundary at x=64 (meaningful with PANELS>=2).
  dma->fillRect(0, 0, TOTAL_W / 2, PANEL_H, RED);
  dma->fillRect(TOTAL_W / 2, 0, TOTAL_W - TOTAL_W / 2, PANEL_H, BLUE);
  hold("seam test (L=red R=blue)", 3000);
}
