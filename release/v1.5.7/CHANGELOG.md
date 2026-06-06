## Download Instructions

**Make sure you download the correct version for your screen size!**

| Use Case | File to Download |
|----------|------------------|
| **New device** (first time flashing) | `firmware-v1.5.7-OLED_0.96inch.bin`, `firmware-v1.5.7-OLED_1.3inch.bin` or `firmware-v1.5.7-OLED_1.54inch.bin` |
| **Existing device** (OTA update via web interface) | `OTA_ONLY_firmware-v1.5.7-OLED_0.96inch.bin`, `OTA_ONLY_firmware-v1.5.7-OLED_1.3inch.bin` or `OTA_ONLY_firmware-v1.5.7-OLED_1.54inch.bin` |

> 0.96" SSD1306 and 2.42" SSD1309 share the `0.96inch` image. 1.3" SH1106 and 1.54" CH1116 each have their own image - do not mix them up.

You can also flash directly from your browser (no tools to install): https://keralots.github.io/SmallOLED-PCMonitor/


# v1.5.7 - Changelog

## New Features

### 1.54" CH1116 display support (new display type)
- Added support for 1.54" 128x64 OLED panels built on the CH1116 controller, selectable as display type `2`.
- The CH1116 is SH1106-compatible but maps its visible area directly to GDDRAM columns 0..127 (no column offset), unlike the SH1106's 2-column offset. Flashing the SH1106 image onto a CH1116 left a stale column on the left edge and clipped the right border (most visible as a missing right side on the Snake clock's arena frame). The new driver corrects the offset so the panel renders edge to edge.
- Use it by setting `DEFAULT_DISPLAY_TYPE 2` in `user_config.h`, or flash the dedicated `1.54inch` build. New PlatformIO build environment: `oled-154`.
- Example panel: https://aliexpress.com/item/1005006579037427.html

### Browser web flasher with in-browser WiFi setup
- New GitHub Pages flasher lets you install or update the firmware straight from Chrome or Edge over USB - no PlatformIO or esptool needed. Pick your OLED variant and click Install.
- On first boot the device offers Improv-Serial provisioning, so the flasher can push your home WiFi credentials right after flashing. The WiFiManager AP portal stays available as a fallback.
- Includes a built-in serial monitor so you can watch the device boot from the same page.

### Tetris clock - Smooth Play mode
- New option for the Tetris Block Game clock that smooths out the piece animation for a more fluid look.

### Clock setting descriptions show defaults
- The animation setting descriptions in the web portal now show each option's default value, so it is easier to get back to stock behaviour.

## Bug Fixes

- **Timezone / DST accuracy (#54):** corrected several timezone database offsets and DST transition rules.
- **LED night light (#55):** the brightness slider is now hidden/guarded when `LED_PWM_ENABLED` is 0, so builds without the LED no longer show a dead control.
- **Clock stuck at 00:00:** fixed the clock freezing at 00:00 when the first NTP sync landed during a minute-change window.
- **Snake clock freeze:** fixed the Snake clock freezing on the pellet frame during a digit change.

## Internal / Docs Changes

- The web config page is now streamed from a PROGMEM template (`src/web/web_pages.h`), reducing RAM pressure on the ESP32-C3.
- Web flasher moved from `/flasher/` to the GitHub Pages site root.
- The CH1116 driver lives in `src/display/ch1116.h` as a thin subclass of the Adafruit SH1106 driver (`Adafruit_CH1116`); it reuses the SH1106 init/draw path and only overrides the column offset (`CH1116_COL_OFFSET`, default 0). No Adafruit library edits required.
- Display type `2` is now CH1116. This supersedes the v1.5.6 note that there was "no display type 2" - that note was about 2.42" SSD1309, which still uses type `0` (the SSD1306 driver).
- Release tooling (`create_firmware.py`, `release.py`) and the web flasher now build and offer all three firmware images: SSD1306 (0.96" / 2.42"), SH1106 (1.3") and CH1116 (1.54").
- Updated `user_config.h`, `display.h`, `main.cpp`, `README.md` and `STRUCTURE_How_to_modify.md` to document the new display type.

Related commits: `84cba3e` (web flasher + Improv), `5ee5ba7` (#54 timezone/DST), `9f29e91` (#55 LED slider guard), `4f98f0c` (PROGMEM web page), `583a803` (Tetris Smooth Play), `af91ac3` (flasher to site root), `dabe347` (setting default labels), `8e80f93` (NTP 00:00 fix), `debcaa9` (Snake freeze fix).
