"""PlatformIO pre-build step: make NimBLE fail cleanly when Bluetooth cannot start.

NimBLE-Arduino 1.4.3 starts the BT controller through ESP_ERROR_CHECK, so a
controller that cannot get its memory aborts the whole firmware. Game mode
starts Bluetooth at runtime next to the panel's DMA buffers, where that can
happen. Patched, NimBLEDevice::init() undoes what it started and returns with
getInitialized() still false, and the gamepad task gives up instead.

The patch is idempotent. If the library code it expects has changed (a new
NimBLE version), the build stops so the patch gets looked at again.
"""

from pathlib import Path

Import("env")  # noqa: F821 - PlatformIO injects this

OLD = (
    "        ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));\n"
    "        ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));\n"
    "        ESP_ERROR_CHECK(esp_nimble_hci_init());\n"
    "#endif\n"
    "        nimble_port_init();\n"
)
NEW = (
    "        // tools/patch_nimble.py: undo and return uninitialized instead of aborting\n"
    "        errRc = esp_bt_controller_init(&bt_cfg);\n"
    "        if (errRc != ESP_OK) {\n"
    '            NIMBLE_LOGE(LOG_TAG, "esp_bt_controller_init() failed: %d", errRc);\n'
    "            return;\n"
    "        }\n"
    "        errRc = esp_bt_controller_enable(ESP_BT_MODE_BLE);\n"
    "        if (errRc != ESP_OK) {\n"
    '            NIMBLE_LOGE(LOG_TAG, "esp_bt_controller_enable() failed: %d", errRc);\n'
    "            esp_bt_controller_deinit();\n"
    "            return;\n"
    "        }\n"
    "        errRc = esp_nimble_hci_init();\n"
    "        if (errRc != ESP_OK) {\n"
    '            NIMBLE_LOGE(LOG_TAG, "esp_nimble_hci_init() failed: %d", errRc);\n'
    "            esp_bt_controller_disable();\n"
    "            esp_bt_controller_deinit();\n"
    "            return;\n"
    "        }\n"
    "        errRc = nimble_port_init();\n"
    "        if (errRc != ESP_OK) {\n"
    '            NIMBLE_LOGE(LOG_TAG, "nimble_port_init() failed: %d", errRc);\n'
    "            esp_nimble_hci_and_controller_deinit();\n"
    "            return;\n"
    "        }\n"
    "#else\n"
    "        nimble_port_init();\n"
    "#endif\n"
)

if not env.GetOption("clean"):  # noqa: F821
    libdeps = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")  # noqa: F821
    for src in libdeps.glob("*/src/NimBLEDevice.cpp"):
        text = src.read_text(encoding="utf-8")
        if NEW in text:
            continue
        if OLD not in text:
            print(f"error: {src} no longer matches tools/patch_nimble.py - review the patch")
            env.Exit(1)  # noqa: F821
        src.write_text(text.replace(OLD, NEW), encoding="utf-8", newline="")
        print(f"Patched {src.parent.parent.name}: Bluetooth start failure")
