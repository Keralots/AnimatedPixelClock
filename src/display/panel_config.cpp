/*
 * AnimatedPixelClock - HUB75 panel options
 */

#include "panel_config.h"

#include <Preferences.h>

#include "display.h"

PanelOptions panelOptions;
uint8_t panelTestPattern = 0;

static const PanelOptions PANEL_DEFAULTS = {
    HUB75_I2S_CFG::FM6126A, 8, DEFAULT_LAT_BLANKING, false, 8, 60};

static PanelOptions sanitized(PanelOptions o) {
  if (o.driver > HUB75_I2S_CFG::DP3246) o.driver = PANEL_DEFAULTS.driver;
  if (o.clockMHz != 8 && o.clockMHz != 16 && o.clockMHz != 20) o.clockMHz = 8;
  o.latchBlanking = constrain(o.latchBlanking, 1, 4);
  o.colorDepth = constrain(o.colorDepth, 4, 8);
  o.minRefresh = constrain(o.minRefresh, 30, 200);
  return o;
}

void loadPanelOptions() {
  Preferences p;
  panelOptions = PANEL_DEFAULTS;
  if (!p.begin("panel", true)) return;  // namespace absent until the first save
  panelOptions.driver = p.getUChar("driver", PANEL_DEFAULTS.driver);
  panelOptions.clockMHz = p.getUChar("clockMHz", PANEL_DEFAULTS.clockMHz);
  panelOptions.latchBlanking = p.getUChar("latBlank", PANEL_DEFAULTS.latchBlanking);
  panelOptions.clkPhase = p.getBool("clkPhase", PANEL_DEFAULTS.clkPhase);
  panelOptions.colorDepth = p.getUChar("depth", PANEL_DEFAULTS.colorDepth);
  panelOptions.minRefresh = p.getUChar("minRefresh", PANEL_DEFAULTS.minRefresh);
  p.end();
  panelOptions = sanitized(panelOptions);
}

bool savePanelOptions(const PanelOptions &opts) {
  Preferences p;
  if (!p.begin("panel", false)) return false;
  PanelOptions o = sanitized(opts);
  p.putUChar("driver", o.driver);
  p.putUChar("clockMHz", o.clockMHz);
  p.putUChar("latBlank", o.latchBlanking);
  p.putBool("clkPhase", o.clkPhase);
  p.putUChar("depth", o.colorDepth);
  p.putUChar("minRefresh", o.minRefresh);
  p.end();
  panelOptions = o;
  return true;
}

static HUB75_I2S_CFG panelConfig(uint8_t colorDepth) {
  HUB75_I2S_CFG cfg = makeMatrixConfig();
  cfg.driver = (HUB75_I2S_CFG::shift_driver)panelOptions.driver;
  cfg.i2sspeed = panelOptions.clockMHz == 20   ? HUB75_I2S_CFG::HZ_20M
                 : panelOptions.clockMHz == 16 ? HUB75_I2S_CFG::HZ_16M
                                               : HUB75_I2S_CFG::HZ_8M;
  cfg.latch_blanking = panelOptions.latchBlanking;
  cfg.clkphase = panelOptions.clkPhase;
  cfg.setPixelColorDepthBits(colorDepth);
  cfg.min_refresh_rate = panelOptions.minRefresh;
  return cfg;
}

void applyPanelOptions() {
  display.setCfg(panelConfig(panelOptions.colorDepth));
}

bool setPanelColorDepth(uint8_t colorDepth) {
  colorDepth = constrain(colorDepth, 4, panelOptions.colorDepth);
  if (display.getCfg().getPixelColorDepthBits() == colorDepth) return true;
  if (!rebuildMatrixDisplay(display, panelConfig(colorDepth))) return false;
  display.setBrightness8(getAppliedBrightness());
  return true;
}

int panelRefreshRate() { return display.refreshRate(); }

void drawPanelTestPattern() {
  const int w = display.width(), h = display.height();
  switch (panelTestPattern) {
    case 1: display.fillScreenRGB888(255, 255, 255); break;
    case 2: display.fillScreenRGB888(64, 64, 64); break;
    case 3: display.fillScreenRGB888(12, 12, 12); break;
    case 4: display.fillScreenRGB888(255, 0, 0); break;
    case 5: display.fillScreenRGB888(0, 255, 0); break;
    case 6: display.fillScreenRGB888(0, 0, 255); break;
    case 7:  // grey ramp left to right: white, red, green, blue bands
      // Starts at 5, the first input the library's CIE1931 table lights;
      // lower values map to off and left the first columns dark.
      for (int y = 0; y < h; y++) {
        const int band = y * 4 / h;
        for (int x = 0; x < w; x++) {
          const uint8_t v = (uint8_t)(5 + x * 250 / (w - 1));
          display.drawPixelRGB888(x, y, band == 0 || band == 1 ? v : 0,
                                  band == 0 || band == 2 ? v : 0,
                                  band == 0 || band == 3 ? v : 0);
        }
      }
      break;
    case 8:  // one-pixel checkerboard
      for (int y = 0; y < h; y++)
        for (int x = (y & 1); x < w; x += 2)
          display.drawPixelRGB888(x, y, 255, 255, 255);
      break;
  }
}
