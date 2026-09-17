/*
 * AnimatedPixelClock - Crash report
 *
 * Reads the summary of the core dump the SDK writes to flash when the
 * firmware crashes, keeps it in NVS and adds it to /api/info as "lastCrash".
 * See crash_report.cpp for the details.
 */

#ifndef CRASH_REPORT_H
#define CRASH_REPORT_H

#include <ArduinoJson.h>

// Call once, early in setup(). If there is a core dump in flash, prints its
// summary, keeps it in NVS and erases the dump.
void crashReportBegin();

// Call from loop(). Once the time is synced, stores when the boot that found
// the crash started. Returns at once otherwise.
void crashReportLoop();

// Adds "lastCrash" to the /api/info document when a crash has been recorded.
void crashReportToJson(JsonDocument& doc);

#endif // CRASH_REPORT_H
