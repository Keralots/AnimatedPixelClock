/*
 * AnimatedPixelClock - Audio Spectrum Visualizer
 *
 * 32-band bar EQ fed by the PC companion over UDP (binary "FFT1" packets,
 * ~25 Hz). Runs as a forced display mode (/api/mode/viz), like the clock
 * and ambient overrides.
 */

#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <Arduino.h>

#define VIZ_BANDS 32
#define VIZ_PACKET_LEN 36  // "FFT1" magic + 32 amplitude bytes

// Consume a UDP packet if it is a spectrum packet. Returns true when
// consumed (caller skips JSON parsing and logging).
bool vizIngest(const uint8_t* buf, int len);

// True when a spectrum packet arrived within the last maxAgeMs.
bool vizRecentEnough(unsigned long maxAgeMs);

// Call when /api/mode/viz forces the mode on: opens a 10s grace window so
// the "No audio data" screen shows even before the first packet.
void vizNoteForced();

// Show the visualizer? True while fed (10s tolerance) or in the grace window.
bool vizShouldDisplay();

// Render one frame of the bar EQ (call at 60 Hz while forced mode active).
void displayVisualizer();

#endif // VISUALIZER_H
