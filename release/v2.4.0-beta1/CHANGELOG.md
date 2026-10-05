# AnimatedPixelClock v2.4.0-beta1

**This is a beta.** It brings game mode, which still needs testing on more
controllers and boards. The clock itself runs the same code paths as 2.3.2
plus the changes listed below. Feedback and bug reports are welcome in the
issues. To go back, upload the `OTA_ONLY` image of 2.3.2 on the Firmware Update
page; settings are kept.

**What's new**

- Game mode: play Falling Blocks, Snake, Bricks, Space Rocks, Runner, Defenders
  and Light Cycles on the panel with an Xbox Wireless Controller (model 1708 or
  newer, controller firmware 5.x) over Bluetooth LE. Start it from the new
  **Game mode** page or `GET /api/game/start`. The page lists best scores with
  reset buttons and has rumble, idle timeout and Falling Blocks options; the
  game list shows the pad's battery. Best scores travel with the configuration
  export and import. While a game runs the panel drops to 5 bits per colour and
  the night schedule is paused. Game mode ends after 2 minutes without a pad,
  60 seconds after the pad is lost, after the idle time, or from the page.
- Game mode ships as a **separate firmware**: every board now has a clock-only
  image and a `-games` image. Bluetooth keeps about 29KB of internal memory
  reserved once it is in the firmware, even when nobody plays, so the
  clock-only image stays the recommended one. The Maintenance page shows which
  variant a clock runs, and the update page asks before installing the other
  one.
- Display panel options on the Maintenance page: driver chip, data clock, latch
  blanking, clock phase, colour depth and minimum refresh rate, with live test
  patterns. For panels from other batches that stay dark, flicker or show
  ghosting. They are part of the configuration export and import.
- WiFi transmit power setting on the Maintenance page (19.5 down to 8.5 dBm,
  also `GET /api/wifi/txpower?dbm=`). Some small boards, such as the
  ESP32-S3-Zero, connect and serve the portal much faster at 11-13 dBm.

**Changes**

- The burning room ambient effect is removed. A clock that had it
  selected shows Space Invaders instead.
- The web flasher always erases the whole board on install. Keeping data never
  worked: the full image overwrote the settings anyway.

**Fixes**

- Dark flashes on panels whose refresh rate is close to the animation frame
  rate: a frame is no longer cleared before the panel has finished showing it.
- The brightness ramp test pattern lights every column.

**Install**

- New device: flash `firmware-v2.4.0-beta1-<board>.bin` at `0x0`. The web
  flasher at https://pixelclock.stolaris.dev/ keeps offering the stable 2.3.2.
- Existing device: upload `OTA_ONLY_firmware-v2.4.0-beta1-<board>.bin`, or
  `OTA_ONLY_firmware-v2.4.0-beta1-<board>-games.bin` for game mode, on the
  clock's Firmware Update page. Do not upload a full image as an OTA update.
- Windows: `pc_stats_monitor_v4.exe` is the same companion as in 2.3.2.

**Boards:** `wroom` = ESP32-S3-WROOM-1 N16R8 (16 MB), `supermini` =
ESP32-S3-Zero / Super Mini (4 MB), `waveshare` = Waveshare ESP32-S3-RGB-Matrix
(32 MB). Each has a `-games` twin with game mode. Verify downloads against
`SHA256SUMS.txt`.
