/*
 * AnimatedPixelClock - Web Server Module
 *
 * Web server handlers for configuration interface.
 */

#ifndef WEB_H
#define WEB_H

#include "../config/config.h"
#include <Update.h>
#include <WebServer.h>


#include "../config/settings.h"

// Global web server object
extern WebServer server;

// ========== Web Server Functions ==========

// Initialize web server with all routes
void setupWebServer();

// Web handlers
void handleRoot();
void handlePortalCss();
void handlePortalJs();
void handleSave();
void handleReset();
void handleMetricsAPI();
void handleDeviceInfo();
void handleRename();
void handleExportConfig();
void handleImportConfig();

// Notification banner API
void handleNotify();
void handleNotifyDismiss();

// Custom animation storage API (uploaded .pca files on LittleFS)
void handleAnimList();
void handleAnimDelete();
void handleAnimUploadDone();
void handleAnimUploadChunk();

// Runtime control API (display power, mode, brightness, clock style, reboot)
void handleStatus();
void handleDisplayOn();
void handleDisplayOff();
void handleSetBrightness();
void handleModeClock();
void handleModeAuto();
void handleModeAmbient();
void handleModeViz();
void handleSetClockStyle();
void handleReboot();

#endif // WEB_H
