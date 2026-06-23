// ============================================================
//  RotaryEncoder.cpp
// ============================================================
#include "RotaryEncoder.h"

RotaryEncoder* RotaryEncoder::_instance = nullptr;

// Standard 2-bit quadrature transition table.
// Index = (prevState << 2) | newState, state = (s1<<1)|s2.
// +1 = one CW micro-step, -1 = one CCW micro-step, 0 = invalid/bounce.
static const int8_t QUAD_TABLE[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

RotaryEncoder::RotaryEncoder()
    : _lastState(0)
    , _stepAccum(0)
    , _lastRotateMs(0)
    , _btnLastRaw(true)       // INPUT_PULLUP idle-high
    , _btnStable(true)
    , _btnLastChangeMs(0)
    , _btnPressMs(0)
    , _btnLongFired(false)
    , _btnPressLatched(false)
{}

void RotaryEncoder::begin() {
    pinMode(ENC_S1_PIN, INPUT_PULLUP);
    pinMode(ENC_S2_PIN, INPUT_PULLUP);
    pinMode(ENC_KEY_PIN, INPUT_PULLUP);

    _instance  = this;
    _lastState = (int8_t)((digitalRead(ENC_S1_PIN) << 1) | digitalRead(ENC_S2_PIN));

    attachInterrupt(digitalPinToInterrupt(ENC_S1_PIN), RotaryEncoder::_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENC_S2_PIN), RotaryEncoder::_isr, CHANGE);

    Serial.printf("[Encoder] Ready — KEY=%d  s1(CLK)=%d  s2(DT)=%d\n",
                  ENC_KEY_PIN, ENC_S1_PIN, ENC_S2_PIN);
}

void IRAM_ATTR RotaryEncoder::_isr() {
    if (_instance) _instance->_handleIsr();
}

void RotaryEncoder::_handleIsr() {
    static volatile uint32_t lastUs = 0;
    uint32_t now = micros();
    if (now - lastUs < ENC_ROTATE_DEBOUNCE_US) return;
    lastUs = now;

    int8_t s1 = (int8_t)digitalRead(ENC_S1_PIN);
    int8_t s2 = (int8_t)digitalRead(ENC_S2_PIN);
    int8_t newState = (int8_t)((s1 << 1) | s2);

    int8_t idx  = (int8_t)((_lastState << 2) | newState);
    int8_t step = QUAD_TABLE[idx & 0x0F];

    _stepAccum += step;
    _lastState  = newState;

    if (step != 0) _lastRotateMs = millis();
}

EncDir RotaryEncoder::pollRotation() {
    constexpr int32_t STEPS_PER_DETENT = 4;
    if (_stepAccum >= STEPS_PER_DETENT) {
        _stepAccum -= STEPS_PER_DETENT;
        return EncDir::CW;
    }
    if (_stepAccum <= -STEPS_PER_DETENT) {
        _stepAccum += STEPS_PER_DETENT;
        return EncDir::CCW;
    }
    return EncDir::NONE;
}

int32_t RotaryEncoder::pendingSteps() {
    return _stepAccum;
}

// ============================================================
//  pollButtonClicked()
//  Returns true once on a confirmed SHORT click (press+release
//  completed before ENC_LONG_PRESS_MS). Long-press consumes
//  the event so clicks and long-presses are mutually exclusive.
// ============================================================
bool RotaryEncoder::pollButtonClicked() {
    bool raw = digitalRead(ENC_KEY_PIN);
    uint32_t now = millis();

    // Detect raw edge and start debounce timer
    if (raw != _btnLastRaw) {
        _btnLastChangeMs = now;
        _btnLastRaw      = raw;
    }

    // Debounce: only act once stable for ENC_BUTTON_DEBOUNCE_MS
    if ((now - _btnLastChangeMs) > ENC_BUTTON_DEBOUNCE_MS && _btnStable != raw) {
        bool wasStable = _btnStable;
        _btnStable = raw;

        if (wasStable == true && _btnStable == false) {
            // Confirmed PRESS — latch it, start timing for long-press
            // Guard against rotation-coupled noise
            uint32_t lastRotate = _lastRotateMs;
            if (now - lastRotate >= ENC_BTN_QUIET_AFTER_ROTATE_MS) {
                _btnPressLatched = true;
                _btnPressMs      = now;
                _btnLongFired    = false;
            }
        }

        if (wasStable == false && _btnStable == true) {
            // Confirmed RELEASE
            if (_btnPressLatched && !_btnLongFired) {
                // Released before long-press threshold → short click
                _btnPressLatched = false;
                return true;
            }
            _btnPressLatched = false;
            _btnLongFired    = false;
        }
    }

    return false;
}

// ============================================================
//  pollButtonLongPress()
//  Returns true once when button has been held ≥ ENC_LONG_PRESS_MS.
//  Fires immediately on reaching the threshold (before release).
// ============================================================
bool RotaryEncoder::pollButtonLongPress() {
    if (!_btnPressLatched || _btnLongFired) return false;

    uint32_t now = millis();
    if ((now - _btnPressMs) >= ENC_LONG_PRESS_MS) {
        _btnLongFired = true;   // suppress the subsequent release click
        return true;
    }
    return false;
}
