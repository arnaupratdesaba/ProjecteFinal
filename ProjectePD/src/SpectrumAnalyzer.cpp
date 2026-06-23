// ============================================================
//  SpectrumAnalyzer.cpp  —  FFT engine
// ============================================================
#include "SpectrumAnalyzer.h"
#include <math.h>
#include <string.h>

// We use arduinoFFT with double precision
// Constructor: FFT object is created per-compute call with stack arrays.

SpectrumAnalyzer::SpectrumAnalyzer()
    : _alpha(0.6f)
    , _rms(0.0f)
    , _dominantFreqHz(0.0f)
{
    memset(_prev, 0, sizeof(_prev));
}

// ---- Public --------------------------------------------------

void SpectrumAnalyzer::compute(const float* src, float* bins) {
    // 0. Compute RMS of the raw PCM window (before windowing modifies _real)
    double sumSq = 0.0;
    for (uint16_t i = 0; i < FFT_SIZE; i++) sumSq += (double)src[i] * (double)src[i];
    _rms = sqrtf((float)(sumSq / FFT_SIZE));

    // 1. Copy source into real[], zero imag[]
    for (uint16_t i = 0; i < FFT_SIZE; i++) {
        _real[i] = (double)src[i];
        _imag[i] = 0.0;
    }

    // 2. Apply Hann window to reduce spectral leakage
    for (uint16_t i = 0; i < FFT_SIZE; i++) {
        double w = 0.5 * (1.0 - cos(TWO_PI * i / (FFT_SIZE - 1)));
        _real[i] *= w;
    }

    // 3. ArduinoFFT v2 API
    ArduinoFFT<double> fft(_real, _imag, FFT_SIZE, (double)SAMPLE_RATE);
    fft.compute(FFTDirection::Forward);
    fft.complexToMagnitude();

    // 3b. Find the dominant frequency bin (skip DC bin 0)
    {
        double   peakMag = 0.0;
        uint16_t peakBin = 1;
        for (uint16_t k = 1; k < FFT_SIZE / 2; k++) {
            if (_real[k] > peakMag) { peakMag = _real[k]; peakBin = k; }
        }
        _dominantFreqHz = (float)peakBin * ((float)SAMPLE_RATE / (float)FFT_SIZE);
    }

    // 4. Map FFT magnitudes to logarithmic display bars
    float rawBars[NUM_BARS];
    _binsToBars(rawBars);

    // 5. Normalise + temporal smoothing
    // Find global peak for normalisation
    float maxMag = 1e-6f;
    for (uint16_t b = 0; b < NUM_BARS; b++) {
        if (rawBars[b] > maxMag) maxMag = rawBars[b];
    }

    for (uint16_t b = 0; b < NUM_BARS; b++) {
        // Convert to dBFS, map to 0..1
        float db = 20.0f * log10f((rawBars[b] / maxMag) + 1e-9f);
        // clamp -80..0 dBFS → 0..1
        float norm = constrain((db + 80.0f) / 80.0f, 0.0f, 1.0f);
        // Temporal smoothing: fall slower than rise
        float alpha = (norm > _prev[b]) ? 0.3f : _alpha;
        _prev[b] = alpha * _prev[b] + (1.0f - alpha) * norm;
        bins[b]  = _prev[b];
    }
}

// ---- Private -------------------------------------------------

void SpectrumAnalyzer::_binsToBars(float* bars) {
    // Logarithmically-spaced frequency bands.
    // We cover 20 Hz – Nyquist (SAMPLE_RATE/2).
    constexpr float  freqMin  = 40.0f;
    const     float  freqMax  = SAMPLE_RATE / 2.0f;
    const     float  freqStep = SAMPLE_RATE / (float)FFT_SIZE;

    float logMin = log10f(freqMin);
    float logMax = log10f(freqMax);

    for (uint16_t b = 0; b < NUM_BARS; b++) {
        float fLow  = powf(10.0f, logMin + (logMax - logMin) *  b       / NUM_BARS);
        float fHigh = powf(10.0f, logMin + (logMax - logMin) * (b + 1)  / NUM_BARS);

        uint16_t binLow  = max(1, (int)(fLow  / freqStep));
        uint16_t binHigh = min((uint16_t)(FFT_SIZE / 2 - 1),
                               (uint16_t)(fHigh / freqStep));

        float sum = 0.0f;
        uint16_t count = 0;
        for (uint16_t k = binLow; k <= binHigh; k++) {
            sum += (float)_real[k];
            count++;
        }
        bars[b] = (count > 0) ? (sum / count) : 0.0f;
    }
}