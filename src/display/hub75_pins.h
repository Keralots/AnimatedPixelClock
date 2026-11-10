/*
 * AnimatedPixelClock - HUB75 GPIO map and panel geometry
 *
 * Defaults are the verified hand-wired map used by the WROOM devkit and the
 * compact 4MB boards. A board environment overrides them from build_flags by
 * defining HUB75_PINS_CUSTOM together with all 14 pins; a partial override is
 * rejected here rather than silently falling back to the wrong wiring.
 */

#ifndef HUB75_PINS_H
#define HUB75_PINS_H

// Module size and chain length. A board with another layout sets these from
// build_flags. HUB75_PANEL_ROWS above 1 stacks the chain into that many rows,
// each running left to right, so the wire is one long row but the canvas is not.
#ifndef HUB75_PANEL_W
#define HUB75_PANEL_W 64
#endif
#ifndef HUB75_PANEL_H
#define HUB75_PANEL_H 64
#endif
#ifndef HUB75_CHAIN
#define HUB75_CHAIN 2
#endif
#ifndef HUB75_PANEL_ROWS
#define HUB75_PANEL_ROWS 1
#endif
#if HUB75_CHAIN % HUB75_PANEL_ROWS
#error "HUB75_CHAIN must be a multiple of HUB75_PANEL_ROWS"
#endif

#ifdef HUB75_PINS_CUSTOM

#if !(defined(HUB75_PIN_R1) && defined(HUB75_PIN_G1) && defined(HUB75_PIN_B1) && \
      defined(HUB75_PIN_R2) && defined(HUB75_PIN_G2) && defined(HUB75_PIN_B2) && \
      defined(HUB75_PIN_A)  && defined(HUB75_PIN_B)  && defined(HUB75_PIN_C)  && \
      defined(HUB75_PIN_D)  && defined(HUB75_PIN_E)  && defined(HUB75_PIN_LAT) && \
      defined(HUB75_PIN_OE) && defined(HUB75_PIN_CLK))
#error "HUB75_PINS_CUSTOM requires all 14 HUB75_PIN_* macros"
#endif

#else

#if defined(HUB75_PIN_R1) || defined(HUB75_PIN_G1) || defined(HUB75_PIN_B1) || \
    defined(HUB75_PIN_R2) || defined(HUB75_PIN_G2) || defined(HUB75_PIN_B2) || \
    defined(HUB75_PIN_A)  || defined(HUB75_PIN_B)  || defined(HUB75_PIN_C)  || \
    defined(HUB75_PIN_D)  || defined(HUB75_PIN_E)  || defined(HUB75_PIN_LAT) || \
    defined(HUB75_PIN_OE) || defined(HUB75_PIN_CLK)
#error "HUB75 pin overrides require HUB75_PINS_CUSTOM and all 14 macros"
#endif

#define HUB75_PIN_R1  1
#define HUB75_PIN_G1  2
#define HUB75_PIN_B1  4
#define HUB75_PIN_R2  5
#define HUB75_PIN_G2  6
#define HUB75_PIN_B2  7
#define HUB75_PIN_A   8
#define HUB75_PIN_B   9
#define HUB75_PIN_C   10
#define HUB75_PIN_D   11
#define HUB75_PIN_E   12
#define HUB75_PIN_LAT 14
#define HUB75_PIN_OE  38
#define HUB75_PIN_CLK 13

#endif  // HUB75_PINS_CUSTOM

#endif  // HUB75_PINS_H
