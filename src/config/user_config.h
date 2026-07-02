/*
 * AnimatedPixelClock - User Configuration
 *
 * ============================================
 *   THIS IS THE ONLY FILE YOU NEED TO EDIT
 *   TO CONFIGURE YOUR HARDWARE!
 * ============================================
 *
 * Modify these values to match your hardware setup. The HUB75 panel pin map
 * lives in src/display/matrix_display.h (verified hardware config).
 */

#ifndef USER_CONFIG_H
#define USER_CONFIG_H

// ========== Display Configuration ==========
// Screen dimensions (2x 64x64 HUB75 panels chained side by side)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// ========== WiFi Configuration ==========
// Access Point name for initial setup.
// AP_PASSWORD: leave as "" for an open (passwordless) AP — easiest for users.
//              Set a password string to require one (e.g. "monitor123").
#define AP_NAME "PCMonitor-Setup"
#define AP_PASSWORD ""

// ========== Optional Hardcoded WiFi Credentials ==========
// Use this if your ESP32 module has a faulty WiFi AP mode
// Set SSID and password to your home network, then upload
// Leave as empty strings "" to use normal WiFiManager portal
#define HARDCODED_WIFI_SSID ""
#define HARDCODED_WIFI_PASSWORD ""

// WiFi reconnection timeout (ms) - restart if WiFi lost for this long
#define WIFI_RECONNECT_TIMEOUT 60000

// ========== Network Configuration ==========
// UDP port for receiving PC stats
#define UDP_PORT 4210

// NTP servers
#define NTP_SERVER_PRIMARY "pool.ntp.org"
#define NTP_SERVER_SECONDARY "time.nist.gov"

// NTP resync interval (1 hour in ms)
#define NTP_RESYNC_INTERVAL 3600000

// ========== Timing Configuration ==========
// Timeout for PC stats (ms) - show clock if no data received
#define STATS_TIMEOUT 10000

// Maximum time (ms) that animated clocks can override NTP time
// After this, force resync even if animation is still running
// Prevents clock drift when packets are dropped during animations
#define TIME_OVERRIDE_MAX_MS 60000

// ========== Watchdog Configuration ==========
// Watchdog timeout in seconds
#define WATCHDOG_TIMEOUT_SECONDS 30

// ========== QR Code Setup Configuration ==========
// Display QR code during WiFi AP setup for easy mobile connection
// When enabled: the panel shows a scannable QR code instead of text instructions
// When disabled: Traditional text instructions (original behavior)
#define QR_SETUP_ENABLED 0               // 1 = QR code, 0 = text instructions

// ========== Improv-Serial WiFi Setup (Web Flasher) ==========
// In-browser WiFi provisioning over USB serial, used by the web flasher at
// docs/. After flashing, ESP Web Tools probes the device for
// Improv-Serial and shows a "Configure WiFi" dialog right in the browser tab:
// the user picks their home network and the credentials are pushed over USB.
// Active only on first boot (no saved WiFi); the WiFiManager AP portal
// keeps running in parallel as a fallback. Once WiFi is
// saved, subsequent boots skip Improv entirely.
//
// Keep this ENABLED for the released web-flasher binaries so they are
// WiFi-push capable. Costs nothing if no browser is listening.
#define IMPROV_SETUP_ENABLED 1           // 1 = Improv-Serial WiFi push, 0 = AP portal only
#define IMPROV_SETUP_WINDOW_MS 180000    // 3-min Improv listen window on first boot

#endif // USER_CONFIG_H
