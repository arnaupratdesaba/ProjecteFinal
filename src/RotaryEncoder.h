#pragma once
// ============================================================
//  RotaryEncoder.h  —  KY-040-style rotary encoder driver
//
//  Module pins:  v5 (power) · KEY (push-button) · s1 (CLK) · s2 (DT) · GND
//  Only KEY / s1 / s2 connect to the MCU (v5 → 3.3V, GND → GND).
//
//  - Rotation is decoded via a hardware interrupt on s1/s2 so no
//    detents are missed even while FFT/display work is running.
//  - The button is polled + debounced (called from loop()).
//  - Long-press detection: hold ≥ ENC_LONG_PRESS_MS → pollButtonLongPress()
//    returns true once. Short click and long-press are mutually exclusive.
// ============================================================
#include <Arduino.h>
#include "config.h"

enum class EncDir { NONE, CW, CCW };

class RotaryEncoder {
public:
    RotaryEncoder();

    // Attaches pins + interrupt. Call once from setup().
    void begin();

    // Call every loop() — handles button debounce.
    // Returns true exactly once for a completed short click (press+release
    // within ENC_LONG_PRESS_MS). Long-press consumes the event instead.
    bool pollButtonClicked();

    // Returns true once when button has been held ≥ ENC_LONG_PRESS_MS.
    // Fires on the press side (before release) so feedback is immediate.
    // After firing, release is ignored by pollButtonClicked().
    bool pollButtonLongPress();

    // Drains and returns the next pending rotation step, if any.
    EncDir pollRotation();

    // Number of detents queued (positive = net CW, negative = net CCW).
    int32_t pendingSteps();

private:
    static void IRAM_ATTR _isr();
    static RotaryEncoder* _instance;

    void _handleIsr();

    // Rotation state (touched by ISR — keep volatile)
    volatile int8_t   _lastState;
    volatile int32_t  _stepAccum;
    volatile uint32_t _lastRotateMs;

    // Button state (polled only)
    bool     _btnLastRaw;
    bool     _btnStable;
    uint32_t _btnLastChangeMs;
    uint32_t _btnPressMs;       // time when debounced PRESS was latched
    bool     _btnLongFired;     // long-press event already delivered
    bool     _btnPressLatched;  // we have a confirmed debounced press pending
};
