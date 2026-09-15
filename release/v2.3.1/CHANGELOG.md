# AnimatedPixelClock v2.3.1

**What's new**

- New board: the Waveshare ESP32-S3-RGB-Matrix driver board is now supported and
  can be flashed from the web flasher. It carries the HUB75 header and the output
  buffers, so there is no per-GPIO wiring; the ribbon cables and panel power still
  get connected. Its 32 MB flash leaves 23 MB for custom animations.
- New clock style: Doom Fire. The PSX Doom fire effect, with the digits as
  heat sources burning white-hot over a fire line; a changed digit burns away
  before the new one re-ignites.
- The README now has a gallery of every animated style rebuilding all four digits
  at the hour rollover.

**Install**

- New device: flash `firmware-v2.3.1-<board>.bin` at `0x0`, or use the web
  flasher at https://pixelclock.stolaris.dev/
- Existing device: upload `OTA_ONLY_firmware-v2.3.1-<board>.bin` on the clock's
  Firmware Update page. Do not upload a full image as an OTA update.
- Windows: download and run `pc_stats_monitor_v4.exe`, no Python needed.

**Boards:** `wroom` = ESP32-S3-WROOM-1 N16R8 (16 MB), `supermini` =
ESP32-S3-Zero / Super Mini (4 MB), `waveshare` = Waveshare ESP32-S3-RGB-Matrix
(32 MB). Verify downloads against `SHA256SUMS.txt`.
