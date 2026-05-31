## Download Instructions

**Make sure you download the correct version for your screen size!**

| Use Case | File to Download |
|----------|------------------|
| **New device** (first time flashing) | `firmware-v1.5.5-OLED_0.96inch.bin` or `firmware-v1.5.5-OLED_1.3inch.bin` |
| **Existing device** (OTA update via web interface) | `OTA_ONLY_firmware-v1.5.5-OLED_0.96inch.bin` or `OTA_ONLY_firmware-v1.5.5-OLED_1.3inch.bin` |


# v1.5.5 - Changelog

## New Features

### HTTP Control API for Remote Automation
- Added lightweight HTTP endpoints so the device can be controlled remotely from home automation (Home Assistant, Node-RED) or a simple `curl` command — handy for turning the display off while away to help extend OLED lifetime.
- All endpoints are plain HTTP GET and return JSON:
  - `GET /api/display/off` / `GET /api/display/on` — turn the panel off / on
  - `GET /api/display/brightness?value=0-100` — set display brightness (percent)
  - `GET /api/mode/clock` / `GET /api/mode/auto` — force the clock, or resume automatic mode (PC stats when online, clock when offline)
  - `GET /api/clock/style?id=0-6` — switch the clock animation
  - `GET /api/status` — read the current display/mode state as JSON
  - `GET /api/reboot` — soft restart (settings preserved)
- These controls are runtime-only and reset to your configured behavior after a reboot, so frequent toggling from automations causes no flash wear.
- Scheduled night dimming now respects a display that has been turned off via HTTP, so the periodic brightness check no longer switches the panel back on.
- See the new **HTTP Control API** section in the README for usage examples and a Home Assistant snippet.
- Addresses #49.

## Internal Changes

- Release builds now select the OLED display type via a build flag (new PlatformIO environments `oled-096` and `oled-13`) instead of editing `DEFAULT_DISPLAY_TYPE` in `user_config.h` before each build.
- `create_firmware.py` now compiles the matching variant itself and can build both OLED variants in a single run.
- Related commits: `7182993`, `0338b5a`
