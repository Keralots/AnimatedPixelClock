"""PlatformIO pre-build step: make the HUB75 driver report its real refresh rate.

Driver 3.1.0 sweeps from each bitplane through to the MSB on every extra pass, but
its refresh calculation still counts one plane per pass. It also assumes the
requested bus speed, while the S3 LCD divider produces 10, 16 or 22.86MHz. That
figure picks lsbMsbTransitionBit and sets the scan wait in matrix_display.h, where
too short a wait tears the picture and too long a wait loses frames.

The patch is idempotent. If the driver code it expects has changed (a new driver
version), the build stops so the patch gets looked at again.
"""

from pathlib import Path

Import("env")  # noqa: F821 - PlatformIO injects this

PAIRS = [
    (
        "        int psPerClock = 1000000000000UL / static_cast<uint32_t>(m_cfg.i2sspeed);\n",
        "        // tools/patch_hub75.py: the clock Bus_Parallel16::init() really sets\n"
        "        const uint32_t requestedHz = static_cast<uint32_t>(m_cfg.i2sspeed);\n"
        "        const uint32_t busHz = requestedHz <= 10000000 ? 10000000\n"
        "                               : requestedHz < 20000000 ? 16000000 : 160000000 / 7;\n"
        "        int psPerClock = 1000000000000ULL / busHz;\n",
    ),
    (
        "            nsPerRow += (1 << (i - lsbMsbTransitionBit - 1)) * nsPerLatch;\n",
        "            // tools/patch_hub75.py: each pass sweeps plane i through to the MSB\n"
        "            nsPerRow += (1 << (i - lsbMsbTransitionBit - 1)) *\n"
        "                        (m_cfg.getPixelColorDepthBits() - i) * nsPerLatch;\n",
    ),
]

if not env.GetOption("clean"):  # noqa: F821
    libdeps = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")  # noqa: F821
    for src in libdeps.glob("*/src/ESP32-HUB75-MatrixPanel-I2S-DMA.cpp"):
        text = src.read_text(encoding="utf-8")
        changed = False
        for old, new in PAIRS:
            if new in text:
                continue
            if old not in text:
                print(f"error: {src} no longer matches tools/patch_hub75.py - review the patch")
                env.Exit(1)  # noqa: F821
            text = text.replace(old, new)
            changed = True
        if changed:
            src.write_text(text, encoding="utf-8", newline="")
            print(f"Patched {src.parent.parent.name}: real refresh rate")
