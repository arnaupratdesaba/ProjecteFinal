#pragma once
// ============================================================
//  SpectrumAnalyzer.h  —  FFT engine (arduinoFFT wrapper)
// ============================================================
#include <Arduino.h>
#include <arduinoFFT.h>
#include "config.h"

class SpectrumAnalyzer {
public:
    SpectrumAnalyzer();

    // Run FFT on a window of normalised PCM samples.
    // 'src'  : float array of length FFT_SIZE, values in [-1, +1]
    // 'bins' : output magnitude array, length NUM_BARS
    void compute(const float* src, float* bins);

    // Optionally set smoothing factor (0 = no smoothing, 0.8 = heavy).
    void setSmoothing(float alpha) { _alpha = alpha; }

    uint16_t getFftSize()  const { return FFT_SIZE; }
    uint16_t getNumBars()  const { return NUM_BARS; }

    // Audio stats computed during the last compute() call.
    // RMS level of the PCM window, normalised 0..1.
    float    getRMS()            const { return _rms; }
    // Frequency (Hz) of the FFT bin with the highest magnitude.
    float    getDominantFreqHz() const { return _dominantFreqHz; }

private:
    double  _real[FFT_SIZE];
    double  _imag[FFT_SIZE];
    float   _prev[NUM_BARS];  // for temporal smoothing
    float   _alpha;           // smoothing coefficient
    float   _rms;             // RMS of the last PCM window, 0..1
    float   _dominantFreqHz;  // freq of highest-magnitude FFT bin

    // Map linear FFT bins to logarithmically-spaced display bars.
    void _binsToBars(float* bars);
};