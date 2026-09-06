#pragma once

#include <stdint.h>
#include <stdlib.h>

// All selectable renderers, with the historical default rotation order.
static const uint8_t CYCLE_STYLES[] = {1, 0, 3, 2, 5, 6, 7, 8, 10, 11, 12, 14, 15, 16};
static const unsigned CYCLE_COUNT = sizeof(CYCLE_STYLES);
static const char CYCLE_DEFAULT[] =
    "1:300,0:300,3:300,2:300,5:300,6:300,7:300,8:300,10:300,11:300,12:300,14:300,15:300,16:300";
struct CycleEntry { uint8_t style; uint16_t seconds; };

// Zero duration disables an entry. Keep one non-weather renderer available.
inline bool parseCycleConfig(const char* p, CycleEntry* entries) {
  if (!p) return false;
  uint16_t seen = 0;
  bool hasFallback = false;
  for (unsigned i = 0; i < CYCLE_COUNT; ++i) {
    if (*p < '0' || *p > '9') return false;
    char* end;
    long id = strtol(p, &end, 10);
    if (*end != ':') return false;
    p = end + 1;
    if (*p < '0' || *p > '9') return false;
    long seconds = strtol(p, &end, 10);
    if (seconds != 0 && (seconds < 5 || seconds > 3600)) return false;
    int index = -1;
    for (unsigned j = 0; j < CYCLE_COUNT; ++j)
      if (id == CYCLE_STYLES[j]) index = (int)j;
    if (index < 0 || (seen & (1U << index))) return false;
    seen |= 1U << index;
    entries[i] = {(uint8_t)id, (uint16_t)seconds};
    if (seconds && id != 14) hasFallback = true;
    // Accept complete historical sets (12 or 13 styles), preserving their
    // order and durations. Append newly introduced styles as disabled.
    unsigned count = i + 1;
    if (!*end && count >= 12 && count < CYCLE_COUNT && seen == ((1U << count) - 1)) {
      for (unsigned j = count; j < CYCLE_COUNT; ++j) entries[j] = {CYCLE_STYLES[j], 0};
      return hasFallback;
    }
    if (i + 1 == CYCLE_COUNT) { if (*end) return false; }
    else { if (*end != ',') return false; }
    p = end + 1;
  }
  return hasFallback;
}
