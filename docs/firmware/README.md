# Firmware and companion releases

Run `python release.py` from the repository root. It reads `FIRMWARE_VERSION`
from `src/config/config.h`, builds both targets, validates the flash layout,
and packages the firmware with the prebuilt Windows companion. It does not
commit, push, or publish a GitHub Release.

| File ID | PlatformIO environment | Hardware |
| --- | --- | --- |
| `supermini` | `matrix-s3` | ESP32-S3-Zero / Super Mini, 4 MB, native USB |
| `wroom` | `matrix-s3-wroom` | ESP32-S3-WROOM-1 N16R8, 16 MB, USB-UART |

The `supermini` filename is retained for compatibility; it also covers the
Waveshare ESP32-S3-Zero. Choose the image for your board and flash size.

## Packaging

1. Set the intended firmware version in `src/config/config.h`.
2. Build and test the Windows companion using
   `PC-Companion-App-v4/win-companion/build_exe.bat`.
3. Run `python release.py`. The script includes the EXE from
   `PC-Companion-App-v4/win-companion/dist/pc_stats_monitor_v4.exe`.
   Use `--companion PATH` to package another tested build.
4. Review the files and `SHA256SUMS.txt` in `release/v<version>/`.

`--skip-build` reuses existing firmware and still validates it and refreshes
PlatformIO flash metadata. Use it only when those binaries match the source.
An optional version argument must match `FIRMWARE_VERSION`; it cannot silently
relabel an older firmware build.

Full images contain the bootloader at `0x0`, partition table at `0x8000`, OTA
initialization (`boot_app0.bin`) at `0xe000`, and application at `0x10000`.
Offsets and files come from PlatformIO's generated metadata. Packaging rejects
overlapping parts, mismatched chip/flash sizes and images too large for OTA slots.

## Outputs

The web flasher reads `docs/firmware/latest/VERSION` and selects one of:

```text
AnimatedPixelClock-supermini-v<version>-Full.bin
AnimatedPixelClock-wroom-v<version>-Full.bin
SHA256SUMS.txt
```

These files are committed under `docs/` and published by GitHub Pages from
`main:/docs`. The filename IDs match `BOARDS` in `docs/flasher.js`.

The GitHub Release assets are prepared in `release/v<version>/`:

```text
firmware-v<version>-supermini.bin
firmware-v<version>-wroom.bin
OTA_ONLY_firmware-v<version>-supermini.bin
OTA_ONLY_firmware-v<version>-wroom.bin
pc_stats_monitor_v4.exe
SHA256SUMS.txt
```

- **New device / USB installation:** flash the full `firmware-*.bin` at `0x0`,
  or use the web flasher. Erasing removes saved settings and animations.
- **Existing device / WiFi update:** upload the matching `OTA_ONLY_*.bin` through
  the clock's Firmware Update page. Do not upload a full image as an OTA update.
- **Windows:** download and run the EXE. No Python installation is needed.

The release-directory EXE copy is gitignored to avoid storing it twice; upload
it as a release asset. The original EXE remains tracked in `win-companion/dist`.

## Publishing

Commit the source, flasher updates, `docs/firmware/latest` and release BINs/checksums.
Push `main`, tag that exact commit as `v<version>`, and create a GitHub Release
with all six assets listed above. Use a draft until every upload is complete,
then publish it and mark stable releases as latest.

The flasher's Windows download points to that same tag's `pc_stats_monitor_v4.exe`.
Keep that asset name stable. Check the public page, both BIN downloads, the EXE
download and their SHA-256 hashes after GitHub Pages finishes deploying.
