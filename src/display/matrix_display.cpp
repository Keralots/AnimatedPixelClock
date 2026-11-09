/*
 * AnimatedPixelClock - HUB75 matrix runtime rebuild
 *
 * The driver has no full teardown: its release() leaves the GDMA channel
 * allocated and the transfer running. That Bus_Parallel16 member is private, so
 * it is reached through the explicit instantiation access rule instead of
 * patching the library.
 */

#include "matrix_display.h"

#include <new>

namespace {

template <typename Tag, typename Tag::type M>
struct Steal {
  friend typename Tag::type stolen(Tag) { return M; }
};

struct BusChan {
  typedef gdma_channel_handle_t Bus_Parallel16::*type;
  friend type stolen(BusChan);
};

struct PanelInit {
  typedef bool MatrixPanel_I2S_DMA::*type;
  friend type stolen(PanelInit);
};

template struct Steal<BusChan, &Bus_Parallel16::dma_chan>;
template struct Steal<PanelInit, &MatrixPanel_I2S_DMA::initialized>;

}  // namespace

bool MatrixDisplay::ready() const { return this->*stolen(PanelInit()); }

void MatrixDisplay::releaseDma() {
  gdma_channel_handle_t &chan = dma_bus.*stolen(BusChan());
  if (chan) {
    dma_bus.dma_transfer_stop();
    gdma_disconnect(chan);
    gdma_del_channel(chan);
    chan = nullptr;
  }
}

bool rebuildMatrixDisplay(MatrixDisplay &panel, const HUB75_I2S_CFG &cfg) {
  panel.releaseDma();
  panel.~MatrixDisplay();
  new (&panel) MatrixDisplay(cfg);
  return panel.begin();
}

#if HUB75_PANEL_ROWS > 1

bool MatrixDisplay::canvasToChain(int16_t &x, int16_t &y) const {
  if (x < 0 || x >= _width || y < 0 || y >= _height) return false;
  x += (y / HUB75_PANEL_H) * _width;
  y %= HUB75_PANEL_H;
  return true;
}

void MatrixDisplay::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (canvasToChain(x, y)) MatrixPanel_I2S_DMA::drawPixel(x, y, color);
}

void MatrixDisplay::drawPixelRGB888(int16_t x, int16_t y, uint8_t r, uint8_t g, uint8_t b) {
  if (canvasToChain(x, y)) MatrixPanel_I2S_DMA::drawPixelRGB888(x, y, r, g, b);
}

void MatrixDisplay::drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
  fillRect(x, y, w, 1, color);
}

void MatrixDisplay::drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
  fillRect(x, y, 1, h, color);
}

// Clips to the canvas first: past the right edge the chain carries on into the
// next panel row, so the driver's own clipping would wrap instead of cut.
void MatrixDisplay::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  int32_t right = min<int32_t>((int32_t)x + w, _width);
  int32_t bottom = min<int32_t>((int32_t)y + h, _height);
  int32_t left = max<int32_t>(x, 0);
  int32_t top = max<int32_t>(y, 0);
  if (left >= right) return;
  while (top < bottom) {
    const int32_t panelRow = top / HUB75_PANEL_H;
    const int32_t end = min<int32_t>(bottom, (panelRow + 1) * HUB75_PANEL_H);
    MatrixPanel_I2S_DMA::fillRect(left + panelRow * _width, top % HUB75_PANEL_H,
                                  right - left, end - top, color);
    top = end;
  }
}

#endif  // HUB75_PANEL_ROWS > 1
