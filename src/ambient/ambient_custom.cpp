/*
 * AnimatedPixelClock - Ambient: Custom Animation
 *
 * Plays a user-uploaded .pca animation (see anim_store.h) selected by
 * settings.ambientCustomFile. Header, palette and the per-frame delay table
 * are cached at open; one 4KiB heap buffer holds the current 4bpp frame,
 * re-read from flash only on frame advance (~11 fps typical, trivial I/O
 * against the 30 Hz ambient render). Rendering matches the This-is-fine
 * player: equal-color runs become single HLine calls. Missing or corrupt
 * files fall back to the Doom fire effect.
 */

#include <LittleFS.h>

#include "../config/config.h"
#include "../display/display.h"
#include "ambient.h"
#include "anim_store.h"

static File pcaFile;
static PcaHeader pcaHdr;
static uint16_t pcaPalette[PCA_MAX_PALETTE];
static uint16_t* pcaDelays = nullptr;  // frameCount entries
static uint8_t* pcaFrame = nullptr;    // current 4bpp frame
static uint32_t pcaFrameOffset = 0;    // file offset of frame 0
static int pcaIndex = -1;
static unsigned long pcaNextAdvance = 0;
static bool pcaOpen = false;
static bool pcaFailed = false;  // don't retry/spam until invalidated

void ambientCustomInvalidate() {
  if (pcaFile) pcaFile.close();
  free(pcaDelays);
  free(pcaFrame);
  pcaDelays = nullptr;
  pcaFrame = nullptr;
  pcaOpen = false;
  pcaFailed = false;
  pcaIndex = -1;
}

static bool pcaReadFrame(int index) {
  if (!pcaFile.seek(pcaFrameOffset + (uint32_t)index * PCA_FRAME_BYTES))
    return false;
  return pcaFile.read(pcaFrame, PCA_FRAME_BYTES) == PCA_FRAME_BYTES;
}

static bool pcaTryOpen() {
  if (!animFsUsable() || !animValidName(settings.ambientCustomFile))
    return false;

  pcaFile = LittleFS.open(animPath(settings.ambientCustomFile), "r");
  if (!animValidatePca(pcaFile, &pcaHdr)) {
    if (pcaFile) pcaFile.close();
    return false;
  }

  pcaDelays = (uint16_t*)malloc(pcaHdr.frameCount * sizeof(uint16_t));
  pcaFrame = (uint8_t*)malloc(PCA_FRAME_BYTES);
  uint8_t palRaw[PCA_MAX_PALETTE * 2];
  bool ok = pcaDelays && pcaFrame && pcaFile.seek(PCA_HEADER_BYTES) &&
            pcaFile.read(palRaw, pcaHdr.paletteLen * 2) ==
                (size_t)pcaHdr.paletteLen * 2 &&
            pcaFile.read((uint8_t*)pcaDelays, pcaHdr.frameCount * 2) ==
                (size_t)pcaHdr.frameCount * 2;
  if (!ok) {
    ambientCustomInvalidate();
    pcaFailed = true;  // invalidate cleared it; stay failed until next change
    return false;
  }
  for (int i = 0; i < pcaHdr.paletteLen; i++)
    pcaPalette[i] = (uint16_t)palRaw[i * 2] | ((uint16_t)palRaw[i * 2 + 1] << 8);
  // Defensive clamp; the converter already enforces the 30 Hz floor.
  for (int i = 0; i < pcaHdr.frameCount; i++) {
    if (pcaDelays[i] < 34) pcaDelays[i] = 34;
    if (pcaDelays[i] > 5000) pcaDelays[i] = 5000;
  }
  pcaFrameOffset = PCA_HEADER_BYTES + (uint32_t)pcaHdr.paletteLen * 2 +
                   (uint32_t)pcaHdr.frameCount * 2;
  pcaIndex = 0;
  if (!pcaReadFrame(0)) {
    ambientCustomInvalidate();
    pcaFailed = true;
    return false;
  }
  pcaNextAdvance = millis() + pcaDelays[0];
  pcaOpen = true;
  return true;
}

static void pcaDrawFrame() {
  for (int y = 0; y < SCREEN_HEIGHT; y++) {
    const uint8_t* row = pcaFrame + y * (SCREEN_WIDTH / 2);
    int x = 0;
    while (x < SCREEN_WIDTH) {
      uint8_t b = row[x >> 1];
      uint8_t idx = (x & 1) ? (b & 0x0F) : (b >> 4);
      int run = 1;
      while (x + run < SCREEN_WIDTH) {
        uint8_t nb = row[(x + run) >> 1];
        uint8_t nidx = ((x + run) & 1) ? (nb & 0x0F) : (nb >> 4);
        if (nidx != idx) break;
        run++;
      }
      display.drawFastHLine(x, y, run, pcaPalette[idx & (PCA_MAX_PALETTE - 1)]);
      x += run;
    }
  }
}

void ambientCustomFrame() {
  if (!pcaOpen) {
    if (pcaFailed || !pcaTryOpen()) {
      if (!pcaFailed) {
        Serial.printf("AnimStore: cannot play '%s', falling back to fire\n",
                      settings.ambientCustomFile);
        pcaFailed = true;
      }
      ambientFireFrame();
      return;
    }
  }

  unsigned long now = millis();
  if (now >= pcaNextAdvance) {
    // If ambient was inactive for a while, resync instead of fast-forwarding.
    if (now - pcaNextAdvance > 2000) pcaNextAdvance = now;
    pcaIndex = (pcaIndex + 1) % pcaHdr.frameCount;
    pcaNextAdvance += pcaDelays[pcaIndex];
    if (!pcaReadFrame(pcaIndex)) {  // file vanished mid-play (deleted?)
      ambientCustomInvalidate();
      pcaFailed = true;
      ambientFireFrame();
      return;
    }
  }
  pcaDrawFrame();
}
