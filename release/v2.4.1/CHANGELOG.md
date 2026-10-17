# AnimatedPixelClock v2.4.1

A small update with fixes for custom rotation colors and the Doom Fire options, and a 12-hour format without AM/PM. Settings are kept on an OTA update. To go back, upload the `OTA_ONLY` image of 2.4.0 on the Firmware Update page.

**What's new**

- Time format gains "12-hour without AM/PM", which hides the indicator in every clock style. Saved as `hideAmPm` and part of the configuration export and import (#15).
- The wiring guide has a diagram for a level shifter (4x SN74AHCT125N) between an ESP32-S3-Zero and the panel, for panels that need 5V logic or show ghosting on 3.3V signals. The README and wiring guide also cover the NONDK P2.5 128x64 panel, which needs driver 0 and 5V logic (#16).

**Fixes**

- Custom rotation now draws each clock in that clock's own digit color. Before, colors set for a clock were ignored once it ran in the rotation. The separate rotation digit color picker is gone (#18).
- The Doom Fire flame options and colors stayed visible after switching to another clock style (#17).

**Install**

- New device: use the web flasher at https://pixelclock.stolaris.dev/ or flash `firmware-v2.4.1-<board>.bin` (or `-games`) at `0x0`. The flasher erases the whole board.
- Existing device: upload `OTA_ONLY_firmware-v2.4.1-<board>.bin`, or `OTA_ONLY_firmware-v2.4.1-<board>-games.bin` for game mode, on the clock's Firmware Update page. Do not upload a full image as an OTA update.
- Windows: `pc_stats_monitor_v4.exe` is the same companion as in 2.4.0.

**Boards:** `wroom` = ESP32-S3-WROOM-1 N16R8 (16 MB), `supermini` = ESP32-S3-Zero / Super Mini (4 MB), `waveshare` = Waveshare ESP32-S3-RGB-Matrix (32 MB). Each has a `-games` twin with game mode. Verify downloads against `SHA256SUMS.txt`.
