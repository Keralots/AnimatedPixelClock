/*
 * AnimatedPixelClock - Ambient: Burning Room ("This is fine")
 *
 * Plays the actual comic animation: the camera pulls in from the burning
 * room to a close-up of the dog sipping its mug. 33 frames at 128x64,
 * ~90 ms per frame, 16-color global palette, 4 bits per pixel packed two
 * pixels per byte (~132 KB of flash in thisisfine_frames.h). Playback is
 * wall-clock based so the loop stays smooth regardless of render rate.
 */

#include "ambient.h"

#include "../config/config.h"
#include "../display/display.h"
#include "thisisfine_frames.h"

void ambientThisIsFineFrame() {
  const uint8_t* frame =
      TIF_FRAMES[(millis() / TIF_FRAME_MS) % TIF_FRAME_COUNT];

  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    const uint8_t* row = frame + y * (SCREEN_WIDTH / 2);
    int x = 0;
    while (x < SCREEN_WIDTH) {
      uint8_t b = row[x >> 1];
      uint8_t idx = (x & 1) ? (b & 0x0F) : (b >> 4);
      // Coalesce equal-color runs into one HLine call.
      int run = 1;
      while (x + run < SCREEN_WIDTH) {
        uint8_t nb = row[(x + run) >> 1];
        uint8_t nidx = ((x + run) & 1) ? (nb & 0x0F) : (nb >> 4);
        if (nidx != idx) break;
        run++;
      }
      display.drawFastHLine(x, y, run, TIF_PALETTE[idx]);
      x += run;
    }
  }
}
