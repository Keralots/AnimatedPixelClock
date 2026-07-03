/*
 * AnimatedPixelClock - Notification Banner Module
 *
 * 14px banner band (top or bottom) with an optional 8x8 icon and text that
 * scrolls when it does not fit. Text motion is dt-based so it stays smooth
 * at any refresh rate.
 */

#include "notify.h"

#include "../config/config.h"
#include "../display/display.h"

// ========== Built-in 8x8 icons (1-bit, MSB = leftmost pixel) ==========
struct NotifyIcon {
  const char* name;
  uint8_t rows[8];
};

static const NotifyIcon NOTIFY_ICONS[] = {
    {"bell",  {0x18, 0x3C, 0x3C, 0x7E, 0x7E, 0xFF, 0x18, 0x00}},
    {"mail",  {0xFF, 0xC3, 0xA5, 0x99, 0x81, 0x81, 0xFF, 0x00}},
    {"alert", {0x18, 0x24, 0x3C, 0x5A, 0x5A, 0x81, 0x99, 0xFF}},
    {"heart", {0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00}},
    {"check", {0x00, 0x01, 0x03, 0x86, 0xCC, 0x78, 0x30, 0x00}},
    {"cross", {0xC3, 0xE7, 0x7E, 0x3C, 0x3C, 0x7E, 0xE7, 0xC3}},
    {"info",  {0x18, 0x18, 0x00, 0x38, 0x18, 0x18, 0x3C, 0x00}},
    {"home",  {0x18, 0x3C, 0x7E, 0xFF, 0x66, 0x66, 0x7E, 0x00}},
    {"music", {0x1F, 0x11, 0x11, 0x11, 0x11, 0x77, 0x77, 0x00}},
    {"star",  {0x18, 0x18, 0xFF, 0x7E, 0x3C, 0x66, 0x42, 0x00}},
};
static const int NOTIFY_ICON_COUNT =
    sizeof(NOTIFY_ICONS) / sizeof(NOTIFY_ICONS[0]);

// ========== Banner geometry ==========
#define NOTIFY_BAND_H 14
#define NOTIFY_SCROLL_PX_PER_SEC 40.0f
#define NOTIFY_SCROLL_GAP 24  // px of blank between marquee wraps

// ========== State (file-local) ==========
static char notifyText[NOTIFY_TEXT_MAX + 1];
static uint16_t notifyColor = DISPLAY_WHITE;
static int8_t notifyIcon = -1;
static uint8_t notifyPos = 0;  // 0=bottom, 1=top
static unsigned long notifyEndAt = 0;
static bool notifyOn = false;
static float scrollOffset = 0;
static unsigned long lastScrollMs = 0;

int notifyIconIdByName(const char* name) {
  if (!name) return -1;
  for (int i = 0; i < NOTIFY_ICON_COUNT; i++) {
    if (strcasecmp(name, NOTIFY_ICONS[i].name) == 0) return i;
  }
  return -1;
}

const char* notifyIconNames() {
  return "bell,mail,alert,heart,check,cross,info,home,music,star";
}

void notifySet(const char* text, uint16_t color565, int8_t iconId,
               uint32_t durationMs, uint8_t position) {
  strlcpy(notifyText, text, sizeof(notifyText));
  notifyColor = color565;
  notifyIcon = (iconId >= 0 && iconId < NOTIFY_ICON_COUNT) ? iconId : -1;
  notifyPos = position ? 1 : 0;
  notifyEndAt = millis() + durationMs;
  scrollOffset = 0;
  lastScrollMs = millis();
  notifyOn = true;
}

void notifyDismiss() { notifyOn = false; }

bool notifyActive() {
  if (notifyOn && (long)(millis() - notifyEndAt) >= 0) notifyOn = false;
  return notifyOn;
}

static void drawIcon8x8(int x, int y, const uint8_t* rows, uint16_t color) {
  for (int r = 0; r < 8; r++) {
    uint8_t bits = rows[r];
    for (int c = 0; c < 8; c++) {
      if (bits & (0x80 >> c)) display.drawPixel(x + c, y + r, color);
    }
  }
}

void drawNotifyOverlay() {
  if (!notifyActive()) return;

  int bandY = (notifyPos == 1) ? 0 : SCREEN_HEIGHT - NOTIFY_BAND_H;
  int accentY = (notifyPos == 1) ? NOTIFY_BAND_H - 1 : bandY;
  int contentY = (notifyPos == 1) ? 0 : bandY + 1;

  display.fillRect(0, bandY, SCREEN_WIDTH, NOTIFY_BAND_H, DISPLAY_BLACK);
  display.drawFastHLine(0, accentY, SCREEN_WIDTH, notifyColor);

  bool hasIcon = (notifyIcon >= 0);
  int textX0 = hasIcon ? 14 : 3;
  int avail = SCREEN_WIDTH - textX0 - 2;
  int textW = strlen(notifyText) * 6 - 1;
  int textY = contentY + 4;

  display.setTextSize(1);
  display.setTextWrap(false);
  display.setTextColor(notifyColor);

  if (textW <= avail) {
    display.setCursor(textX0 + (avail - textW) / 2, textY);
    display.print(notifyText);
  } else {
    // Marquee: advance dt-based, wrap after the text + gap has fully passed.
    unsigned long now = millis();
    scrollOffset += (now - lastScrollMs) * (NOTIFY_SCROLL_PX_PER_SEC / 1000.0f);
    lastScrollMs = now;
    int span = textW + NOTIFY_SCROLL_GAP;
    if (scrollOffset >= span) scrollOffset -= span;
    int x = textX0 - (int)scrollOffset;
    display.setCursor(x, textY);
    display.print(notifyText);
    display.setCursor(x + span, textY);
    display.print(notifyText);
    // Text may have spilled left of the band's text area; repaint that strip
    // so the marquee appears clipped (icon zone or left margin).
    display.fillRect(0, contentY, textX0, NOTIFY_BAND_H - 2, DISPLAY_BLACK);
  }

  if (hasIcon) {
    drawIcon8x8(3, contentY + 3, NOTIFY_ICONS[notifyIcon].rows, notifyColor);
  }

  // Leave shared GFX state the way the clock renderers expect it.
  display.setTextColor(DISPLAY_WHITE);
  display.setTextWrap(true);
}
