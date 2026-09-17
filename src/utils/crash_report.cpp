/*
 * AnimatedPixelClock - Crash report
 *
 * The precompiled arduino-esp32 2.0.17 libraries for the ESP32-S3 are built
 * with CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH and the ELF format, and all three
 * partition tables have a coredump partition. So on a panic, an abort() or a
 * watchdog timeout the SDK already writes a core dump to flash; nothing read
 * it back.
 *
 * At the next boot crashReportBegin() reads it once. esp_core_dump_image_check()
 * verifies the checksum first, because esp_core_dump_get_summary() parses the
 * ELF without checking it. The summary - task, exception cause, PC, address,
 * backtrace and the ELF SHA-256 of the image that crashed - is kept in NVS, and
 * the dump is erased so the same crash is not reported again on the boot after.
 * The record stays until the next crash replaces it.
 *
 * abort(), a failed assert and the task watchdog all end in panic_abort(), which
 * writes to address 0 on purpose, so the CPU reports StoreProhibited at 0. The
 * report names those "abort()" and "Task watchdog" instead. The task watchdog
 * aborts from its interrupt, so for it "task" and "backtrace" belong to whatever
 * the interrupt stopped, not to the task that hung.
 *
 * To turn the addresses into source lines, take the firmware.elf whose SHA-256
 * starts with "elfSha256" (shasum -a 256 .pio/build/<env>/firmware.elf):
 *   xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf <pc> <backtrace...>
 */

#include "crash_report.h"
#include "../config/config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_core_dump.h>
#include <esp_ota_ops.h>
#include <esp_private/panic_internal.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <time.h>
#include <xtensa/config/core.h>
#include <xtensa/corebits.h>

// Kept in NVS as one blob. Change CRASH_MAGIC when the layout changes, so an
// old record is ignored instead of read wrong.
struct CrashRecord {
  uint32_t magic;
  char task[16];
  uint32_t pc;
  uint32_t cause;          // EXCCAUSE, see causeName()
  uint32_t vaddr;          // EXCVADDR
  uint32_t backtrace[16];
  uint8_t depth;
  bool corrupted;
  char elfSha256[17];      // the first 16 hex digits, as the SDK keeps them
  int32_t resetReason;     // esp_reset_reason() of the boot that found it
  uint32_t bootTime;       // Unix time that boot started, 0 until the time is synced
};

#define CRASH_MAGIC 0x31525243UL
#define CRASH_NVS_NAMESPACE "crash"
#define CRASH_NVS_KEY "last"

// panic_abort() in the arduino-esp32 2.0.17 libraries is 26 bytes of code
// (xtensa-esp32s3-elf-nm -S firmware.elf); its write to address 0 is inside.
#define PANIC_ABORT_CODE_BYTES 26

static CrashRecord lastCrash = {};
static bool crashThisBoot = false;
static bool bootTimePending = false;
static char runningElfSha256[17] = "";

// Exception causes as ESP-IDF v4.4.7 names them in the panic output
// (components/esp_system/port/arch/xtensa/panic_arch.c). A pseudo cause - an
// interrupt watchdog, a double exception - is stored in the core dump as
// XCHAL_EXCCAUSE_NUM + PANIC_RSN_* (espcoredump/src/port/xtensa/core_dump_port.c).
static const char* causeName(uint32_t cause) {
  static const char* const reason[] = {
    "IllegalInstruction", "Syscall", "InstructionFetchError", "LoadStoreError",
    "Level1Interrupt", "Alloca", "IntegerDivideByZero", "PCValue",
    "Privileged", "LoadStoreAlignment", "res", "res",
    "InstrPDAddrError", "LoadStorePIFDataError", "InstrPIFAddrError", "LoadStorePIFAddrError",
    "InstTLBMiss", "InstTLBMultiHit", "InstFetchPrivilege", "res",
    "InstrFetchProhibited", "res", "res", "res",
    "LoadStoreTLBMiss", "LoadStoreTLBMultihit", "LoadStorePrivilege", "res",
    "LoadProhibited", "StoreProhibited", "res", "res",
    "Cp0Dis", "Cp1Dis", "Cp2Dis", "Cp3Dis",
    "Cp4Dis", "Cp5Dis", "Cp6Dis", "Cp7Dis"
  };
  static const char* const pseudo[] = {
    "Unknown reason", "Unhandled debug exception", "Double exception",
    "Unhandled kernel exception", "Coprocessor exception",
    "Interrupt wdt timeout on CPU0", "Interrupt wdt timeout on CPU1",
    "Cache disabled but cached memory region accessed"
  };
  const uint32_t reasons = sizeof(reason) / sizeof(reason[0]);
  const uint32_t pseudos = sizeof(pseudo) / sizeof(pseudo[0]);
  if (cause < reasons) return reason[cause];
  if (cause >= XCHAL_EXCCAUSE_NUM && cause - XCHAL_EXCCAUSE_NUM < pseudos) {
    return pseudo[cause - XCHAL_EXCCAUSE_NUM];
  }
  return "Unknown";
}

static bool crashFromRunningFirmware() {
  return strcmp(lastCrash.elfSha256, runningElfSha256) == 0;
}

static const char* crashName() {
  // The pc can only be compared with panic_abort() when the crashed image is
  // the one running now; in another build the function sits elsewhere.
  if (lastCrash.cause == EXCCAUSE_STORE_PROHIBITED && lastCrash.vaddr == 0 && crashFromRunningFirmware() &&
      lastCrash.pc - (uint32_t)&panic_abort < PANIC_ABORT_CODE_BYTES) {
    return lastCrash.resetReason == ESP_RST_TASK_WDT ? "Task watchdog" : "abort()";
  }
  return causeName(lastCrash.cause);
}

static bool saveCrashRecord() {
  Preferences prefs;
  if (!prefs.begin(CRASH_NVS_NAMESPACE, false)) return false;
  const bool saved = prefs.putBytes(CRASH_NVS_KEY, &lastCrash, sizeof(lastCrash)) == sizeof(lastCrash);
  prefs.end();
  return saved;
}

void crashReportBegin() {
  Preferences prefs;
  // Read-write: a read-only begin() of a namespace that does not exist yet
  // fails with an error on serial. isKey() first, because getBytes() of a
  // missing key prints one too.
  if (prefs.begin(CRASH_NVS_NAMESPACE, false)) {
    if (prefs.isKey(CRASH_NVS_KEY) && prefs.getBytesLength(CRASH_NVS_KEY) == sizeof(lastCrash)) {
      prefs.getBytes(CRASH_NVS_KEY, &lastCrash, sizeof(lastCrash));
    }
    prefs.end();
  }
  if (lastCrash.magic != CRASH_MAGIC) {
    memset(&lastCrash, 0, sizeof(lastCrash));
  }
  const uint8_t maxDepth = sizeof(lastCrash.backtrace) / sizeof(lastCrash.backtrace[0]);
  if (lastCrash.depth > maxDepth) lastCrash.depth = maxDepth;
  lastCrash.task[sizeof(lastCrash.task) - 1] = '\0';
  lastCrash.elfSha256[sizeof(lastCrash.elfSha256) - 1] = '\0';
  esp_ota_get_app_elf_sha256(runningElfSha256, sizeof(runningElfSha256));

  size_t dumpAddr = 0;
  size_t dumpSize = 0;
  if (esp_core_dump_image_get(&dumpAddr, &dumpSize) != ESP_OK) {
    return; // no core dump in flash
  }

  if (esp_core_dump_image_check() != ESP_OK) {
    Serial.println("Crash report: the core dump in flash is damaged, erasing it");
    esp_core_dump_image_erase();
    return;
  }

  esp_core_dump_summary_t* summary = (esp_core_dump_summary_t*)calloc(1, sizeof(esp_core_dump_summary_t));
  if (!summary) {
    return; // the dump stays in flash for the next boot
  }

  // After a good checksum this fails only if the partition cannot be mapped,
  // so the dump stays in flash for the next boot.
  if (esp_core_dump_get_summary(summary) != ESP_OK) {
    Serial.println("Crash report: the core dump in flash could not be read, trying again next boot");
    free(summary);
    return;
  }

  CrashRecord record = {};
  record.magic = CRASH_MAGIC;
  strncpy(record.task, summary->exc_task, sizeof(record.task) - 1);
  record.pc = summary->exc_pc;
  record.cause = summary->ex_info.exc_cause;
  record.vaddr = summary->ex_info.exc_vaddr;
  record.depth = summary->exc_bt_info.depth < maxDepth ? summary->exc_bt_info.depth : maxDepth;
  for (uint8_t i = 0; i < record.depth; i++) {
    record.backtrace[i] = summary->exc_bt_info.bt[i];
  }
  record.corrupted = summary->exc_bt_info.corrupted;
  memcpy(record.elfSha256, summary->app_elf_sha256, sizeof(record.elfSha256) - 1);
  record.resetReason = (int32_t)esp_reset_reason();

  lastCrash = record;
  crashThisBoot = true;
  bootTimePending = true;
  free(summary);
  Serial.printf("Crash report: the last run crashed in task %s, %s, pc 0x%08x, addr 0x%08x, ELF %s\n",
                lastCrash.task, crashName(), (unsigned)lastCrash.pc, (unsigned)lastCrash.vaddr,
                lastCrash.elfSha256);
  // Erased only once the summary is safe in NVS, or the crash would be lost.
  if (saveCrashRecord()) {
    esp_core_dump_image_erase();
  } else {
    Serial.println("Crash report: could not save it to NVS, the dump stays in flash");
  }
}

void crashReportLoop() {
  if (!bootTimePending || !ntpSynced) return;
  bootTimePending = false;
  lastCrash.bootTime = (uint32_t)(time(nullptr) - (time_t)(esp_timer_get_time() / 1000000));
  saveCrashRecord();
}

void crashReportToJson(JsonDocument& doc) {
  if (lastCrash.magic != CRASH_MAGIC) return;

  JsonObject crash = doc["lastCrash"].to<JsonObject>();
  char hex[11];
  crash["task"] = lastCrash.task;
  crash["cause"] = lastCrash.cause;
  crash["causeName"] = crashName();
  snprintf(hex, sizeof(hex), "0x%08x", (unsigned)lastCrash.pc);
  crash["pc"] = hex;
  snprintf(hex, sizeof(hex), "0x%08x", (unsigned)lastCrash.vaddr);
  crash["addr"] = hex;
  JsonArray backtrace = crash["backtrace"].to<JsonArray>();
  for (uint8_t i = 0; i < lastCrash.depth; i++) {
    snprintf(hex, sizeof(hex), "0x%08x", (unsigned)lastCrash.backtrace[i]);
    backtrace.add(hex);
  }
  if (lastCrash.corrupted) crash["backtraceCorrupted"] = true;
  crash["elfSha256"] = lastCrash.elfSha256;
  crash["sameFirmware"] = crashFromRunningFirmware();
  crash["resetReason"] = lastCrash.resetReason;
  if (lastCrash.bootTime) crash["bootTime"] = lastCrash.bootTime;
  crash["thisBoot"] = crashThisBoot;
}
