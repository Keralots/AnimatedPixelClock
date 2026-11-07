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
