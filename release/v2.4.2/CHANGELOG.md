# AnimatedPixelClock v2.4.2

A punchier audio visualizer, a new game, a newer panel driver and support for the Huidu HD-WF2 board. Settings are kept on an OTA update. To go back, upload the `OTA_ONLY` image of 2.4.1 on the Firmware Update page.

**What's new**

- Audio visualizer: a new "Punchy response" option, on by default. Equalizer bars and the LED strip meter jump to each hit and then fall, instead of the softer smoothed motion. Turn it off on the visualizer settings to get the old look.
- The Windows companion sends audio 50 times a second instead of 25, so the visualizer reacts faster and no longer misses short hits. Firmware 2.4.2 still works with the older companion.
- Game mode has an eighth game, Frog Crossing: get the frog over five road lanes and five river lanes into the home bays before the timer runs out (`-games` images).
- Huidu HD-WF2 LED sign controller support: four 64x32 panels on the 75EX1 output as one 128x64 canvas, chained left to right along the top row, then along the bottom row. The web flasher offers it as `wf2`, clock only, because the board has no PSRAM for game mode. Flashing erases Huidu's factory firmware, so back it up first if you may want it back. Thanks to @cracktiest for contributing and testing it.

**Fixes**

- Panel driver updated to ESP32-HUB75-MatrixPanel-DMA 3.1.0, with a correction for the refresh rate it reports, so the picture no longer tears. The default "Minimum refresh" in the panel settings is now 70 Hz, and a saved 60 Hz is read as 70.
- The weather fetch uses plain HTTP on boards without PSRAM. Over HTTPS it took about 49 KB of internal memory each time and could leave the web portal without memory. Boards with PSRAM keep HTTPS.
- After a short stall the visualizer shows the newest audio instead of catching up on old packets.

**Install**

- New device: use the web flasher at https://pixelclock.stolaris.dev/ or flash `firmware-v2.4.2-<board>.bin` (or `-games`) at `0x0`. The flasher erases the whole board.
- Existing device: upload `OTA_ONLY_firmware-v2.4.2-<board>.bin`, or `OTA_ONLY_firmware-v2.4.2-<board>-games.bin` for game mode, on the clock's Firmware Update page. Do not upload a full image as an OTA update.
- Windows: `pc_stats_monitor_v4.exe` is a new companion build with the 50 packets/s audio stream.

**Boards:** `wroom` = ESP32-S3-WROOM-1 N16R8 (16 MB), `supermini` = ESP32-S3-Zero / Super Mini (4 MB), `waveshare` = Waveshare ESP32-S3-RGB-Matrix (32 MB), `wf2` = Huidu HD-WF2 (8 MB). Each except `wf2` has a `-games` twin with game mode. Verify downloads against `SHA256SUMS.txt`.
