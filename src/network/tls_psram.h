/*
 * AnimatedPixelClock - TLS allocations in PSRAM
 *
 * Points mbedTLS at PSRAM so a TLS session (the weather fetch) stops taking
 * its buffers from internal SRAM. See tls_psram.cpp for the details.
 */

#ifndef TLS_PSRAM_H
#define TLS_PSRAM_H

// Call once, before anything opens a TLS connection. Every mbedTLS allocation
// (TLS, and the Wi-Fi supplicant's WPA3-SAE math) then tries PSRAM first and
// falls back to internal SRAM, so nothing changes on a board without PSRAM.
void tlsUsePsram();

#endif // TLS_PSRAM_H
