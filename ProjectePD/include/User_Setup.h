// ============================================================
//  User_Setup.h  —  TFT_eSPI configuration for ST7789V 2.0"
//
//  Module pinout (6-pin, no backlight pin):
//    SCL (clock)  → GPIO 12
//    SDA (data)   → GPIO 11
//    CS           → GPIO 13
//    DC           → GPIO  9
//    RST          → GPIO 10
//    VCC          → 3.3 V   (backlight tied directly to VCC on PCB)
//    GND          → GND
//
//  NOTE: TFT_WIDTH/TFT_HEIGHT below are the panel's native PORTRAIT
//  dimensions — TFT_eSPI always wants these as shipped. The UI runs in
//  LANDSCAPE by calling tft.setRotation(1) at runtime in DisplayManager
//  (see config.h's TFT_W=320 / TFT_H=240). Do not swap these two macros.
// ============================================================

// CRITICAL: must be defined so TFT_eSPI uses THIS file, not its own
#define USER_SETUP_LOADED

// ---- Driver ----
#define ST7789_DRIVER
#define TFT_WIDTH   240
#define TFT_HEIGHT  320

// ---- Colour inversion — required by ST7789 panels ----
#define TFT_INVERSION_ON

// ---- SPI pins ----
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   13
#define TFT_DC    9
#define TFT_RST  10
#define TFT_MISO -1   // not present on this module

// ---- No backlight pin — tied to VCC on PCB ----
// #define TFT_BL  -1  // leave undefined; do NOT define as a real GPIO

// ---- No touch ----
#define TOUCH_CS -1

// ---- SPI clock ----
#define SPI_FREQUENCY      40000000
#define SPI_READ_FREQUENCY  6000000

// ---- Fonts ----
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
