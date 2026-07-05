/*
 * AnimatedPixelClock - Ambient: Custom Animation
 *
 * Plays a user-uploaded .pca animation (see anim_store.h) selected by
 * settings.ambientCustomFile. Header, palette and the per-frame delay table
 * are cached at open. Rendering matches the This-is-fine player: equal-color
 * runs become single HLine calls. Missing or corrupt files fall back to the
 * Doom fire effect.
 *
 * Two hardware-earned rules shape this file:
 *
 * 1. No File handle is ever kept across calls. littlefs does not support the
 *    same file being open twice, and /api/anim/list's directory iteration
 *    opens every file - a held playback handle got its reads corrupted by
 *    that (short read mid-play). Every read is open/seek/read/close, and
 *    everything runs sequentially on the loop task, so two handles to one
 *    file cannot coexist.
 *
 * 2. No filesystem I/O inside the render tick. A flash read between
 *    clearDisplay() and the buffer flip delays the flip by a few ms on
 *    every frame advance; that phase jitter against the panel's DMA scan
 *    shows as mirrored rolling bands (same physics as the old 60 Hz beat).
 *    The next frame is therefore prefetched into a second buffer from
 *    loop() BETWEEN render ticks (ambientCustomPrefetch), and the advance
 *    just swaps buffer pointers.
 */

#include <LittleFS.h>

#include "../config/config.h"
#include "../display/display.h"
#include "ambient.h"
#include "anim_store.h"

static PcaHeader pcaHdr;
static uint16_t pcaPalette[PCA_MAX_PALETTE];
// Fixed buffers, never heap-allocated: an open attempt right after a save
// runs while the config page reload has the heap under pressure (lwIP
// buffers + page streaming), and a failed 4KB malloc there stranded the
// player on the fire fallback until the user saved again (hardware-seen,
// failReason=alloc). ~9KB of BSS buys an open path that cannot OOM.
static uint16_t pcaDelays[PCA_MAX_FRAMES];
static uint8_t pcaBufA[PCA_FRAME_BYTES];
static uint8_t pcaBufB[PCA_FRAME_BYTES];
static uint8_t* pcaFrame = pcaBufA;    // frame being drawn
static uint8_t* pcaNext = pcaBufB;     // prefetched next frame
static int pcaNextIndex = -1;          // frame held in pcaNext (-1 = none)
static bool pcaWantPrefetch = false;   // loop() should fetch pcaNextIndex
static uint32_t pcaFrameOffset = 0;    // file offset of frame 0
static int pcaIndex = -1;
static unsigned long pcaNextAdvance = 0;
static bool pcaOpen = false;
static bool pcaFailed = false;         // last attempt failed; retried on a timer
static unsigned long pcaRetryAt = 0;   // next automatic retry (2s backoff)
// Why the last open/playback attempt failed (kept across invalidate for
// /api/anim/list diagnostics): 0=ok 1=gate 2=validate 3=header-read
// 4=first-frame 5=prefetch-read 6=alloc
static uint8_t pcaFailReason = 0;

bool ambientCustomPlaying() { return pcaOpen && !pcaFailed; }

uint8_t ambientCustomFailReason() { return pcaFailReason; }

void ambientCustomInvalidate() {
  pcaNextIndex = -1;
  pcaWantPrefetch = false;
  pcaOpen = false;
  pcaFailed = false;
  pcaIndex = -1;
}

// Transient open per read - rule 1 in the header comment.
static bool pcaReadFrame(int index, uint8_t* dst) {
  File f = LittleFS.open(animPath(settings.ambientCustomFile), "r");
  if (!f) return false;
  bool ok = f.seek(pcaFrameOffset + (uint32_t)index * PCA_FRAME_BYTES) &&
            f.read(dst, PCA_FRAME_BYTES) == PCA_FRAME_BYTES;
  f.close();
  return ok;
}

static bool pcaTryOpen() {
  if (!animFsUsable() || !animValidName(settings.ambientCustomFile)) {
    pcaFailReason = 1;
    return false;
  }

  File f = LittleFS.open(animPath(settings.ambientCustomFile), "r");
  if (!animValidatePca(f, &pcaHdr)) {
    pcaFailReason = 2;
    if (f) f.close();
    return false;
  }

  uint8_t palRaw[PCA_MAX_PALETTE * 2];
  bool ok = f.seek(PCA_HEADER_BYTES) &&
            f.read(palRaw, pcaHdr.paletteLen * 2) ==
                (size_t)pcaHdr.paletteLen * 2 &&
            f.read((uint8_t*)pcaDelays, pcaHdr.frameCount * 2) ==
                (size_t)pcaHdr.frameCount * 2;
  f.close();
  if (!ok) {
    pcaFailReason = 3;
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
  if (!pcaReadFrame(0, pcaFrame)) {  // one-time hitch at open is fine
    pcaFailReason = 4;
    return false;
  }
  pcaNextAdvance = millis() + pcaDelays[0];
  pcaNextIndex = -1;
  pcaWantPrefetch = true;  // loop() pulls frame 1 in before it's due
  pcaOpen = true;
  pcaFailReason = 0;
  return true;
}

// Called from loop() between render ticks - rule 2 in the header comment.
void ambientCustomPrefetch() {
  if (!pcaOpen || !pcaWantPrefetch) return;
  int next = (pcaIndex + 1) % pcaHdr.frameCount;
  if (pcaReadFrame(next, pcaNext)) {
    pcaNextIndex = next;
  } else {  // file vanished (deleted mid-play?) - fail, render falls back
    ambientCustomInvalidate();
    pcaFailed = true;
    pcaRetryAt = millis() + 2000;
    pcaFailReason = 5;
  }
  pcaWantPrefetch = false;
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
    // A failed attempt shows the fire fallback but retries every 2s: open
    // can fail transiently (e.g. during the post-save page-reload burst)
    // and must not strand the user on fire until the next manual save.
    if (pcaFailed && millis() < pcaRetryAt) {
      ambientFireFrame();
      return;
    }
    if (!pcaTryOpen()) {
      if (!pcaFailed) {
        Serial.printf("AnimStore: cannot play '%s' (reason %d), retrying\n",
                      settings.ambientCustomFile, pcaFailReason);
      }
      pcaFailed = true;
      pcaRetryAt = millis() + 2000;
      ambientFireFrame();
      return;
    }
    pcaFailed = false;
  }

  unsigned long now = millis();
  if (now >= pcaNextAdvance) {
    // If ambient was inactive for a while, resync instead of fast-forwarding.
    if (now - pcaNextAdvance > 2000) pcaNextAdvance = now;
    int next = (pcaIndex + 1) % pcaHdr.frameCount;
    if (pcaNextIndex == next) {
      // Swap in the prefetched frame - no filesystem I/O on this path.
      uint8_t* t = pcaFrame;
      pcaFrame = pcaNext;
      pcaNext = t;
      pcaIndex = next;
      pcaNextIndex = -1;
      pcaNextAdvance += pcaDelays[pcaIndex];
      pcaWantPrefetch = true;
    }
    // Prefetch not done yet (loop was busy): hold the current frame one
    // more render tick rather than reading flash inside the render path.
  }
  pcaDrawFrame();
}
