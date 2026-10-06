# AnimatedPixelClock v2.4.0

Game mode, more weather views and a safer web interface. Every board now has a clock-only image and a `-games` image with game mode. Settings are kept on an OTA update. To go back, upload the `OTA_ONLY` image of 2.3.2 on the Firmware Update page.

**Breaking changes**

- The weather API key setting is removed. The free Open-Meteo endpoint needs no key and its limit is far above the clock's one fetch every 10 minutes. A saved key is deleted on the next settings save; old backups that contain one still import.
- Endpoints that change or erase something refuse requests a browser sends from another site's page (see Security). Factory reset (`/reset`), pad forget (`/api/game/forget`), score reset (`/api/game/hiscores?reset=`) and setting the WiFi power (`/api/wifi/txpower?dbm=`) now take POST only; reading scores and the WiFi power stays GET. curl, Home Assistant, scripts and the companion are otherwise not affected.
- `/api/export` no longer sends `Access-Control-Allow-Origin: *`, so another site's page cannot read the configuration. Reading it from a script works as before.
- The burning room ambient effect is removed. A clock that had it selected shows Space Invaders instead.

**What's new**

- Game mode: play Falling Blocks, Snake, Bricks, Space Rocks, Runner, Defenders and Light Cycles on the panel with an Xbox Wireless Controller (model 1708 or newer, controller firmware 5.x) over Bluetooth LE. Start it from the **Game mode** page or `GET /api/game/start`. The page lists best scores with reset buttons and has rumble, idle timeout and Falling Blocks options; the game list shows the pad's battery. Best scores travel with the configuration export and import. While a game runs the panel drops to 5 bits per colour and the night schedule is paused. Game mode ends after 2 minutes without a pad, 60 seconds after the pad is lost, after the idle time, or from the page.
- Game mode ships as a **separate firmware**. Bluetooth keeps about 29KB of internal memory reserved once it is in the firmware, even when nobody plays, so the clock-only image stays the recommended one. The Maintenance page shows which variant a clock runs, and the update page asks before installing the other one. The web flasher lists every board in a "Clock only" and a "Clock + game mode" section.
- Weather clock forecast layouts: the next three days as rows or columns, the next 12 hours as a temperature curve over rain chance bars, or tomorrow in focus with a large animated icon.
- Richer weather icon animations, with night (moon) and wind variants; the small forecast icons are animated too.
- New colors: one Date + AM/PM color for the date, weekday and AM/PM marker in every style, and a Details row color for the Weather clock.
- Display panel options on the Maintenance page: driver chip, data clock, latch blanking, clock phase, colour depth and minimum refresh rate, with live test patterns. For panels from other batches that stay dark, flicker or show ghosting. They are part of the configuration export and import.
- WiFi transmit power setting on the Maintenance page (19.5 down to 8.5 dBm, also `POST /api/wifi/txpower?dbm=`). Some small boards, such as the ESP32-S3-Zero, connect and serve the portal much faster at 11-13 dBm.

**Security**

- Any page open in a browser on the home network could reset the clock, flash firmware or change settings without a click. Settings save, import, rename, panel options, notifications, animation upload, firmware update, factory reset, WiFi power, pad forget and score reset now take POST and refuse requests from another site's page. Animation delete still answers GET for the companion, and the display, brightness, style, mode and reboot controls stay open for home automation. Thanks to NickoScope for the report (#11).

**Fixes**

- A POST to the update or animation upload endpoint that was not a file upload crashed and restarted the clock, and a firmware update request without a file restarted it as if updated. Both now answer with an error.
- `/api/export` produced invalid JSON when a device name, metric label or NTP server contained a quote.
- The 12-hour time format and static IP selections were not shown after reloading the portal (#13).
- Dark flashes on panels whose refresh rate is close to the animation frame rate: a frame is no longer cleared before the panel has finished showing it.
- The brightness ramp test pattern lights every column.
- Game mode leaves with an error instead of crashing when Bluetooth cannot start.

**Install**

- New device: use the web flasher at https://pixelclock.stolaris.dev/ or flash `firmware-v2.4.0-<board>.bin` (or `-games`) at `0x0`. The flasher erases the whole board.
- Existing device: upload `OTA_ONLY_firmware-v2.4.0-<board>.bin`, or `OTA_ONLY_firmware-v2.4.0-<board>-games.bin` for game mode, on the clock's Firmware Update page. Do not upload a full image as an OTA update.
- Windows: `pc_stats_monitor_v4.exe` is the same companion as in 2.3.2.

**Boards:** `wroom` = ESP32-S3-WROOM-1 N16R8 (16 MB), `supermini` = ESP32-S3-Zero / Super Mini (4 MB), `waveshare` = Waveshare ESP32-S3-RGB-Matrix (32 MB). Each has a `-games` twin with game mode. Verify downloads against `SHA256SUMS.txt`.
