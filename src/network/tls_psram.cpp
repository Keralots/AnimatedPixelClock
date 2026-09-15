/*
 * AnimatedPixelClock - TLS allocations in PSRAM
 *
 * The precompiled arduino-esp32 2.0.17 libraries are built with
 * CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC and CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN
 * 16384, so every TLS session takes a 16KB input and a 16KB output buffer,
 * plus the handshake state, from internal SRAM - the scarce memory on these
 * boards, next to megabytes of PSRAM.
 *
 * The same libraries' mbedtls/esp_config.h defines MBEDTLS_PLATFORM_MEMORY
 * without the CALLOC/FREE macros, which keeps mbedtls_platform_set_calloc_free()
 * in the library: the custom allocation mode of the mbedTLS Kconfig help,
 * chosen at boot. The switch is global, so the Wi-Fi supplicant's mbedTLS
 * allocations (WPA3-SAE) move to PSRAM too. Internal SRAM stays the fallback
 * when a PSRAM allocation fails. The default free is heap_caps_free(), which
 * accepts a pointer from any heap, so anything allocated before the switch is
 * still freed correctly.
 */

#include "tls_psram.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>

static void* tlsCalloc(size_t n, size_t size) {
  void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return p;
}

static void tlsFree(void* p) { heap_caps_free(p); }

void tlsUsePsram() {
  if (!psramFound()) return;
  mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree);
}
