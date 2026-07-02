# Firmware binaries for the web flasher

These files are produced automatically by `release.py` at the repo root
(`python release.py`), which builds the `matrix-s3-wroom` environment, merges
it into a "Full" image, and drops it here with the matching `VERSION`. You
normally don't add them by hand - the manual steps below are just for
reference.

The flasher reads the version string from `VERSION` (a single line, e.g.
`v2.0.0`) and expects the full-image binary alongside it:

    firmware/latest/
      VERSION                                  <- one line, e.g. v2.0.0
      AnimatedPixelClock-v2.0.0-Full.bin       <- ESP32-S3-WROOM, HUB75 128x64

Notes
-----
- The filename pattern is `AnimatedPixelClock-<VERSION>-Full.bin`, matching the
  `binId` in `flasher.js`.
- "Full" means a merged image (bootloader + partitions + app) flashed at
  offset 0x0. ESP Web Tools writes it to 0x0.
- To publish a new release, run `python release.py` after bumping
  `FIRMWARE_VERSION` in `src/config/config.h`. No flasher code change needed.

How to build a merged image manually (from the project, esptool):

    esptool.py --chip esp32s3 merge_bin -o AnimatedPixelClock-v2.0.0-Full.bin \
      0x0 bootloader.bin 0x8000 partitions.bin 0x10000 firmware.bin
