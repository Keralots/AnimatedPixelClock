/*
 * AnimatedPixelClock - Sprite color slots
 *
 * Named color slots for the user-editable sprite colors (HUB75 build only).
 * Kept dependency-free (no display.h) so config.h can include it to size the
 * Settings.spriteColors[] array. The SPRITE_COLOR() accessor macro lives in
 * display.h (it needs DISPLAY_WHITE for the OLED fallback).
 *
 * APPEND-ONLY: add new slots immediately before COL_COUNT. NEVER insert or
 * reorder existing slots - the values are persisted to NVS indexed by enum
 * position, so reordering silently remaps a user's saved colors.
 */

#ifndef COLOR_SLOTS_H
#define COLOR_SLOTS_H

#include <Arduino.h>

enum ColorSlot {
  COL_DIGITS = 0,        // legacy single digit color (superseded by COL_DIGITS_S*,
                         //   kept only to seed the per-style slots on upgrade)
  COL_MARIO_HAT,
  COL_MARIO_OVERALLS,
  COL_MARIO_SKIN,
  COL_MARIO_SHOES,
  COL_PACMAN,
  COL_PELLET,
  COL_SNAKE,
  COL_INVADER,
  COL_LASER,
  COL_SNAKE_FOOD,
  // Tetris idle-game pieces. MUST stay contiguous and in this exact order
  // (I,O,T,S,Z,J,L) - clock_tetris.cpp indexes them as COL_TET_I + pieceIndex,
  // where pieceIndex is random(7) per TET_PIECE_ROT. A static_assert guards it.
  COL_TET_I,
  COL_TET_O,
  COL_TET_T,
  COL_TET_S,
  COL_TET_Z,
  COL_TET_J,
  COL_TET_L,
  // Dino Runner (style 11)
  COL_DINO,
  COL_DINO_CACTUS,
  COL_DINO_PTERO,
  COL_DINO_GROUND,
  COL_DINO_CLOUD,
  // Asteroids (style 10)
  COL_AST_SHIP,
  COL_AST_ROCK,
  // Pong / Arkanoid (style 5)
  COL_PONG_BALL,
  COL_PONG_PADDLE,
  // Mario idle-encounter sprites (style 0)
  COL_GOOMBA,
  COL_SPINY,
  COL_KOOPA,
  COL_COIN,
  COL_STAR,
  COL_MUSHROOM,
  COL_FIREBALL,
  // PC-monitor stats screen (not a clock style)
  COL_STAT_TEXT,     // metric lines, clock timestamp, config hints
  COL_STAT_BAR,      // progress-bar fill
  COL_STAT_BAR_BG,   // progress-bar outline
  // Matrix Rain (style 12)
  COL_MATRIX_RAIN,   // rain trail base color (fade levels derived from it)
  COL_MATRIX_HEAD,   // column head glyph + digit decode flicker
  // Missile Command (style 13)
  COL_MC_MISSILE,    // enemy missile trails + heads
  COL_MC_COUNTER,    // counter-missile trails + cannon barrel
  COL_MC_EXPLOSION,  // explosion rings (white flicker partner is hardcoded)
  COL_MC_CITY,       // city silhouettes
  COL_MC_GROUND,     // ground strip + cannon mound
  // Per-clock time-digit + colon colors, one per clock style (0..14). MUST stay
  // contiguous and in ascending style order - digitColor() indexes them as
  // COL_DIGITS_S0 + settings.clockStyle. A static_assert guards the span.
  // (S14 was appended later; it stays contiguous because these were the last
  // slots. If a future style needs a digit slot that CANNOT be contiguous,
  // special-case it in digitColor() instead of inserting here.)
  COL_DIGITS_S0,  COL_DIGITS_S1,  COL_DIGITS_S2,  COL_DIGITS_S3,
  COL_DIGITS_S4,  COL_DIGITS_S5,  COL_DIGITS_S6,  COL_DIGITS_S7,
  COL_DIGITS_S8,  COL_DIGITS_S9,  COL_DIGITS_S10, COL_DIGITS_S11,
  COL_DIGITS_S12, COL_DIGITS_S13, COL_DIGITS_S14,
  // Weather clock (style 14)
  COL_WEATHER_ICON,    // primary icon body (sun disc, cloud)
  COL_WEATHER_ACCENT,  // secondary effects (rain, snow, lightning, fog)
  COL_WEATHER_TEMP,    // big temperature readout
  // ...append future slots here (before COL_COUNT)
  COL_COUNT
};

// Default palette (RGB565), indexed by ColorSlot. Real definition in settings.cpp.
extern const uint16_t SPRITE_COLOR_DEFAULTS[COL_COUNT];

#endif  // COLOR_SLOTS_H
