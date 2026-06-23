#pragma once
// ============================================================
//  AudioCapture.h  —  INMP441 I2S driver & ring-buffer recorder
// ============================================================
#include <Arduino.h>
#include <driver/i2s.h>
#include "config.h"

enum class RecordState { IDLE, RECORDING, DONE };

class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    // Initialise I2S peripheral. Returns true on success.
    bool begin();

    // Start / stop streaming capture into internal buffer.
    bool startRecording();
    void stopRecording();

    // Polling call — feed from loop(). Fills the buffer while recording.
    // Returns true if the buffer just became full (auto-stopped).
    bool update();

    // Read the recorded PCM buffer (32-bit left-justified samples from INMP441).
    // Returns the number of valid samples captured.
    const int32_t* getSamples()     const { return _buf; }
    uint32_t       getSampleCount() const { return _sampleCount; }

    RecordState getState() const { return _state; }

    // Convenience: fill a float array with normalised [-1, +1] values
    // starting at 'offset' for 'length' samples.
    void getNormalisedWindow(float* dst, uint32_t offset, uint32_t length) const;

    // Peak dBFS for the last DMA chunk (live level meter).
    float getPeakDB() const { return _peakDB; }

    // Free the heap buffer (call after done processing).
    void freeBuffer();

    // Wipe the contents of the recording buffer and reset sample count,
    // without freeing/reallocating memory. Call this whenever a previous
    // recording's audio should no longer be considered valid/available
    // (returning to idle, starting a fresh recording, etc).
    void clearAudio();

private:
    int32_t*    _buf;
    uint32_t    _capacity;    // max samples buffer can hold
    uint32_t    _sampleCount; // samples written so far
    RecordState _state;
    float       _peakDB;
    bool        _i2sInstalled;  // guards against double-install / double-uninstall

    void _installI2S();
    void _uninstallI2S();
};