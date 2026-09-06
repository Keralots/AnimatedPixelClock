#!/usr/bin/env python3
"""
End-to-end release builder for the AnimatedPixelClock web flasher.

Runs the whole release pipeline for the browser flasher at docs/:
    1. Reads FIRMWARE_VERSION from src/config/config.h  ->  v<ver>
    2. Locates the PlatformIO CLI (PATH, then the standard penv install)
    3. Builds both board variants in a single PlatformIO invocation
       (matrix-s3-wroom = WROOM 16MB, matrix-s3 = S3-Zero / Super Mini 4MB)
    4. Merges bootloader + partitions + OTA initialization + app into a "Full" image per
       variant (flashed at 0x0, what ESP Web Tools writes)
    5. Copies the Full.bin images into docs/firmware/latest/ as
       AnimatedPixelClock-<id>-v<ver>-Full.bin and writes the VERSION file the
       flasher page reads
    6. Writes the GitHub Release images into release/v<ver>/:
         firmware-v<ver>-<id>.bin           (new device, full 0x0 image)
         OTA_ONLY_firmware-v<ver>-<id>.bin  (existing device, web UI update)
    7. Copies the prebuilt Windows companion EXE and writes SHA256SUMS.txt
       for the release assets and web-flasher images. Does not publish to GitHub.

The web flasher reads the firmware id from the BOARDS map in docs/flasher.js:
each board's `firmware` field must match an id below.

Usage:
    python release.py                 # build + package both variants
    python release.py --skip-build    # package whatever .pio/build already has
    python release.py v2.1.0          # require this version to match config.h
"""

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import struct
from pathlib import Path

# Variants published by the web flasher.
# (PlatformIO env, firmware id, label). The firmware id must match the
# `firmware` field in docs/flasher.js and drives the release/ filenames.
VARIANTS = [
    ("matrix-s3-wroom", "wroom",     "ESP32-S3-WROOM devkit (16MB)"),
    ("matrix-s3",       "supermini", "ESP32-S3-Zero / Super Mini (4MB)"),
]
FLASH_BYTES = {"matrix-s3-wroom": 16 * 1024 * 1024, "matrix-s3": 4 * 1024 * 1024}

# Flash offsets for the ESP32-S3 (bootloader starts at 0x0).
BOOTLOADER_OFFSET = 0x0
PARTITIONS_OFFSET = 0x8000
OTA_DATA_OFFSET = 0xE000
FIRMWARE_OFFSET = 0x10000

REPO_ROOT = Path(__file__).resolve().parent
CONFIG_H = REPO_ROOT / "src" / "config" / "config.h"
DOCS_LATEST = REPO_ROOT / "docs" / "firmware" / "latest"
COMPANION_EXE = REPO_ROOT / "PC-Companion-App-v4" / "win-companion" / "dist" / "pc_stats_monitor_v4.exe"


def normalize_version(version: str) -> str:
    version = version.removeprefix("v")
    if not re.fullmatch(r"\d+\.\d+\.\d+(?:-[A-Za-z0-9.]+)?", version):
        raise ValueError("Version must be MAJOR.MINOR.PATCH, optionally with a prerelease suffix")
    return "v" + version


def read_version() -> str:
    """Extract FIRMWARE_VERSION from src/config/config.h, normalised to v<ver>."""
    if not CONFIG_H.exists():
        sys.exit(f"error: {CONFIG_H} not found")
    pat = re.compile(r'#define\s+FIRMWARE_VERSION\s+"([^"]+)"')
    for line in CONFIG_H.read_text(encoding="utf-8").splitlines():
        m = pat.search(line)
        if m:
            ver = m.group(1).strip()
            return normalize_version(ver)
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


def build_envs(pio_path: str):
    """Build every variant env in a single PlatformIO invocation."""
    cmd = [pio_path, "run"]
    for env, *_ in VARIANTS:
        cmd.extend(["-e", env])
    run(cmd)


def build_dir(env: str) -> Path:
    return REPO_ROOT / ".pio" / "build" / env


def refresh_build_metadata(pio_path: str):
    """Ask the selected platform for its actual flash parts and offsets."""
    cmd = [pio_path, "run", "-t", "idedata"]
    for env, *_ in VARIANTS:
        cmd.extend(["-e", env])
    result = subprocess.run(cmd, cwd=REPO_ROOT, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    if result.returncode:
        sys.exit("error: could not read PlatformIO flash metadata\n" + result.stdout + result.stderr)


def merge_segments(segments):
    """Merge at offset zero, rejecting empty/overlapping parts before writing."""
    image = bytearray()
    for offset, data in sorted(segments, key=lambda part: part[0]):
        if not data or offset < len(image):
            raise ValueError(f"Empty or overlapping flash part at {offset:#x}")
        image.extend(b"\xff" * (offset - len(image)))
        image.extend(data)
    return bytes(image)


def validate_partitions(table: bytes, firmware_size: int, flash_size: int):
    """Check both OTA slots fit the image and the board's physical flash."""
    apps, ota_data = {}, False
    for pos in range(0, len(table) - 31, 32):
        magic, kind, subtype, offset, size, _, _ = struct.unpack_from("<HBBII16sI", table, pos)
        if magic != 0x50AA:
            break  # MD5 record / end of partition table.
        if offset + size > flash_size:
            raise ValueError("Partition exceeds the selected board's flash size")
        if kind == 0:
            apps[subtype] = (offset, size)
        if kind == 1 and subtype == 0:
            ota_data = offset == OTA_DATA_OFFSET and size == 0x2000
    if not ota_data or 0x10 not in apps or 0x11 not in apps:
        raise ValueError("Expected OTA data and two application partitions")
    if apps[0x10][0] != FIRMWARE_OFFSET or any(firmware_size > size for _, size in apps.values()):
        raise ValueError("Firmware does not fit both OTA slots at the expected offsets")


def prepare_full_bin(env: str, version: str) -> bytes:
    bd = build_dir(env)
    metadata = json.loads((bd / "idedata.json").read_text(encoding="utf-8"))["extra"]
    app_offset = int(metadata["application_offset"], 0)
    segments = [(int(part["offset"], 0), Path(part["path"]).read_bytes())
                for part in metadata["flash_images"]]
    firmware = (bd / "firmware.bin").read_bytes()
    segments.append((app_offset, firmware))
    parts = dict(segments)
    if set(parts) != {BOOTLOADER_OFFSET, PARTITIONS_OFFSET, OTA_DATA_OFFSET, FIRMWARE_OFFSET}:
        raise ValueError(f"Unexpected flash layout for {env}")
    for data in (parts[BOOTLOADER_OFFSET], firmware):
        if len(data) < 24 or data[0] != 0xE9 or struct.unpack_from("<H", data, 12)[0] != 9:
            raise ValueError(f"Not an ESP32-S3 image: {env}")
    boot_flash_size = 1024 * 1024 << (parts[BOOTLOADER_OFFSET][3] >> 4)
    if boot_flash_size != FLASH_BYTES[env]:
        raise ValueError(f"Bootloader flash size does not match {env}")
    if len(parts[OTA_DATA_OFFSET]) != 0x2000:
        raise ValueError("Unexpected OTA initialization image size")
    if b"\0" + version.removeprefix("v").encode() + b"\0" not in firmware:
        raise ValueError(f"Firmware version missing from {env}; rebuild before packaging")
    validate_partitions(parts[PARTITIONS_OFFSET], len(firmware), FLASH_BYTES[env])
    return merge_segments(segments)


def write_full_bin(image: bytes, out_path: Path):
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(image)

    size = out_path.stat().st_size
    print(f"  Full: {out_path.relative_to(REPO_ROOT)} ({size / 1024:.1f} KB)")


def copy_ota_bin(env: str, out_path: Path):
    """Copy firmware.bin verbatim as the OTA update image."""
    firmware = build_dir(env) / "firmware.bin"
    if not firmware.exists():
        sys.exit(f"error: {firmware} not found - run a build first (omit --skip-build).")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(firmware, out_path)
    size = out_path.stat().st_size
    print(f"  OTA:  {out_path.relative_to(REPO_ROOT)} ({size / 1024:.1f} KB)")


def write_version_file(version: str):
    DOCS_LATEST.mkdir(parents=True, exist_ok=True)
    (DOCS_LATEST / "VERSION").write_text(version + "\n", encoding="utf-8", newline="\n")


def write_checksums(directory: Path, names):
    lines = [f"{hashlib.sha256((directory / name).read_bytes()).hexdigest()}  {name}"
             for name in sorted(names)]
    # LF only: sha256sum -c cannot open a filename that carries a trailing CR.
    (directory / "SHA256SUMS.txt").write_text("\n".join(lines) + "\n", encoding="utf-8",
                                              newline="\n")


def find_old_full_bins(version: str):
    """List Full.bin files in docs/firmware/latest/ not for this version."""
    if not DOCS_LATEST.exists():
        return []
    pat = re.compile(r"^AnimatedPixelClock-(.+)-(v[^-]+)-Full\.bin$")
    old = []
    for f in DOCS_LATEST.iterdir():
        m = pat.match(f.name)
        if m and m.group(2) != version:
            old.append(f.name)
    return sorted(old)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("version", nargs="?", default=None,
                        help="Expected version, must match config.h")
    parser.add_argument("--skip-build", action="store_true",
                        help="Skip the PlatformIO build (assume .pio/build is current)")
    parser.add_argument("--companion", type=Path, default=COMPANION_EXE,
                        help="Prebuilt Windows companion to include (not rebuilt by this script)")
    args = parser.parse_args()

    version = read_version()
    if args.version and normalize_version(args.version) != version:
        parser.error("Requested version differs from FIRMWARE_VERSION; update config.h and rebuild")
    if not args.companion.is_file() or args.companion.read_bytes()[:2] != b"MZ":
        parser.error("Build the Windows companion first, or supply --companion with a valid EXE")

    ota_dir = REPO_ROOT / "release" / version

    print(f"AnimatedPixelClock web-flasher release: {version}")
    print("Variants: " + ", ".join(f"{env} -> {fid}" for env, fid, *_ in VARIANTS))

    pio = locate_pio()
    print(f"PlatformIO: {pio}")
    if not args.skip_build:
        build_envs(pio)
    else:
        print("Skipping build (--skip-build)")

    refresh_build_metadata(pio)
    # Validate every variant before changing any published file.
    images = {env: prepare_full_bin(env, version) for env, *_ in VARIANTS}

    print("\n--- Web flasher images (docs/firmware/latest/) ---")
    for env, fid, _label in VARIANTS:
        out = DOCS_LATEST / f"AnimatedPixelClock-{fid}-{version}-Full.bin"
        write_full_bin(images[env], out)

    print(f"\n--- GitHub Release images (release/{version}/) ---")
    for env, fid, _label in VARIANTS:
        # Full 0x0 image for new devices - same bytes as the docs flasher image.
        full_src = DOCS_LATEST / f"AnimatedPixelClock-{fid}-{version}-Full.bin"
        full_out = ota_dir / f"firmware-{version}-{fid}.bin"
        full_out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(full_src, full_out)
        print(f"  Full: {full_out.relative_to(REPO_ROOT)} ({full_out.stat().st_size / 1024:.1f} KB)")
        # OTA-only image for existing devices (web UI update).
        copy_ota_bin(env, ota_dir / f"OTA_ONLY_firmware-{version}-{fid}.bin")

    shutil.copy2(args.companion, ota_dir / COMPANION_EXE.name)
    release_names = [name for _, fid, _ in VARIANTS
                     for name in (f"firmware-{version}-{fid}.bin", f"OTA_ONLY_firmware-{version}-{fid}.bin")]
    write_checksums(ota_dir, release_names + [COMPANION_EXE.name])
    write_checksums(DOCS_LATEST, [f"AnimatedPixelClock-{fid}-{version}-Full.bin" for _, fid, _ in VARIANTS])
    write_version_file(version)
    print(f"  Companion: {COMPANION_EXE.name}; SHA256SUMS.txt; VERSION ({version})")

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
    print(f'  git commit -m "release: publish {version}"')
    print("  git push origin main")
    print(f"  Create tag {version} at that commit, then publish a GitHub Release with")
    print(f"  the four BINs, {COMPANION_EXE.name} and SHA256SUMS.txt from {ota_dir}.")


if __name__ == "__main__":
    main()
