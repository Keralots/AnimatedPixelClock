#!/usr/bin/env python3
"""
Create firmware binaries for both OTA update and full web flasher installation.

The display type is selected at BUILD TIME via a dedicated PlatformIO environment
(oled-096 / oled-13), so you no longer need to edit DEFAULT_DISPLAY_TYPE in
src/config/user_config.h before each release. This script builds the matching
variant for you and packages it.

Usage:
    python create_firmware.py v1.4.0        # Build BOTH OLED variants (0.96" + 1.3")
    python create_firmware.py v1.4.0 0      # Version v1.4.0, OLED 0.96inch only
    python create_firmware.py v1.4.0 1      # Version v1.4.0, OLED 1.3inch only
    python create_firmware.py v1.4.0 1 --no-build   # Package an already-built variant

Output files (in release/{version}/ folder):
    release/v1.4.0/firmware-v1.4.0-OLED_0.96inch.bin           (full flash)
    release/v1.4.0/OTA_ONLY_firmware-v1.4.0-OLED_0.96inch.bin  (OTA update)
"""

import argparse
import os
import shutil
import subprocess
import sys

RELEASES_DIR = 'release'

# Flash offsets (standard for ESP32-C3)
BOOTLOADER_OFFSET = 0x0
PARTITIONS_OFFSET = 0x8000
FIRMWARE_OFFSET = 0x10000

# OLED type -> human-readable name (used in output filenames)
OLED_TYPES = {
    '0': '0.96inch',
    '1': '1.3inch'
}

# OLED type -> PlatformIO environment that fixes DISPLAY_TYPE via build flag.
# Each env has its own build directory under .pio/build/, so the two variants
# never share object files and don't need a clean between builds.
OLED_ENVS = {
    '0': 'oled-096',
    '1': 'oled-13'
}


def find_platformio():
    """Locate the platformio executable (PATH first, then the standard penv)."""
    for name in ('platformio', 'pio'):
        found = shutil.which(name)
        if found:
            return found

    # Fall back to the default install location used by the PlatformIO IDE/CLI
    candidates = [
        os.path.expanduser('~/.platformio/penv/Scripts/platformio.exe'),  # Windows
        os.path.expanduser('~/.platformio/penv/bin/platformio'),          # Linux/macOS
    ]
    for candidate in candidates:
        if os.path.exists(candidate):
            return candidate

    return None


def build_dir_for(env):
    """Return the PlatformIO build output directory for an environment."""
    return os.path.join('.pio', 'build', env)


def binary_paths(env):
    """Return (bootloader, partitions, firmware) paths for an environment."""
    build_dir = build_dir_for(env)
    return (
        os.path.join(build_dir, 'bootloader.bin'),
        os.path.join(build_dir, 'partitions.bin'),
        os.path.join(build_dir, 'firmware.bin'),
    )


def build_variant(env):
    """Compile the firmware for the given PlatformIO environment."""
    pio = find_platformio()
    if not pio:
        print("Error: could not find the 'platformio' (or 'pio') executable.")
        print("Add it to PATH, or build manually with: platformio run -e " + env)
        return False

    print(f"\nBuilding firmware (env: {env}) ...")
    print(f"  Using: {pio}")
    result = subprocess.run([pio, 'run', '-e', env])
    if result.returncode != 0:
        print(f"Error: build failed for env '{env}' (exit code {result.returncode}).")
        return False
    return True


def get_output_filenames(version, oled_type):
    """Generate output filenames based on version and OLED type."""
    oled_name = OLED_TYPES.get(oled_type, '0.96inch')
    base_name = f"{version}-OLED_{oled_name}"

    # Create version-specific folder under release/
    output_dir = os.path.join(RELEASES_DIR, version)

    merged_name = f"firmware-{base_name}.bin"
    ota_name = f"OTA_ONLY_firmware-{base_name}.bin"

    return (
        output_dir,
        os.path.join(output_dir, merged_name),
        os.path.join(output_dir, ota_name)
    )


def create_ota_binary(firmware_path, output_path):
    """Create OTA-only firmware binary (just firmware.bin copy)."""

    if not os.path.exists(firmware_path):
        print(f"Error: {firmware_path} not found!")
        print("Please build the firmware first (omit --no-build).")
        return False

    firmware_size = os.path.getsize(firmware_path)

    print(f"\nCreating OTA firmware: {output_path}")
    print(f"  Source: {firmware_path}")
    print(f"  Size: {firmware_size} bytes ({firmware_size / 1024:.1f} KB)")

    with open(firmware_path, 'rb') as infile:
        with open(output_path, 'wb') as outfile:
            outfile.write(infile.read())

    return True


def create_merged_binary(bootloader_path, partitions_path, firmware_path, output_path):
    """Merge bootloader, partitions, and firmware into single binary."""

    # Check if all input files exist
    for filepath in [bootloader_path, partitions_path, firmware_path]:
        if not os.path.exists(filepath):
            print(f"Error: {filepath} not found!")
            print("Please build the firmware first (omit --no-build).")
            return False

    print(f"\nCreating merged firmware: {output_path}")
    print(f"  Bootloader: {bootloader_path} @ 0x{BOOTLOADER_OFFSET:X}")
    print(f"  Partitions: {partitions_path} @ 0x{PARTITIONS_OFFSET:X}")
    print(f"  Firmware:   {firmware_path} @ 0x{FIRMWARE_OFFSET:X}")

    with open(output_path, 'wb') as outfile:
        # Write bootloader at 0x0
        with open(bootloader_path, 'rb') as f:
            bootloader_data = f.read()
            outfile.write(bootloader_data)
            bootloader_size = len(bootloader_data)

        # Pad to partitions offset (0x8000)
        padding_size = PARTITIONS_OFFSET - bootloader_size
        outfile.write(b'\xFF' * padding_size)

        # Write partitions at 0x8000
        with open(partitions_path, 'rb') as f:
            partitions_data = f.read()
            outfile.write(partitions_data)
            partitions_size = len(partitions_data)

        # Pad to firmware offset (0x10000)
        current_pos = PARTITIONS_OFFSET + partitions_size
        padding_size = FIRMWARE_OFFSET - current_pos
        outfile.write(b'\xFF' * padding_size)

        # Write firmware at 0x10000
        with open(firmware_path, 'rb') as f:
            firmware_data = f.read()
            outfile.write(firmware_data)

    total_size = os.path.getsize(output_path)
    print(f"  Total size: {total_size} bytes ({total_size / 1024:.1f} KB)")

    return True


def process_variant(version, oled_type, output_dir, no_build):
    """Build (unless --no-build) and package a single OLED variant."""
    env = OLED_ENVS[oled_type]
    oled_name = OLED_TYPES[oled_type]

    print("\n" + "=" * 60)
    print(f"OLED {oled_name}  (env: {env})")
    print("=" * 60)

    if not no_build:
        if not build_variant(env):
            return False

    bootloader_path, partitions_path, firmware_path = binary_paths(env)
    _, merged_path, ota_path = get_output_filenames(version, oled_type)

    success_merged = create_merged_binary(
        bootloader_path, partitions_path, firmware_path, merged_path)
    success_ota = create_ota_binary(firmware_path, ota_path)

    if success_merged and success_ota:
        print(f"\n  Web Flasher (full):  {merged_path}")
        print(f"  OTA Update:          {ota_path}")
        return True

    return False


def main():
    parser = argparse.ArgumentParser(
        description='Create firmware binaries for OTA and web flasher',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
    python create_firmware.py v1.4.0      # Build BOTH variants (0.96" + 1.3")
    python create_firmware.py v1.4.0 0    # 0.96inch OLED only
    python create_firmware.py v1.4.1 1    # 1.3inch OLED only
        '''
    )
    parser.add_argument(
        'version',
        help='Firmware version (e.g., v1.4.0)'
    )
    parser.add_argument(
        'oled_type',
        nargs='?',
        choices=['0', '1'],
        default=None,
        help='OLED type: 0 = 0.96inch, 1 = 1.3inch. Omit to build BOTH.'
    )
    parser.add_argument(
        '--no-build',
        action='store_true',
        help='Skip compiling; package binaries already built in .pio/build/.'
    )

    args = parser.parse_args()

    # Determine which variant(s) to process
    oled_types = [args.oled_type] if args.oled_type else ['0', '1']

    # Create release directory if needed
    output_dir = os.path.join(RELEASES_DIR, args.version)
    os.makedirs(output_dir, exist_ok=True)

    print("=" * 60)
    print(f"Creating firmware binaries")
    print(f"  Version: {args.version}")
    print(f"  Variants: {', '.join(OLED_TYPES[t] for t in oled_types)}")
    print(f"  Build: {'skipped (--no-build)' if args.no_build else 'yes'}")
    print("=" * 60)

    all_ok = True
    for oled_type in oled_types:
        if not process_variant(args.version, oled_type, output_dir, args.no_build):
            all_ok = False

    print("\n" + "=" * 60)
    if all_ok:
        print("SUCCESS! Firmware files created in:", output_dir)
        print("=" * 60)
        print(f"\nWEB FLASHER USAGE:")
        print(f"  - Open: https://espressif.github.io/esptool-js/")
        print(f"  - Flash the firmware-*.bin file at offset 0x0")
        print(f"\nOTA UPDATE USAGE:")
        print(f"  - Open device web page")
        print(f"  - Upload the OTA_ONLY_firmware-*.bin in Firmware Update section")
        return 0
    else:
        print("ERROR: Failed to create one or more firmware files")
        print("=" * 60)
        return 1


if __name__ == '__main__':
    sys.exit(main())
