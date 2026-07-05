/*
 * AnimatedPixelClock - Custom Animation Store
 *
 * LittleFS-backed storage for user-uploaded .pca animations (PCA1 format,
 * produced by tools/gif2pca.py) played by the "Custom animation" ambient
 * style. The feature is only enabled when the filesystem partition is big
 * enough to be useful (the 4MB min_spiffs table has just 128KiB, below a
 * single typical animation, so the feature hides itself there).
 */

#ifndef ANIM_STORE_H
#define ANIM_STORE_H

#include <Arduino.h>
#include <FS.h>

// PCA1 layout: 12-byte header, palette, per-frame delay table, 4bpp frames.
#define PCA_MAGIC "PCA1"
#define PCA_HEADER_BYTES 12
#define PCA_MAX_FRAMES 360
#define PCA_MAX_PALETTE 16
#define PCA_FRAME_BYTES 4096  // 128*64 / 2
#define PCA_MAX_BYTES (1536UL * 1024UL)  // hard upload cap

#define ANIM_DIR "/anim"
#define ANIM_TMP "/anim/upload.tmp"
#define ANIM_FS_MIN_TOTAL (256UL * 1024UL)  // below this the feature is disabled
#define ANIM_FS_FREE_MARGIN (16UL * 1024UL)

struct PcaHeader {
  uint16_t frameCount;
  uint16_t defaultFrameMs;
  uint8_t paletteLen;
  uint8_t flags;
};

// Mount LittleFS (formats on first use) and create the anim directory.
void animStoreInit();

// True when the filesystem mounted and is large enough for the feature.
bool animFsUsable();

size_t animFsFree();
size_t animFsTotal();

// Strict basename check: [A-Za-z0-9_-]{1,24}, no path parts, no extension.
bool animValidName(const char* name);

// Parse + bounds-check the PCA header and verify the EXACT expected file
// length. Leaves the file position unspecified. hdr may be nullptr.
bool animValidatePca(File& f, PcaHeader* hdr);

// Full path "/anim/<name>.pca" for a validated basename.
String animPath(const char* name);

#endif  // ANIM_STORE_H
