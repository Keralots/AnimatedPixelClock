## Download Instructions

**Make sure you download the correct version for your screen size!**

| Use Case | File to Download |
|----------|------------------|
| **New device** (first time flashing) | `firmware-v1.5.6-OLED_0.96inch.bin` or `firmware-v1.5.6-OLED_1.3inch.bin` |
| **Existing device** (OTA update via web interface) | `OTA_ONLY_firmware-v1.5.6-OLED_0.96inch.bin` or `OTA_ONLY_firmware-v1.5.6-OLED_1.3inch.bin` |


# v1.5.6 - Changelog

## New Features

### Snake Clock (new animation)
- A Nokia-style snake roams the screen chasing food and steering around both the clock digits and its own body.
- On each minute change the changed digits crumble into pellets; the snake hunts them down one by one before the new digit drops in.
- Optional **Arena Border** (Nokia-style frame) and optional date row, configurable in the web portal under Clock Settings.

### Tetris Clock (new animation)
- Block-grid numerals sit low on the screen, with a blinking block colon and the occasional tumbling tetromino during the idle gap.
- On each minute change the changed digits are rebuilt one at a time (left to right) in one of two styles: **Drop-in Slabs** (old digit bursts into fragments, new digit locks in as three slabs) or **Falling Dots** (the digit is assembled dot by dot from the bottom up).
- Block style (LCD grid or solid), animation style, dot order, and date position (top/bottom) are all configurable.

### Cycle All Styles mode
- New clock style that automatically rotates through every animation, switching to the next one every 5 minutes.
- Selectable like any other clock style in the web portal. Contributed via #52.

## Internal / Docs Changes

- Clarified that 2.42" SSD1309 panels use display type `0` (the SSD1306 driver) and that there is no separate display type `2`. Updated the comments in `user_config.h`, `display.h` and `main.cpp`. Addresses #53.
- New clock styles bring the HTTP control API range to `/api/clock/style?id=0-9` (`7` = Snake, `8` = Tetris, `9` = Cycle All Styles).

Related commits: `4031acb` (Snake + Tetris + Cycle All), `e09a9e5` (Snake pathfinding), `156d0c4` (#53 display-type docs).
