# AnimatedPixelClock v2.3.2

**What's new**

- Optional WS2812B LED strip: a glow under or behind the panel on one spare GPIO,
  off until enabled. Effects: Solid, Wave, Rainbow, Fire, Meteor, Scanner, Hour
  sweep, Weather and Audio VU. It follows the panel's power and dimming schedule
  and has a current limit. Wiring, pin choice and settings are in the README
  under "LED strip (optional)".
- Matrix Rain now falls in real mirrored katakana, the way the film does, and the
  changing digit scrambles through the same glyphs.
- Matrix Rain options: "Smooth fall" lets the leading character slide down a pixel
  at a time instead of jumping a whole cell, and "Small clock" draws the time in
  the top-right corner so the rain fills the panel. Transparent digits are the new
  default; existing devices keep their setting.
- The web portal loads 3-5x faster and no longer makes the clock stutter while it
  loads.
- Diagnostics: the last crash is reported in `/api/info` and the diagnostics
  download, so a reboot no longer leaves no trace.

**Fixes**

- Saving settings no longer freezes the display for about a second.
- Settings export and import now include the Matrix Rain settings.
- The WiFi link watchdog no longer restarts WiFi when the board is only short of
  memory, which could take the clock off the network over and over.
- More free internal memory: TLS buffers use PSRAM on boards that have it, and the
  weather fetch releases its memory between fetches.

Thanks to @NickoScope for contributing.

**Install**

- New device: flash `firmware-v2.3.2-<board>.bin` at `0x0`, or use the web
  flasher at https://pixelclock.stolaris.dev/
- Existing device: upload `OTA_ONLY_firmware-v2.3.2-<board>.bin` on the clock's
  Firmware Update page. Do not upload a full image as an OTA update.
- Windows: download and run `pc_stats_monitor_v4.exe`, no Python needed.

**Boards:** `wroom` = ESP32-S3-WROOM-1 N16R8 (16 MB), `supermini` =
ESP32-S3-Zero / Super Mini (4 MB), `waveshare` = Waveshare ESP32-S3-RGB-Matrix
(32 MB). Verify downloads against `SHA256SUMS.txt`.
