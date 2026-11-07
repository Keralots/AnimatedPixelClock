
#ifndef MATRIX_DISPLAY_H
#define MATRIX_DISPLAY_H

#include <Arduino.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "hub75_pins.h"

// Physical HUB75 panel configuration
#define HUB75_PANEL_W 64
#define HUB75_PANEL_H 32
#define HUB75_CHAIN   4

// Virtual display configuration: 2 columns x 2 rows
#define VIRTUAL_ROWS   2
#define VIRTUAL_COLS   2
#define VIRTUAL_WIDTH  (HUB75_PANEL_W * VIRTUAL_COLS)
#define VIRTUAL_HEIGHT (HUB75_PANEL_H * VIRTUAL_ROWS)

// Create HUB75 configuration
inline HUB75_I2S_CFG makeMatrixConfig() {
    HUB75_I2S_CFG::i2s_pins pins = {
        HUB75_PIN_R1, HUB75_PIN_G1, HUB75_PIN_B1,
        HUB75_PIN_R2, HUB75_PIN_G2, HUB75_PIN_B2,
        HUB75_PIN_A, HUB75_PIN_B, HUB75_PIN_C, HUB75_PIN_D,
        HUB75_PIN_E,
        HUB75_PIN_LAT, HUB75_PIN_OE, HUB75_PIN_CLK
    };

    HUB75_I2S_CFG cfg(
        HUB75_PANEL_W,
        HUB75_PANEL_H,
        HUB75_CHAIN,
        pins
    );

    cfg.driver = HUB75_I2S_CFG::FM6124;
    cfg.clkphase = false;
    cfg.double_buff = true;

    return cfg;
}

// Matrix display abstraction
class MatrixDisplay : public MatrixPanel_I2S_DMA {
public:

    explicit MatrixDisplay(const HUB75_I2S_CFG &cfg)
        : MatrixPanel_I2S_DMA(cfg) {
        _width = VIRTUAL_WIDTH;
        _height = VIRTUAL_HEIGHT;
    }

    // Display control
    inline void clearDisplay() {
        clearScreen();
    }

    inline void display() {
        if (!ready()) return;

        flipDMABuffer();
        lastFlipUs = micros();
        hasFlipped = true;
    }

    inline void waitForScanCompletion() {
        if (!hasFlipped || calculated_refresh_rate <= 0) return;

        const uint32_t scanUs =
            (1000000UL + calculated_refresh_rate - 1) /
            calculated_refresh_rate + 100;

        const uint32_t elapsed = micros() - lastFlipUs;

        if (elapsed < scanUs) {
            delayMicroseconds(scanUs - elapsed);
        }
    }

    inline uint8_t *getBuffer() {
        return nullptr;
    }

    inline int refreshRate() const {
        return calculated_refresh_rate;
    }

    inline uint16_t width() const {
        return VIRTUAL_WIDTH;
    }

    inline uint16_t height() const {
        return VIRTUAL_HEIGHT;
    }

    // DMA lifecycle
    bool ready() const;
    void releaseDma();

    // Virtual-to-physical coordinate mapping
    bool mapVirtualToPhysical(
        int16_t &x,
        int16_t &y
    ) const;

    // RGB565 drawing overrides
    void drawPixel(
        int16_t x,
        int16_t y,
        uint16_t color
    ) override;

    void drawFastHLine(
        int16_t x,
        int16_t y,
        int16_t w,
        uint16_t color
    ) override;

    void drawFastVLine(
        int16_t x,
        int16_t y,
        int16_t h,
        uint16_t color
    ) override;

    void fillRect(
        int16_t x,
        int16_t y,
        int16_t w,
        int16_t h,
        uint16_t color
    ) override;

    // RGB888 drawing
    void drawPixelRGB888(
        int16_t x,
        int16_t y,
        uint8_t r,
        uint8_t g,
        uint8_t b
    );

private:

    uint32_t lastFlipUs = 0;
    bool hasFlipped = false;
};

// Rebuild display with a new HUB75 configuration
bool rebuildMatrixDisplay(
    MatrixDisplay &panel,
    const HUB75_I2S_CFG &cfg
);

#endif // MATRIX_DISPLAY_H
