#!/usr/bin/env python3
"""
End-to-end release builder for the AnimatedPixelClock web flasher.

Runs the whole release pipeline for the browser flasher at docs/:
    1. Reads FIRMWARE_VERSION from src/config/config.h  ->  v<ver>
    2. Locates the PlatformIO CLI (PATH, then the standard penv install)
    3. Builds the matrix firmware (matrix-s3-wroom, the ESP32-S3-WROOM board)
    4. Merges bootloader + partitions + app into a single "Full" image
       (flashed at 0x0, what ESP Web Tools writes)
    5. Copies the Full.bin into docs/firmware/latest/ as
       AnimatedPixelClock-v<ver>-Full.bin and writes the VERSION file the
       flasher page reads
    6. Writes the GitHub Release images into release/v<ver>/:
         firmware-v<ver>.bin           (new device, full 0x0 image)
         OTA_ONLY_firmware-v<ver>.bin  (existing device, web UI update)

Usage:
    python release.py                 # build + package
    python release.py --skip-build    # package whatever .pio/build already has
    python release.py v2.1.0          # override the version string
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

# The single published variant: ESP32-S3-WROOM devkit driving the HUB75 matrix.
ENV = "matrix-s3-wroom"
BIN_ID = "AnimatedPixelClock"

# Flash offsets for the ESP32-S3 (bootloader starts at 0x0).
BOOTLOADER_OFFSET = 0x0
PARTITIONS_OFFSET = 0x8000
FIRMWARE_OFFSET = 0x10000

REPO_ROOT = Path(__file__).resolve().parent
CONFIG_H = REPO_ROOT / "src" / "config" / "config.h"
DOCS_LATEST = REPO_ROOT / "docs" / "firmware" / "latest"


def read_version() -> str:
    """Extract FIRMWARE_VERSION from src/config/config.h, normalised to v<ver>."""
    if not CONFIG_H.exists():
        sys.exit(f"error: {CONFIG_H} not found")
    pat = re.compile(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"')
    for line in CONFIG_H.read_text(encoding="utf-8").splitlines():
        m = pat.search(line)
        if m:
            ver = m.group(1).strip()
            return ver if ver.startswith("v") else "v" + ver
    sys.exit("error: FIRMWARE_VERSION not found in src/config/config.h")


def locate_pio() -> str:
    """Locate the PlatformIO CLI executable."""
    for name in ("pio", "pio.exe", "platformio", "platformio.exe"):
        found = shutil.which(name)
        if found:
            return found
    candidates = [
        Path.home() / ".platformio" / "penv" / "Scripts" / "pio.exe",
        Path.home() / ".platformio" / "penv" / "Scripts" / "platformio.exe",
        Path.home() / ".platformio" / "penv" / "bin" / "pio",
        Path.home() / ".platformio" / "penv" / "bin" / "platformio",
    ]
    for c in candidates:
        if c.exists():
            return str(c)
    sys.exit(
        "error: pio executable not found.\n"
        "  Tried PATH and the standard ~/.platformio/penv locations.\n"
        "  Install PlatformIO Core or add it to PATH."
    )


def run(cmd, cwd=REPO_ROOT):
    """Run a subprocess, exit on failure."""
    print(f"\n$ {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        sys.exit(f"error: command failed with exit code {result.returncode}")


def build_dir() -> Path:
    return REPO_ROOT / ".pio" / "build" / ENV


def merge_full_bin(out_path: Path):
    """Merge bootloader + partitions + firmware into a single 0x0 image."""
    bd = build_dir()
    bootloader = bd / "bootloader.bin"
    partitions = bd / "partitions.bin"
    firmware = bd / "firmware.bin"
    for p in (bootloader, partitions, firmware):
        if not p.exists():
            sys.exit(f"error: {p} not found - run a build first (omit --skip-build).")

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "wb") as out:
        bl = bootloader.read_bytes()
        out.write(bl)
        out.write(b"\xFF" * (PARTITIONS_OFFSET - len(bl)))

        pt = partitions.read_bytes()
        out.write(pt)
        out.write(b"\xFF" * (FIRMWARE_OFFSET - (PARTITIONS_OFFSET + len(pt))))

        out.write(firmware.read_bytes())

    size = out_path.stat().st_size
    print(f"  Full: {out_path.relative_to(REPO_ROOT)} ({size / 1024:.1f} KB)")


def copy_ota_bin(out_path: Path):
    """Copy firmware.bin verbatim as the OTA update image."""
    firmware = build_dir() / "firmware.bin"
    if not firmware.exists():
        sys.exit(f"error: {firmware} not found - run a build first (omit --skip-build).")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(firmware, out_path)
    size = out_path.stat().st_size
    print(f"  OTA:  {out_path.relative_to(REPO_ROOT)} ({size / 1024:.1f} KB)")


def write_version_file(version: str):
    DOCS_LATEST.mkdir(parents=True, exist_ok=True)
    (DOCS_LATEST / "VERSION").write_text(version + "\n", encoding="utf-8")


def find_old_full_bins(version: str):
    """List Full.bin files in docs/firmware/latest/ not for this version."""
    if not DOCS_LATEST.exists():
        return []
    pat = re.compile(rf"^{BIN_ID}-(v[^-]+)-Full\.bin$")
    old = []
    for f in DOCS_LATEST.iterdir():
        m = pat.match(f.name)
        if m and m.group(1) != version:
            old.append(f.name)
    return sorted(old)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("version", nargs="?", default=None,
                        help="Version override (default: read from config.h)")
    parser.add_argument("--skip-build", action="store_true",
                        help="Skip the PlatformIO build (assume .pio/build is current)")
    args = parser.parse_args()

    version = args.version or read_version()
    if not version.startswith("v"):
        version = "v" + version

    ota_dir = REPO_ROOT / "release" / version

    print(f"AnimatedPixelClock web-flasher release: {version}")
    print(f"Environment: {ENV}")

    if not args.skip_build:
        pio = locate_pio()
        print(f"PlatformIO: {pio}")
        run([pio, "run", "-e", ENV])
    else:
        print("Skipping build (--skip-build)")

    print("\n--- Web flasher image (docs/firmware/latest/) ---")
    full = DOCS_LATEST / f"{BIN_ID}-{version}-Full.bin"
    merge_full_bin(full)
    write_version_file(version)
    print(f"  VERSION  ({version})")

    print(f"\n--- GitHub Release images (release/{version}/) ---")
    full_out = ota_dir / f"firmware-{version}.bin"
    full_out.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(full, full_out)
    print(f"  Full: {full_out.relative_to(REPO_ROOT)} ({full_out.stat().st_size / 1024:.1f} KB)")
    copy_ota_bin(ota_dir / f"OTA_ONLY_firmware-{version}.bin")

    print("\n" + "=" * 60)
    print(f"Release {version} ready.")
    print("=" * 60)

    old = find_old_full_bins(version)
    if old:
        print("\nOlder Full.bin files still in docs/firmware/latest/ "
              "(remove with `git rm` when no longer needed):")
        for name in old:
            print(f"  {name}")

    print("\nNext steps:")
    print("  git add docs/firmware/latest/ release/")
    print(f'  git commit -m "release {version}"')
    print(f"  gh release create {version} release/{version}/*.bin --notes \"...\"")
    print("  git push")


if __name__ == "__main__":
    main()
