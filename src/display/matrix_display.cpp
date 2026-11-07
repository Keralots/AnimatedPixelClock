/*
 * AnimatedPixelClock - HUB75 matrix runtime rebuild
 *
 * The driver has no teardown: its release() leaves the GDMA channel allocated,
 * the transfer running and the second descriptor list on the heap. These two
 * Bus_Parallel16 members are private, so they are reached through the explicit
 * instantiation access rule instead of patching the library.
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
struct BusDescB {
  typedef HUB75_DMA_DESCRIPTOR_T *Bus_Parallel16::*type;
  friend type stolen(BusDescB);
};

struct PanelInit {
  typedef bool MatrixPanel_I2S_DMA::*type;
  friend type stolen(PanelInit);
};

template struct Steal<BusChan, &Bus_Parallel16::dma_chan>;
template struct Steal<BusDescB, &Bus_Parallel16::_dmadesc_b>;
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
  HUB75_DMA_DESCRIPTOR_T *&descB = dma_bus.*stolen(BusDescB());
  heap_caps_free(descB);
  descB = nullptr;
}

bool rebuildMatrixDisplay(MatrixDisplay &panel, const HUB75_I2S_CFG &cfg) {
  panel.releaseDma();
  panel.~MatrixDisplay();
  new (&panel) MatrixDisplay(cfg);
  return panel.begin();
}
bool MatrixDisplay::mapVirtualToPhysical(
    int16_t &x,
    int16_t &y
) const {
    if (x < 0 || x >= VIRTUAL_WIDTH ||
        y < 0 || y >= VIRTUAL_HEIGHT) {
        return false;
    }

    const int16_t virtualX = x;
    const int16_t virtualY = y;

    x = (virtualY / HUB75_PANEL_H) *
        (HUB75_PANEL_W * VIRTUAL_COLS) + virtualX;

    y = virtualY % HUB75_PANEL_H;

    return true;
}
void MatrixDisplay::drawPixel(
    int16_t x,
    int16_t y,
    uint16_t color
) {
    if (!mapVirtualToPhysical(x, y)) return;

    MatrixPanel_I2S_DMA::drawPixel(x, y, color);
}

void MatrixDisplay::drawPixelRGB888(
    int16_t x,
    int16_t y,
    uint8_t r,
    uint8_t g,
    uint8_t b
) {
    if (!mapVirtualToPhysical(x, y)) return;

    MatrixPanel_I2S_DMA::drawPixelRGB888(x, y, r, g, b);
}

void MatrixDisplay::drawFastHLine(
    int16_t x,
    int16_t y,
    int16_t w,
    uint16_t color
) {
    if (w <= 0 || y < 0 || y >= VIRTUAL_HEIGHT) return;

    int32_t xStart = max<int32_t>(0, x);
    int32_t xEnd = min<int32_t>(VIRTUAL_WIDTH, (int32_t)x + w);

    for (int32_t px = xStart; px < xEnd; px++) {
        drawPixel((int16_t)px, y, color);
    }
}

void MatrixDisplay::drawFastVLine(
    int16_t x,
    int16_t y,
    int16_t h,
    uint16_t color
) {
    if (h <= 0 || x < 0 || x >= VIRTUAL_WIDTH) return;

    int32_t yStart = max<int32_t>(0, y);
    int32_t yEnd = min<int32_t>(VIRTUAL_HEIGHT, (int32_t)y + h);

    for (int32_t py = yStart; py < yEnd; py++) {
        drawPixel(x, (int16_t)py, color);
    }
}

void MatrixDisplay::fillRect(
    int16_t x,
    int16_t y,
    int16_t w,
    int16_t h,
    uint16_t color
) {
    if (w <= 0 || h <= 0) return;

    int32_t xStart = max<int32_t>(0, x);
    int32_t yStart = max<int32_t>(0, y);
    int32_t xEnd = min<int32_t>(VIRTUAL_WIDTH, (int32_t)x + w);
    int32_t yEnd = min<int32_t>(VIRTUAL_HEIGHT, (int32_t)y + h);

    for (int32_t py = yStart; py < yEnd; py++) {
        for (int32_t px = xStart; px < xEnd; px++) {
            drawPixel((int16_t)px, (int16_t)py, color);
        }
    }
}