# AnimatedPixelClock v2.3.0

**What's new**

- Two new audio visualizer styles: Oscilloscope, which draws the live waveform on
  a lab-scope graticule, and Starfield Overdrive. The Oscilloscope needs the
  companion app from this release.
- The audio visualizer has its own page in the web interface, with the effect and
  its colors split into separate cards.
- Custom NTP servers on the Network page, so the clock can use a local time source
  instead of the public pool. A Test button checks that each one answers.
- Weather screen no longer gets stuck on "Fetching weather...".
- Fixes: the ambient screen and the corner clocks no longer blink out for single
  frames, and the PC counts as online only when its stats actually arrive.
- Companion app: two companions can no longer end up sharing one web UI port and
  saving into each other's settings, a config that will not parse is kept aside
  instead of being overwritten, and one Save button now applies the connection and
  the layout together.

**Install**

- New device: flash `firmware-v2.3.0-<board>.bin` at `0x0`, or use the web
  flasher at https://pixelclock.stolaris.dev/
- Existing device: upload `OTA_ONLY_firmware-v2.3.0-<board>.bin` on the clock's
  Firmware Update page. Do not upload a full image as an OTA update.
- Windows: download and run `pc_stats_monitor_v4.exe`, no Python needed.

**Boards:** `wroom` = ESP32-S3-WROOM-1 N16R8 (16 MB), `supermini` =
ESP32-S3-Zero / Super Mini (4 MB). Verify downloads against `SHA256SUMS.txt`.
