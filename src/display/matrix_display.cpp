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

template struct Steal<BusChan, &Bus_Parallel16::dma_chan>;
template struct Steal<BusDescB, &Bus_Parallel16::_dmadesc_b>;

}  // namespace

void MatrixDisplay::releaseDma() {
  dma_bus.dma_transfer_stop();
  gdma_channel_handle_t &chan = dma_bus.*stolen(BusChan());
  if (chan) {
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
