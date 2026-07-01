/*
 * AnimatedPixelClock - CH1116 Display Driver
 *
 * The CH1116 (used on some 1.54" 128x64 OLED panels) is an SH1106-compatible
 * controller: same command set, same 132-column GDDRAM, same init sequence.
 * The only practical difference is the column mapping - the visible 128 columns
 * sit one column further into GDDRAM than on a genuine SH1106.
 *
 * The stock Adafruit SH1106 driver hardcodes a 2-column start offset, which on a
 * CH1116 leaves one stray column of stale RAM visible on the LEFT edge (and
 * clips one pixel off the right). This thin subclass reuses the entire SH1106
 * init/draw path and only corrects the offset after begin().
 *
 * No edits to the Adafruit library are needed (it would be wiped on lib
 * reinstall anyway) - _page_start_offset is protected, so a subclass can set it.
 */

#ifndef CH1116_H
#define CH1116_H

#include <Adafruit_SH110X.h>

// Column start offset for CH1116 panels. SH1106 uses 2 (its visible 128 columns
// sit inside a 132-column GDDRAM). The CH1116 maps its visible window directly to
// GDDRAM columns 0..127, so it needs offset 0 - using the SH1106's 2 leaves a
// stale column on the left, and 1 drops the right-edge column off-screen.
// If a particular panel still shows a stray column, try 1 or 2 (override via
// build flag).
#ifndef CH1116_COL_OFFSET
  #define CH1116_COL_OFFSET 0
#endif

class Adafruit_CH1116 : public Adafruit_SH1106G {
public:
  // CH1116 is electrically identical to SH1106 - reuse all SH1106G constructors.
  using Adafruit_SH1106G::Adafruit_SH1106G;

  bool begin(uint8_t i2caddr = 0x3C, bool reset = true) {
    bool ok = Adafruit_SH1106G::begin(i2caddr, reset);
    // Override the SH1106's 2-column offset with the CH1116 value. Only affects
    // subsequent display() calls, so setting it after begin() is safe.
    _page_start_offset = CH1116_COL_OFFSET;
    return ok;
  }
};

#endif // CH1116_H
