#pragma once
// ============================================================
//  DisplayManager.h  —  ST7789V TFT renderer
//
//  Orientation: LANDSCAPE (320x240). Panel is physically 240x320
//  portrait; setRotation(1) in begin() rotates it 90° so TFT_W/TFT_H
//  (from config.h) match the rotated frame.
//
//  Spectrum visualization modes (cycle with long-press in PLAYBACK):
//    BARS    — classic vertical bar graph with cyan-to-violet gradient
//    WAVEFORM — oscilloscope-style line drawn from bar magnitudes
//    RADIAL   — polar/circular bars radiating from screen centre
// ============================================================
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "config.h"

// UI screens
enum class Screen { IDLE, RECORDING, SPECTRUM, ERROR_MSG };

// Spectrum visualization modes — cycle in PLAYBACK via long-press
enum class VizMode { BARS, WAVEFORM, RADIAL, _COUNT };

class DisplayManager {
public:
    DisplayManager();

    void begin();
    void setBrightness(uint8_t value);  // 0–255 (PWM on BL pin — no-op here)

    // ---- Screen renderers ----
    void showIdle();          // animated homepage — call repeatedly from loop()
    void showRecording(float peakDB, uint32_t elapsedMs);
    void showSpectrum(const float* bars, uint16_t numBars,
                      uint32_t durationMs, uint32_t windowMs,
                      uint16_t snapshotIndex = 0, uint16_t snapshotCount = 0,
                      float rms = 0.0f, float dominantHz = 0.0f);
    void showError(const char* msg);

    // ---- Partial update helpers (called inside showSpectrum) ----
    void drawBars(const float* bars, uint16_t numBars);
    void drawWaveform(const float* bars, uint16_t numBars);
    void drawRadial(const float* bars, uint16_t numBars);
    void drawTimeline(uint32_t totalMs, uint32_t cursorMs);
    void drawStatusBar(const char* label, uint16_t colour);
    void drawAudioStats(float rms, float dominantHz, uint32_t windowMs);

    // ---- Viz mode cycling ----
    void     cycleVizMode();
    VizMode  getVizMode() const { return _vizMode; }
    const char* vizModeName() const;

    // ---- State tracking (so callers can force redraws) ----
    Screen currentScreen() const { return _screen; }
    void   invalidate()          { _dirty = true; }

private:
    TFT_eSPI    _tft;
    TFT_eSprite _sprite;  // double-buffer sprite for flicker-free rendering

    // Cached geometry
    uint16_t  _barW;      // width of one bar including gap
    uint16_t  _barAreaH;  // usable height for bar/waveform area
    uint16_t  _barAreaY;  // top-Y of bar area

    // Cached radial-mode geometry (computed once in begin() from real
    // screen bounds, so bars can never overshoot the bar-drawing area)
    uint16_t  _radCx;        // centre X
    uint16_t  _radCy;        // centre Y
    uint16_t  _radInner;     // hub radius
    uint16_t  _radOuterMax;  // max bar length outward, fitted to available space

    // Animation & screen state
    Screen    _screen;
    VizMode   _vizMode;
    bool      _dirty;
    uint32_t  _animFrame;
    uint32_t  _lastAnimMs;

    // ---- colour helpers ----
    uint16_t _spectrumColour(float norm);  // cyan-to-violet gradient
    uint16_t _hsl(float h, float s, float l); // h in [0,1), s/l in [0,1]

    // ---- private draw helpers ----
    void _drawHeader(const char* title);
    void _drawFooter(const char* text);
    void _drawIdleAnimation();
    void _drawRecordingAnimation(float peakDB, uint32_t elapsedMs);

    // Catmull-Rom spline interpolation through 4 control points (p0..p3),
    // sampled at t in [0,1] between p1 and p2. Used to turn the 64 discrete
    // bar magnitudes into a smooth, continuous wave instead of straight
    // connect-the-dots segments.
    static float _catmullRom(float p0, float p1, float p2, float p3, float t);
};