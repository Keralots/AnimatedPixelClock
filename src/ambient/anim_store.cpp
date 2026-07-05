/*
 * AnimatedPixelClock - Custom Animation Store
 *
 * See anim_store.h. Upload streaming itself lives in web.cpp (it needs the
 * WebServer upload callback); this file owns mounting, naming and the PCA
 * header validation shared by upload finalize and the player.
 */

#include "anim_store.h"

#include <LittleFS.h>

static bool fsMounted = false;

void animStoreInit() {
  // format-on-fail: the partition ships unformatted after a plain OTA.
  if (!LittleFS.begin(true)) {
    Serial.println("AnimStore: LittleFS mount failed, custom animations off");
    return;
  }
  fsMounted = true;
  if (!LittleFS.exists(ANIM_DIR)) LittleFS.mkdir(ANIM_DIR);
  // A crashed upload may have left a temp file behind.
  if (LittleFS.exists(ANIM_TMP)) LittleFS.remove(ANIM_TMP);
  Serial.printf("AnimStore: %u/%u KiB used%s\n",
                (unsigned)(LittleFS.usedBytes() / 1024),
                (unsigned)(LittleFS.totalBytes() / 1024),
                animFsUsable() ? "" : " (too small, feature disabled)");
}

bool animFsUsable() {
  return fsMounted && LittleFS.totalBytes() >= ANIM_FS_MIN_TOTAL;
}

size_t animFsFree() {
  if (!fsMounted) return 0;
  size_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  return total > used ? total - used : 0;
}

size_t animFsTotal() { return fsMounted ? LittleFS.totalBytes() : 0; }

bool animValidName(const char* name) {
  if (!name || !name[0]) return false;
  for (int i = 0; name[i]; i++) {
    if (i >= 24) return false;
    char c = name[i];
    if (!isalnum((unsigned char)c) && c != '_' && c != '-') return false;
  }
  return true;
}

bool animValidatePca(File& f, PcaHeader* hdr) {
  if (!f) return false;
  uint8_t h[PCA_HEADER_BYTES];
  if (!f.seek(0) || f.read(h, sizeof(h)) != sizeof(h)) return false;
  if (memcmp(h, PCA_MAGIC, 4) != 0) return false;

  uint16_t frames = (uint16_t)h[4] | ((uint16_t)h[5] << 8);
  uint16_t defMs = (uint16_t)h[6] | ((uint16_t)h[7] << 8);
  uint8_t palette = h[8];
  if (frames < 1 || frames > PCA_MAX_FRAMES) return false;
  if (palette < 2 || palette > PCA_MAX_PALETTE) return false;
  if (defMs < 20 || defMs > 5000) return false;

  // 32-bit safe: max is 12 + 32 + 720 + 360*4096 < 1.5MiB.
  uint32_t expected = PCA_HEADER_BYTES + (uint32_t)palette * 2 +
                      (uint32_t)frames * 2 + (uint32_t)frames * PCA_FRAME_BYTES;
  if ((uint32_t)f.size() != expected) return false;

  if (hdr) {
    hdr->frameCount = frames;
    hdr->defaultFrameMs = defMs;
    hdr->paletteLen = palette;
    hdr->flags = h[9];
  }
  return true;
}

String animPath(const char* name) {
  return String(ANIM_DIR "/") + name + ".pca";
}
