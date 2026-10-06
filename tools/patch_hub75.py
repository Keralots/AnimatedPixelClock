"""PlatformIO pre-build step: make the HUB75 driver fail cleanly when memory is short.

Driver 3.0.14 only logs when it cannot allocate the second DMA descriptor list,
then links descriptors into it anyway: a write through a null pointer. Game mode
restarts the panel at runtime with less memory free, so begin() has to return
false there and let setPanelColorDepth() fall back to a depth that fits.

The patch is idempotent. If the driver code it expects has changed (a new
driver version), the build stops so the patch gets looked at again.
"""

from pathlib import Path

Import("env")  # noqa: F821 - PlatformIO injects this

OLD = (
    '        ESP_LOGE("S3", "ERROR: Couldn\'t malloc _dmadesc_b. Not enough memory.");\n'
    "        _double_dma_buffer = false;\n"
)
NEW = (
    '        ESP_LOGE("S3", "ERROR: Couldn\'t malloc _dmadesc_b. Not enough memory.");\n'
    "        heap_caps_free(_dmadesc_a);  // tools/patch_hub75.py: fail instead of writing through null\n"
    "        _dmadesc_a = nullptr;\n"
    "        return false;\n"
)

if not env.GetOption("clean"):  # noqa: F821
    libdeps = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")  # noqa: F821
    for src in libdeps.glob("*/src/platforms/esp32s3/gdma_lcd_parallel16.cpp"):
        text = src.read_text(encoding="utf-8")
        if NEW in text:
            continue
        if OLD not in text:
            print(f"error: {src} no longer matches tools/patch_hub75.py - review the patch")
            env.Exit(1)  # noqa: F821
        src.write_text(text.replace(OLD, NEW), encoding="utf-8", newline="")
        print(f"Patched {src.parent.parent.parent.parent.name}: DMA descriptor allocation failure")
