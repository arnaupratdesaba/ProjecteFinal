#pragma once
// ============================================================
//  config.h  —  Central pin & audio configuration
//
//  ST7789V wiring (6-pin module, backlight tied to VCC):
//    SCL (clock) ........... GPIO 12
//    SDA (data/MOSI) ....... GPIO 11
//    CS  (chip select) ..... GPIO 13
//    DC  (data/command) .... GPIO  9
//    RST (reset) ........... GPIO 10
//    VCC ................... 3.3 V
//    GND ................... GND
//
//  INMP441 wiring:
//    SCK / BCLK ............ GPIO 41
//    WS  / LRCK ............ GPIO 42
//    SD  / DATA ............ GPIO  2
//    L/R ...................  GND  (left channel)
//    VDD ................... 3.3 V
//    GND ................... GND
// ============================================================

// ---- I2S INMP441 pins ----
#ifndef I2S_SCK_PIN
  #define I2S_SCK_PIN  41
#endif
#ifndef I2S_WS_PIN
  #define I2S_WS_PIN   42
#endif
#ifndef I2S_SD_PIN
  #define I2S_SD_PIN    2
#endif

// ---- ST7789V SPI pins ----
#ifndef TFT_SCL_PIN
  #define TFT_SCL_PIN  12
#endif
#ifndef TFT_SDA_PIN
  #define TFT_SDA_PIN  11
#endif
#ifndef TFT_CS_PIN
  #define TFT_CS_PIN   13
#endif
#ifndef TFT_DC_PIN
  #define TFT_DC_PIN    9
#endif
#ifndef TFT_RST_PIN
  #define TFT_RST_PIN  10
#endif
// No BL pin — backlight is tied directly to VCC on this module

// ---- Rotary encoder pins ----
// Module labelled:  v5 (+5V/3.3V) · KEY (push-button) · s1 (CLK) · s2 (DT) · GND
// v5 and GND are power pins and are not GPIOs — only KEY/s1/s2 are wired to the MCU.
#ifndef ENC_KEY_PIN
  #define ENC_KEY_PIN  4     // KEY — push-button
#endif
#ifndef ENC_S1_PIN
  #define ENC_S1_PIN   15    // s1  — CLK
#endif
#ifndef ENC_S2_PIN
  #define ENC_S2_PIN   16    // s2  — DT
#endif
// Debounce / detent timing
#define ENC_BUTTON_DEBOUNCE_MS  60
#define ENC_ROTATE_DEBOUNCE_US  800
// Ignore button transitions for this long after the encoder was last rotated —
// cheap encoder modules can couple electrical noise from the CLK/DT contacts
// onto the KEY line while spinning, causing false clicks.
#define ENC_BTN_QUIET_AFTER_ROTATE_MS  150

// Long-press threshold: hold encoder button this long (ms) to cycle viz mode.
// Must be longer than normal click debounce but short enough to feel responsive.
#define ENC_LONG_PRESS_MS  600

// ---- Audio / FFT ----
#ifndef SAMPLE_RATE
  #define SAMPLE_RATE        44100
#endif
#ifndef FFT_SIZE
  #define FFT_SIZE           1024
#endif
#ifndef MAX_RECORD_SECONDS
  #define MAX_RECORD_SECONDS 10
#endif

// Number of fixed spectrum "snapshots" taken across a recording.
#ifndef SNAPSHOT_COUNT
  #define SNAPSHOT_COUNT     10
#endif

// Derived sizes
#define MAX_SAMPLES      ((uint32_t)(SAMPLE_RATE) * (MAX_RECORD_SECONDS))
#define BYTES_PER_SAMPLE 4
#define AUDIO_BUF_BYTES  ((MAX_SAMPLES) * (BYTES_PER_SAMPLE))

// ---- Display geometry ----
// Landscape orientation: physical panel is 240x320, rotated 90° in software
// so TFT_W is the long (320) edge and TFT_H is the short (240) edge.
#define TFT_W    320
#define TFT_H    240
#define NUM_BARS  64

// ---- Colour palette — Deep Space theme (RGB565) ----
// Background: near-black with a very slight blue tint
#define COLOR_BG        0x0841   // rgb(8, 8, 12)
// Primary text: soft white
#define COLOR_TEXT      0xEF7D   // rgb(230, 235, 240)
// Accent: electric cyan — used for header lines, progress, waveform
#define COLOR_ACCENT    0x07FF   // rgb(0, 255, 255)   pure cyan
// Warning / confirm: warm amber
#define COLOR_WARN      0xFD20   // rgb(255, 165, 0)
// Header background: deep indigo
#define COLOR_HDR_BG    0x080F   // rgb(8, 0, 30)
// Footer background: very dark navy
#define COLOR_FTR_BG    0x0409   // rgb(4, 8, 18)
// Idle/recording secondary accent: violet
#define COLOR_VIOLET    0x781F   // rgb(120, 0, 255)

// ---- I2S peripheral ----
#define I2S_PORT  I2S_NUM_0
