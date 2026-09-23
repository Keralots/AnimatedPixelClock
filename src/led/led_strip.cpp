/*
 * AnimatedPixelClock - WS2812B accent strip
 *
 * Driven from RMT channel 0: hardware clocks the bits out, so the panel's DMA
 * scan never waits on an 800 kHz bit-bang.
 */

#include "led_strip.h"

#include <driver/gpio.h>
#include <driver/rmt.h>
#include <esp_heap_caps.h>
#include <math.h>

#include "../clocks/clocks.h"
#include "../config/config.h"
#include "../config/settings.h"
#include "../display/display.h"
#include "../display/hub75_pins.h"
#include "../viz/visualizer.h"
#include "../weather/weather.h"

#define LED_RMT_CHANNEL RMT_CHANNEL_0
// 40 MHz counter, 25 ns per tick.
#define LED_RMT_CLK_DIV 2
#define LED_T0H_TICKS 16  // 400 ns
#define LED_T0L_TICKS 34  // 850 ns
#define LED_T1H_TICKS 32  // 800 ns
#define LED_T1L_TICKS 18  // 450 ns

#define LED_FRAME_MS 20  // 50 Hz

// Per-LED controller draw with the LED dark.
#define LED_MA_QUIESCENT 1

// Effect phase in 1/65536 of a cycle; uint32 wraps on a whole cycle, so no seam.
#define LED_PHASE_CYCLE 65536u
// Cycles per second at speed 1.0, in 1/65536.
#define LED_WAVE_RATE 5461u      // one span in 12 s
#define LED_RAINBOW_RATE 6554u   // one turn in 10 s
#define LED_METEOR_RATE 21845u   // one pass in 3 s
#define LED_SCANNER_RATE 16384u  // there and back in 4 s
#define LED_WAVE_SPAN 12

// Hour sweep: level of the part of the hour still to come, of 255.
#define LED_CLOCK_REST_LEVEL 10
// Afterglow time constant; only the minute wrap and the hour drain fall this fast.
#define LED_CLOCK_FADE_MS 200
// On the hour the full bar drains back, accelerating.
#define LED_CLOCK_DRAIN_MS 1500

// Fire 2012.
#define LED_FIRE_COOLING 55
#define LED_FIRE_SPARK_ZONE 7

// Fade when following the panel off / dim.
#define LED_NIGHT_FADE_MS 1000

// Weather. Chances per LED per frame, in 1/1000.
#define LED_CLOUD_SPAN 24
#define LED_RAIN_CHANCE 6
#define LED_SNOW_CHANCE 2
#define LED_RAIN_DECAY 40
#define LED_SNOW_DECAY 8
#define LED_STORM_CHANCE 4  // per frame: a flash every ~5 s
#define LED_STORM_DECAY 25
#define LED_WEATHER_POLL_MS 1000

// Audio VU, per 20 ms frame.
#define LED_VU_ATTACK 0.80f
#define LED_VU_RELEASE 0.22f
// Waveform RMS of a full-scale sine (companion WAVE_GAIN = 118).
#define LED_VU_WAVE_RMS_FULL 83.0f
#define LED_VU_PEAK_GRAVITY 0.9f  // bar fractions / s^2
#define LED_VU_STALE_MS 1500
// Raw RMS that counts as sound; the companion keeps silence flat.
#define LED_VU_SOUND_LEVEL 0.03f
// Matches the companion's AUTO_GAP_S.
#define LED_VU_GAP_MS 1000
#define LED_VU_FADE_MS 600

// Bytes per LED across all buffers: frame, VU frame, afterglow (3 each),
// sparkle and heat (1 each).
#define LED_BYTES_PER_LED 11

extern bool httpForceViz;

// Sized to the strip on enable, freed on disable. GRB, as the wire wants it.
static uint8_t *ledBuf = nullptr;
static uint16_t ledCap = 0;
static uint8_t *ledFrame = nullptr;
static uint8_t *vuFrame = nullptr;
static uint8_t *clockPrev = nullptr;
static uint8_t *sparkle = nullptr;
static uint8_t *heat = nullptr;

static uint8_t sineTable[256];
static bool rmtInstalled = false;
static int activePin = -1;
static uint16_t activeCount = 0;
static uint16_t blankCount = 0;  // LEDs cut off by a shorter count, still lit
static uint32_t lastFrameMs = 0;
static uint32_t effectPhase = 0;
static float vuLevel = 0.0f;  // 0..1 of the bar
static float vuPeak = 0.0f;
static float vuPeakVel = 0.0f;
static bool vuGateOpen = false;
static bool vuHeard = false;
static uint32_t vuSoundSince = 0;
static uint32_t vuLastSound = 0;
static float vuMix = 0.0f;  // 0 idle effect, 1 meter
static uint16_t fireAcc = 0;
static int clockLastSec = -1;
static uint32_t clockSecStartMs = 0;
static uint32_t clockPrevMs = 0;
static float nightScale = 1.0f;  // 0 off, 1 as set
static uint8_t stormFlash = 0;
static WeatherData weatherSnap;
static uint32_t weatherPolledMs = 0;
static bool weatherPolled = false;

// Bytes -> RMT items, MSB first. Runs in the RMT ISR.
static void IRAM_ATTR ledRmtTranslate(const void *src, rmt_item32_t *dest,
                                      size_t srcSize, size_t wantedNum,
                                      size_t *translatedSize, size_t *itemNum) {
  if (src == nullptr || dest == nullptr) {
    *translatedSize = 0;
    *itemNum = 0;
    return;
  }
  const rmt_item32_t bit0 = {{{LED_T0H_TICKS, 1, LED_T0L_TICKS, 0}}};
  const rmt_item32_t bit1 = {{{LED_T1H_TICKS, 1, LED_T1L_TICKS, 0}}};
  size_t size = 0, num = 0;
  const uint8_t *psrc = (const uint8_t *)src;
  rmt_item32_t *pdest = dest;
  while (size < srcSize && num + 8 <= wantedNum) {
    for (int bit = 7; bit >= 0; bit--) {
      *pdest++ = (*psrc & (1 << bit)) ? bit1 : bit0;
    }
    num += 8;
    size++;
    psrc++;
  }
  *translatedSize = size;
  *itemNum = num;
}

bool ledPinUsable(int pin) {
  if (pin <= 0 || pin > 48) return false;   // GPIO0: BOOT strap
  if (pin >= 22 && pin <= 25) return false; // not on the S3
  if (pin >= 26 && pin <= 37) return false; // flash / PSRAM
  if (pin == 19 || pin == 20) return false; // native USB
  if (pin == 43 || pin == 44) return false; // UART0: ROM boot log, serial
  if (pin == 45) return false;              // VDD_SPI strap
  const int hub75[] = {HUB75_PIN_R1, HUB75_PIN_G1, HUB75_PIN_B1, HUB75_PIN_R2,
                       HUB75_PIN_G2, HUB75_PIN_B2, HUB75_PIN_A,  HUB75_PIN_B,
                       HUB75_PIN_C,  HUB75_PIN_D,  HUB75_PIN_E,  HUB75_PIN_LAT,
                       HUB75_PIN_OE, HUB75_PIN_CLK};
  for (size_t i = 0; i < sizeof(hub75) / sizeof(hub75[0]); i++) {
    if (pin == hub75[i]) return false;
  }
  return true;
}

bool ledEffectValid(int effect) {
  return effect >= 0 && effect < LED_EFFECT_COUNT && effect != LED_EFFECT_RETIRED_BREATHE;
}

bool ledVuIdleValid(int effect) {
  if (effect == LED_VU_IDLE_OFF) return true;
  return ledEffectValid(effect) && effect != LED_EFFECT_VU;
}

// Waits out a transfer before ledFrame is refilled or freed.
static void ledWaitSent() {
  if (rmtInstalled) rmt_wait_tx_done(LED_RMT_CHANNEL, pdMS_TO_TICKS(50));
}

static void ledSend(uint16_t count) {
  if (!rmtInstalled || count == 0) return;
  rmt_write_sample(LED_RMT_CHANNEL, ledFrame, (size_t)count * 3, false);
}

static void ledBlank(uint16_t count) {
  if (ledFrame == nullptr) return;
  ledWaitSent();
  memset(ledFrame, 0, (size_t)count * 3);
  ledSend(count);
}

static void ledRmtRelease() {
  if (!rmtInstalled) return;
  rmt_driver_uninstall(LED_RMT_CHANNEL);
  rmtInstalled = false;
  // Uninstall leaves the pin routed to the RMT signal: detach it, hold DIN low.
  gpio_reset_pin((gpio_num_t)activePin);
  gpio_set_pull_mode((gpio_num_t)activePin, GPIO_FLOATING);
  gpio_set_direction((gpio_num_t)activePin, GPIO_MODE_OUTPUT);
  gpio_set_level((gpio_num_t)activePin, 0);
  activePin = -1;
}

static bool ledRmtInstall(int pin) {
  ledRmtRelease();
  rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX((gpio_num_t)pin, LED_RMT_CHANNEL);
  cfg.clk_div = LED_RMT_CLK_DIV;
  if (rmt_config(&cfg) != ESP_OK) return false;
  if (rmt_driver_install(LED_RMT_CHANNEL, 0, 0) != ESP_OK) return false;
  if (rmt_translator_init(LED_RMT_CHANNEL, ledRmtTranslate) != ESP_OK) {
    rmt_driver_uninstall(LED_RMT_CHANNEL);
    return false;
  }
  rmtInstalled = true;
  activePin = pin;
  return true;
}

static void ledFreeBuffers() {
  free(ledBuf);
  ledBuf = ledFrame = vuFrame = clockPrev = sparkle = heat = nullptr;
  ledCap = 0;
  blankCount = 0;
}

// Grows only: after a shrink the old tail still has to be blanked.
static bool ledReserve(uint16_t count) {
  if (count <= ledCap) return true;
  uint8_t *buf = (uint8_t *)heap_caps_calloc(count, LED_BYTES_PER_LED,
                                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (buf == nullptr) return false;
  ledWaitSent();
  free(ledBuf);
  ledBuf = buf;
  ledCap = count;
  ledFrame = buf;
  vuFrame = buf + (size_t)count * 3;
  clockPrev = buf + (size_t)count * 6;
  sparkle = buf + (size_t)count * 9;
  heat = buf + (size_t)count * 10;
  return true;
}

// Uniform scale-down so the estimate fits the cap: dims, never shifts colour.
static void ledApplyCurrentCap(uint16_t count) {
  uint32_t limit = settings.ledMaxMilliamps;
  if (limit == 0) return;
  uint32_t sum = 0;
  for (uint32_t i = 0; i < (uint32_t)count * 3; i++) sum += ledFrame[i];
  uint32_t draw = (sum * LED_MA_FULL_WHITE) / 765;
  if (draw == 0) return;
  uint32_t quiescent = (uint32_t)count * LED_MA_QUIESCENT;
  uint32_t budget = limit > quiescent ? limit - quiescent : 0;
  if (draw <= budget) return;
  uint32_t scale = (budget * 256) / draw;  // 0..255
  for (uint32_t i = 0; i < (uint32_t)count * 3; i++) {
    ledFrame[i] = (uint8_t)((ledFrame[i] * scale) >> 8);
  }
}

static uint8_t ledSpeedSetting() {
  uint8_t speed = settings.ledSpeed;
  if (speed < 1) return 1;
  if (speed > 20) return 20;
  return speed;
}

static uint8_t ledIntensitySetting() {
  return settings.ledIntensity > 100 ? 100 : settings.ledIntensity;
}

static void ledAdvancePhase(uint8_t effect, uint32_t elapsedMs) {
  uint32_t rate;
  switch (effect) {
  case LED_EFFECT_WAVE:
  case LED_EFFECT_WEATHER: rate = LED_WAVE_RATE; break;
  case LED_EFFECT_RAINBOW: rate = LED_RAINBOW_RATE; break;
  case LED_EFFECT_METEOR: rate = LED_METEOR_RATE; break;
  case LED_EFFECT_SCANNER: rate = LED_SCANNER_RATE; break;
  default: return;
  }
  // Speed in tenths, elapsed in ms.
  effectPhase += (elapsedMs * ledSpeedSetting() * rate) / 10000u;
}

struct LedRgb { uint8_t r, g, b; };

static LedRgb ledSlotRgb(uint8_t slot) {
  uint16_t c = settings.spriteColors[slot];
  LedRgb out;
  out.r = (uint8_t)(((c >> 11) & 0x1F) * 255 / 31);
  out.g = (uint8_t)(((c >> 5) & 0x3F) * 255 / 63);
  out.b = (uint8_t)((c & 0x1F) * 255 / 31);
  return out;
}

static void ledPut(uint16_t i, LedRgb c, uint16_t scale) {
  ledFrame[i * 3 + 0] = (uint8_t)((c.g * scale) / 255);
  ledFrame[i * 3 + 1] = (uint8_t)((c.r * scale) / 255);
  ledFrame[i * 3 + 2] = (uint8_t)((c.b * scale) / 255);
}

static LedRgb ledMix(LedRgb a, LedRgb b, float t) {
  LedRgb out;
  out.r = (uint8_t)(a.r + (b.r - a.r) * t);
  out.g = (uint8_t)(a.g + (b.g - a.g) * t);
  out.b = (uint8_t)(a.b + (b.b - a.b) * t);
  return out;
}

// Mirrored bars grow both ways from the middle, so they are half as long.
static uint16_t ledBarSpan(uint16_t count) {
  return settings.ledVuMirror ? (uint16_t)((count + 1) / 2) : count;
}

static void ledPutBar(uint16_t i, uint16_t count, LedRgb c, uint16_t scale) {
  if (!settings.ledVuMirror) {
    ledPut(i, c, scale);
    return;
  }
  // Odd strip: true centre LED. Even: grows from the seam.
  uint16_t mid = count / 2;
  if (mid + i < count) ledPut(mid + i, c, scale);
  if (count % 2) {
    if (i <= mid) ledPut(mid - i, c, scale);
  } else if (i < mid) {
    ledPut(mid - 1 - i, c, scale);
  }
}

// Shares the panel EQ's gradient slots.
static LedRgb ledVuColor(float f) {
  LedRgb low = ledSlotRgb(COL_VIZ_LOW);
  LedRgb mid = ledSlotRgb(COL_VIZ_MID);
  LedRgb peak = ledSlotRgb(COL_VIZ_PEAK);
  return f < 0.5f ? ledMix(low, mid, f * 2.0f) : ledMix(mid, peak, (f - 0.5f) * 2.0f);
}

static float ledVuWaveLevel() {
  const uint8_t *w = vizWaveform();
  if (w == nullptr) return -1.0f;
  uint32_t sq = 0;
  for (int i = 0; i < VIZ_WAVE_POINTS; i++) {
    int32_t s = (int32_t)w[i] - 128;
    sq += (uint32_t)(s * s);
  }
  return sqrtf((float)sq / (float)VIZ_WAVE_POINTS) / LED_VU_WAVE_RMS_FULL;
}

// Old companions without the waveform. The bands are AGC'd, so a flat mean
// barely moves; weighting the low end at least follows the beat.
static float ledVuBandLevel() {
  const uint8_t *bands = vizBandLevels();
  uint32_t sum = 0, weights = 0;
  for (int i = 0; i < VIZ_BANDS; i++) {
    uint32_t w = (uint32_t)(VIZ_BANDS - i);
    sum += (uint32_t)bands[i] * w;
    weights += w;
  }
  return (float)sum / (float)(weights * 255);
}

// Same arming rule as the companion's viz auto-start. Opens at once if the
// panel is already forced into the visualizer.
static void ledVuGate(float raw, uint32_t now) {
  if (raw >= LED_VU_SOUND_LEVEL) {
    if (!vuHeard || now - vuLastSound > LED_VU_GAP_MS) vuSoundSince = now;
    vuHeard = true;
    vuLastSound = now;
    if (!vuGateOpen && (now - vuSoundSince >= settings.ledVuStartS * 1000u ||
                        (httpForceViz && vizShouldDisplay()))) {
      vuGateOpen = true;
    }
  } else if (vuGateOpen && now - vuLastSound >= settings.ledVuStopS * 1000u) {
    vuGateOpen = false;
  }
}

static void ledVuAdvance(uint32_t elapsedMs, uint32_t now) {
  float raw = 0.0f;
  if (vizRecentEnough(LED_VU_STALE_MS)) {
    raw = ledVuWaveLevel();
    if (raw < 0.0f) raw = ledVuBandLevel();
  }
  ledVuGate(raw, now);
  float step = (float)elapsedMs / LED_VU_FADE_MS;
  vuMix += vuGateOpen ? step : -step;
  if (vuMix < 0.0f) vuMix = 0.0f;
  if (vuMix > 1.0f) vuMix = 1.0f;

  float target = raw * (settings.ledVuGain / 100.0f);
  if (target > 1.0f) target = 1.0f;
  vuLevel += (target - vuLevel) * (target > vuLevel ? LED_VU_ATTACK : LED_VU_RELEASE);
  if (vuLevel < 0.0f) vuLevel = 0.0f;

  float dt = elapsedMs / 1000.0f;
  if (vuLevel >= vuPeak) {
    vuPeak = vuLevel;
    vuPeakVel = 0.0f;
  } else {
    vuPeakVel += LED_VU_PEAK_GRAVITY * dt;
    vuPeak -= vuPeakVel * dt;
    if (vuPeak < vuLevel) vuPeak = vuLevel;
  }
}

static void ledBuildVu(uint16_t count) {
  memset(ledFrame, 0, (size_t)count * 3);
  uint16_t bright = settings.ledBrightness;

  uint16_t span = ledBarSpan(count);
  if (span == 0) return;
  uint16_t lit = (uint16_t)(vuLevel * span + 0.5f);
  uint16_t peakAt = (uint16_t)(vuPeak * span + 0.5f);
  if (peakAt > 0) peakAt--;

  LedRgb peakColor = ledSlotRgb(COL_VIZ_PEAK);
  for (uint16_t i = 0; i < span; i++) {
    bool isPeak = (i == peakAt) && vuPeak > 0.02f;
    if (i >= lit && !isPeak) continue;
    float f = span > 1 ? (float)i / (float)(span - 1) : 0.0f;
    ledPutBar(i, count, isPeak ? peakColor : ledVuColor(f), bright);
  }
}

// Antialiased one-LED dot at pos: its share of LED i.
static float ledDotCover(float pos, uint16_t i) {
  float d = fabsf((float)i - pos);
  return d >= 1.0f ? 0.0f : 1.0f - d;
}

// Bar filling over the hour on a faint rest glow, plus a seconds dot.
static bool ledBuildClock(uint16_t count) {
  struct tm t;
  if (!peekLocalTime(&t)) return false;
  uint32_t now = millis();
  if (t.tm_sec != clockLastSec) {
    clockLastSec = t.tm_sec;
    clockSecStartMs = now;
  }
  float subSec = (now - clockSecStartMs) / 1000.0f;
  if (subSec > 0.999f) subSec = 0.999f;

  LedRgb c = ledSlotRgb(COL_LED_STRIP);
  LedRgb dotColor = ledSlotRgb(COL_LED_SECONDS);
  uint16_t bright = settings.ledBrightness;
  uint16_t rest = (bright * LED_CLOCK_REST_LEVEL + 254) / 255;
  uint16_t span = ledBarSpan(count);
  if (span == 0) return true;
  float filled = (float)(t.tm_min * 60 + t.tm_sec) / 3600.0f * span;
  float intoHour = t.tm_sec + subSec;
  if (t.tm_min == 0 && intoHour * 1000.0f < LED_CLOCK_DRAIN_MS) {
    float p = intoHour * 1000.0f / LED_CLOCK_DRAIN_MS;
    float drain = (1.0f - p * p) * span;
    if (drain > filled) filled = drain;
  }
  uint16_t full = (uint16_t)filled;
  float frac = filled - full;
  float dot = (t.tm_sec + subSec) / 60.0f * (span - 1);
  for (uint16_t i = 0; i < span; i++) {
    uint16_t scale = rest;
    if (i < full) scale = bright;
    else if (i == full) scale = (uint16_t)(rest + (bright - rest) * frac);
    LedRgb px = c;
    float cover = ledDotCover(dot, i);
    if (cover > 0.0f) {
      px = ledMix(c, dotColor, cover);
      scale = (uint16_t)(scale + (bright - scale) * cover);
    }
    ledPutBar(i, count, px, scale);
  }
  return true;
}

// Rise at once, fall no faster than an exponential fade. Reseeds after a gap
// so the previous effect is not faded out.
static void ledClockAfterglow(uint32_t elapsedMs, uint16_t count) {
  uint32_t now = millis();
  size_t bytes = (size_t)count * 3;
  bool fresh = now - clockPrevMs > 10 * LED_FRAME_MS;
  clockPrevMs = now;
  if (fresh) {
    memcpy(clockPrev, ledFrame, bytes);
    return;
  }
  uint16_t keep = (uint16_t)(256.0f * expf(-(float)elapsedMs / LED_CLOCK_FADE_MS));
  for (size_t i = 0; i < bytes; i++) {
    uint8_t held = (uint8_t)((clockPrev[i] * keep) >> 8);
    if (ledFrame[i] < held) ledFrame[i] = held;
    clockPrev[i] = ledFrame[i];
  }
}

// Temperature stops in Celsius, blue through red.
static LedRgb ledTempRgb(float c) {
  static const float stops[] = {-10.0f, 0.0f, 10.0f, 20.0f, 28.0f, 35.0f};
  static const LedRgb cols[] = {{40, 80, 255}, {0, 200, 255}, {0, 255, 120},
                                {255, 220, 0}, {255, 110, 0}, {255, 0, 0}};
  const int n = sizeof(stops) / sizeof(stops[0]);
  if (c <= stops[0]) return cols[0];
  for (int i = 1; i < n; i++) {
    if (c <= stops[i]) {
      return ledMix(cols[i - 1], cols[i], (c - stops[i - 1]) / (stops[i] - stops[i - 1]));
    }
  }
  return cols[n - 1];
}

static bool ledBuildWeather(uint16_t count) {
  if (!weatherConfigured()) return false;
  uint32_t now = millis();
  if (!weatherPolled || now - weatherPolledMs >= LED_WEATHER_POLL_MS) {
    weatherSnap = getWeather();
    weatherPolledMs = now;
    weatherPolled = true;
  }
  if (!weatherSnap.valid) return false;

  LedRgb base = ledTempRgb(weatherSnap.tempC);
  WeatherIconKind kind = weatherIconFromCode(weatherSnap.weatherCode);
  uint16_t depth = 0;  // cloud dimming, of 255
  uint16_t chance = 0, decay = 0;
  LedRgb drop = {150, 200, 255};
  switch (kind) {
  case WICON_PARTCLOUD:
    depth = 70;
    break;
  case WICON_CLOUD:
  case WICON_FOG:
    depth = 120;
    break;
  case WICON_RAIN:
    depth = 90;
    chance = LED_RAIN_CHANCE;
    decay = LED_RAIN_DECAY;
    break;
  case WICON_SNOW:
    depth = 90;
    chance = LED_SNOW_CHANCE;
    decay = LED_SNOW_DECAY;
    drop = {255, 255, 255};
    break;
  case WICON_STORM:
    depth = 140;
    chance = LED_RAIN_CHANCE;
    decay = LED_RAIN_DECAY;
    break;
  default:
    break;
  }

  if (kind == WICON_STORM && stormFlash == 0 && (uint32_t)random(1000) < LED_STORM_CHANCE) {
    stormFlash = 255;
  }
  const LedRgb white = {255, 255, 255};
  uint8_t base8 = (uint8_t)((effectPhase % LED_PHASE_CYCLE) >> 8);
  for (uint16_t i = 0; i < count; i++) {
    uint8_t phase = (uint8_t)(base8 + (i * 256) / LED_CLOUD_SPAN);
    uint16_t level = 255 - (depth * sineTable[phase]) / 255;
    LedRgb c = base;
    if (chance && sparkle[i] == 0 && (uint32_t)random(1000) < chance) sparkle[i] = 255;
    if (sparkle[i]) {
      c = ledMix(c, drop, sparkle[i] / 255.0f);
      level += ((255 - level) * sparkle[i]) / 255;
      sparkle[i] = sparkle[i] > decay ? sparkle[i] - decay : 0;
    }
    if (stormFlash) {
      c = ledMix(c, white, stormFlash / 255.0f);
      level = 255;
    }
    ledPut(i, c, (uint16_t)(level * settings.ledBrightness / 255));
  }
  stormFlash = stormFlash > LED_STORM_DECAY ? stormFlash - LED_STORM_DECAY : 0;
  return true;
}

static void ledBuildSolid(uint16_t count) {
  LedRgb strip = ledSlotRgb(COL_LED_STRIP);
  for (uint16_t i = 0; i < count; i++) ledPut(i, strip, settings.ledBrightness);
}

static void ledBuildWave(uint16_t count) {
  LedRgb strip = ledSlotRgb(COL_LED_STRIP);
  uint8_t base = (uint8_t)((effectPhase % LED_PHASE_CYCLE) >> 8);
  for (uint16_t i = 0; i < count; i++) {
    uint8_t phase = (uint8_t)(base + (i * 256) / LED_WAVE_SPAN);
    uint16_t level = 60 + (sineTable[phase] * 195) / 255;
    ledPut(i, strip, level * settings.ledBrightness / 255);
  }
}

// Saturated hue wheel, h in 1/256 of a turn.
static LedRgb ledHue(uint8_t h) {
  uint8_t region = h / 43;
  uint8_t rise = (uint8_t)((h - region * 43) * 6);
  uint8_t fall = 255 - rise;
  switch (region) {
  case 0: return {255, rise, 0};
  case 1: return {fall, 255, 0};
  case 2: return {0, 255, rise};
  case 3: return {0, fall, 255};
  case 4: return {rise, 0, 255};
  default: return {255, 0, fall};
  }
}

// Intensity 0: whole strip one hue; 50: one rainbow; 100: two.
static void ledBuildRainbow(uint16_t count) {
  uint16_t span = ledBarSpan(count);
  if (span == 0) return;
  uint32_t spread = (uint32_t)ledIntensitySetting() * 512 / 100;
  uint8_t base = (uint8_t)((effectPhase % LED_PHASE_CYCLE) >> 8);
  for (uint16_t i = 0; i < span; i++) {
    uint8_t h = (uint8_t)(base + (i * spread) / span);
    ledPutBar(i, count, ledHue(h), settings.ledBrightness);
  }
}

// FastLED HeatColor.
static LedRgb ledHeatRgb(uint8_t temp) {
  uint8_t t192 = (uint8_t)((temp * 191) / 255);
  uint8_t ramp = (uint8_t)((t192 & 0x3F) << 2);
  if (t192 & 0x80) return {255, 255, ramp};
  if (t192 & 0x40) return {255, ramp, 0};
  return {ramp, 0, 0};
}

// Fire 2012; 50 steps/s at speed 1.0.
static void ledBuildFire(uint32_t elapsedMs, uint16_t count) {
  uint16_t span = ledBarSpan(count);
  if (span == 0) return;
  uint16_t sparking = 40 + ledIntensitySetting() * 180 / 100;
  uint16_t cooling = (LED_FIRE_COOLING * 10) / span + 2;
  fireAcc += (uint16_t)(ledSpeedSetting() * elapsedMs / LED_FRAME_MS);
  while (fireAcc >= 10) {
    fireAcc -= 10;
    for (uint16_t i = 0; i < span; i++) {
      uint8_t cool = (uint8_t)random(cooling);
      heat[i] = heat[i] > cool ? heat[i] - cool : 0;
    }
    for (uint16_t k = span - 1; k >= 2; k--) {
      heat[k] = (uint8_t)((heat[k - 1] + 2 * heat[k - 2]) / 3);
    }
    if ((uint16_t)random(255) < sparking) {
      uint16_t y = (uint16_t)random(span < LED_FIRE_SPARK_ZONE ? span : LED_FIRE_SPARK_ZONE);
      uint16_t h = heat[y] + (uint16_t)random(160, 255);
      heat[y] = h > 255 ? 255 : (uint8_t)h;
    }
  }
  for (uint16_t i = 0; i < span; i++) {
    ledPutBar(i, count, ledHeatRgb(heat[i]), settings.ledBrightness);
  }
}

// Meteor runs one way, scanner bounces. Intensity is the tail.
static void ledBuildComet(uint8_t effect, uint16_t count) {
  uint16_t span = ledBarSpan(count);
  if (span == 0) return;
  LedRgb c = ledSlotRgb(COL_LED_STRIP);
  float t = (float)(effectPhase % LED_PHASE_CYCLE) / LED_PHASE_CYCLE;
  float head;
  bool forward = true;
  float tail = 1.0f + ledIntensitySetting() / 100.0f * span *
                          (effect == LED_EFFECT_METEOR ? 0.8f : 0.5f);
  if (effect == LED_EFFECT_METEOR) {
    // Past the end by a tail, so it runs out before the next one.
    head = t * (span + tail) - 1.0f;
  } else {
    forward = t < 0.5f;
    head = (forward ? t * 2.0f : 2.0f - t * 2.0f) * (span - 1);
  }
  for (uint16_t i = 0; i < span; i++) {
    float d = forward ? head - (float)i : (float)i - head;
    float level;
    if (d <= -1.0f || d >= tail) {
      level = 0.0f;
    } else if (d < 0.0f) {
      level = 1.0f + d;
    } else {
      level = 1.0f - d / tail;
      level *= level * level;
    }
    ledPutBar(i, count, c, (uint16_t)(level * settings.ledBrightness));
  }
}

// Any effect but the VU. Clock and weather fall back to Solid until they have data.
static void ledRunEffect(uint8_t effect, uint32_t elapsed, uint16_t count) {
  ledAdvancePhase(effect, elapsed);
  memset(ledFrame, 0, (size_t)count * 3);
  switch (effect) {
  case LED_EFFECT_WAVE: ledBuildWave(count); return;
  case LED_EFFECT_RAINBOW: ledBuildRainbow(count); return;
  case LED_EFFECT_FIRE: ledBuildFire(elapsed, count); return;
  case LED_EFFECT_METEOR:
  case LED_EFFECT_SCANNER: ledBuildComet(effect, count); return;
  case LED_EFFECT_CLOCK:
    if (ledBuildClock(count)) {
      ledClockAfterglow(elapsed, count);
      return;
    }
    break;
  case LED_EFFECT_WEATHER:
    if (ledBuildWeather(count)) return;
    break;
  default:
    break;
  }
  ledBuildSolid(count);
}

// Meter while music plays, idle effect otherwise, crossfaded.
static void ledBuildVuOrIdle(uint32_t elapsed, uint32_t now) {
  uint16_t count = activeCount;
  size_t bytes = (size_t)count * 3;
  ledVuAdvance(elapsed, now);
  if (vuMix > 0.0f) {
    ledBuildVu(count);
    if (vuMix >= 1.0f) return;
    memcpy(vuFrame, ledFrame, bytes);
  }
  uint8_t idle = settings.ledVuIdle;
  if (idle == LED_VU_IDLE_OFF || !ledVuIdleValid(idle)) {
    memset(ledFrame, 0, bytes);
  } else {
    ledRunEffect(idle, elapsed, count);
  }
  if (vuMix <= 0.0f) return;
  uint16_t mix = (uint16_t)(vuMix * 256.0f);
  for (size_t i = 0; i < bytes; i++) {
    ledFrame[i] = (uint8_t)((ledFrame[i] * (256 - mix) + vuFrame[i] * mix) >> 8);
  }
}

// Dark with the panel, dimmed by its dim ratio, faded both ways.
static void ledFollowPanel(uint32_t elapsedMs, uint16_t count) {
  uint8_t applied = getAppliedBrightness();
  uint8_t normal = sanitizeBrightnessValue(settings.displayBrightness);
  float target = 0.0f;
  if (applied > 0 && normal > 0) target = applied >= normal ? 1.0f : (float)applied / normal;
  float step = (float)elapsedMs / LED_NIGHT_FADE_MS;
  if (nightScale < target) nightScale = nightScale + step > target ? target : nightScale + step;
  else if (nightScale > target) nightScale = nightScale - step < target ? target : nightScale - step;
  if (nightScale >= 1.0f) return;
  uint16_t scale = (uint16_t)(nightScale * 256.0f);
  for (uint32_t i = 0; i < (uint32_t)count * 3; i++) {
    ledFrame[i] = (uint8_t)((ledFrame[i] * scale) >> 8);
  }
}

void ledInit() {
  for (int i = 0; i < 256; i++) {
    sineTable[i] = (uint8_t)(127.5f + 127.5f * sinf((float)i * 2.0f * (float)M_PI / 256.0f));
  }
  ledApplySettings();
}

void ledApplySettings() {
  if (settings.ledCount > LED_STRIP_MAX_COUNT) settings.ledCount = LED_STRIP_MAX_COUNT;
  if (settings.ledBrightness == 0) settings.ledBrightness = 1;

  bool off = !settings.ledEnabled || settings.ledCount == 0 || !ledPinUsable(settings.ledPin);
  if (rmtInstalled && (off || activePin != settings.ledPin)) {
    // Dark, not frozen on the last frame.
    ledBlank(activeCount > blankCount ? activeCount : blankCount);
    ledWaitSent();
    ledRmtRelease();
    activeCount = 0;
    blankCount = 0;
  }
  if (off) {
    activeCount = 0;
    ledFreeBuffers();
    return;
  }

  if (!ledReserve(settings.ledCount)) {
    Serial.printf("LED strip: no RAM for %u LEDs\n", (unsigned)settings.ledCount);
    if (rmtInstalled) {
      ledBlank(activeCount);
      ledWaitSent();
      ledRmtRelease();
    }
    activeCount = 0;
    ledFreeBuffers();
    return;
  }
  if (!rmtInstalled) {
    if (!ledRmtInstall(settings.ledPin)) {
      Serial.printf("LED strip: RMT install failed on GPIO%d\n", settings.ledPin);
      activeCount = 0;
      ledFreeBuffers();
      return;
    }
    Serial.printf("LED strip: %u LEDs on GPIO%d\n", (unsigned)settings.ledCount,
                  settings.ledPin);
  }
  if (settings.ledCount < activeCount && activeCount > blankCount) blankCount = activeCount;
  activeCount = settings.ledCount;
}

void ledLoop() {
  if (!rmtInstalled || activeCount == 0) return;
  uint32_t now = millis();
  uint32_t elapsed = now - lastFrameMs;
  if (elapsed < LED_FRAME_MS) return;
  // After a stall (OTA, long response) resume instead of jumping ahead.
  if (elapsed > 10 * LED_FRAME_MS) elapsed = LED_FRAME_MS;
  lastFrameMs = now;

  if (blankCount) {
    ledBlank(blankCount);
    blankCount = 0;
    return;
  }

  if (settings.ledEffect == LED_EFFECT_VU) {
    ledBuildVuOrIdle(elapsed, now);
  } else {
    ledRunEffect(settings.ledEffect, elapsed, activeCount);
  }
  ledFollowPanel(elapsed, activeCount);
  ledApplyCurrentCap(activeCount);
  ledSend(activeCount);
}
