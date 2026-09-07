# AnimatedPixelClock v2.2.0

**What's new**

- Two new clock styles: Bomberman and TRON
- Stability fixes

**Install**

- New device: flash `firmware-v2.2.0-<board>.bin` at `0x0`, or use the web
  flasher at https://pixelclock.stolaris.dev/
- Existing device: upload `OTA_ONLY_firmware-v2.2.0-<board>.bin` on the clock's
  Firmware Update page. Do not upload a full image as an OTA update.
- Windows: download and run `pc_stats_monitor_v4.exe`, no Python needed.

**Boards:** `wroom` = ESP32-S3-WROOM-1 N16R8 (16 MB), `supermini` =
ESP32-S3-Zero / Super Mini (4 MB). Verify downloads against `SHA256SUMS.txt`.
