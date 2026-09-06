#pragma once

#include <stdint.h>
#include <stdlib.h>

// All selectable renderers, with the historical default rotation order.
static const uint8_t CYCLE_STYLES[] = {1, 0, 3, 2, 5, 6, 7, 8, 10, 11, 12, 14, 15};
static const unsigned CYCLE_COUNT = sizeof(CYCLE_STYLES);
static const char CYCLE_DEFAULT[] =
    "1:300,0:300,3:300,2:300,5:300,6:300,7:300,8:300,10:300,11:300,12:300,14:300,15:300";
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
    // Existing twelve-style configurations retain their order and durations.
    if (i + 2 == CYCLE_COUNT && !*end && !(seen & (1U << (CYCLE_COUNT - 1)))) {
      entries[i + 1] = {15, 0};
      return hasFallback;
    }
    if (i + 1 == CYCLE_COUNT) { if (*end) return false; }
    else { if (*end != ',') return false; }
    p = end + 1;
  }
  return hasFallback;
}
