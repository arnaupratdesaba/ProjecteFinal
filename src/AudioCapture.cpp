// ============================================================
//  AudioCapture.cpp  —  INMP441 I2S driver & recorder
// ============================================================
#include "AudioCapture.h"
#include <math.h>
#include <string.h>

// DMA chunk size (bytes) — balance between latency and overhead
static constexpr size_t DMA_CHUNK = 512;

// ---- Constructor / Destructor --------------------------------

AudioCapture::AudioCapture()
    : _buf(nullptr)
    , _capacity(MAX_SAMPLES)
    , _sampleCount(0)
    , _state(RecordState::IDLE)
    , _peakDB(-96.0f)
    , _i2sInstalled(false)
{}

AudioCapture::~AudioCapture() {
    freeBuffer();
    _uninstallI2S();
}

// ---- Public API ----------------------------------------------

bool AudioCapture::begin() {
    // Allocate audio buffer in PSRAM when available, fallback to DRAM
    size_t wantBytes = (size_t)_capacity * BYTES_PER_SAMPLE;

#ifdef BOARD_HAS_PSRAM
    _buf = (int32_t*)ps_malloc(wantBytes);
    if (_buf) {
        Serial.printf("[Audio] Allocated %u KB in PSRAM\n",
                      (unsigned)(wantBytes / 1024));
    }
#endif
    if (!_buf) {
        _buf = (int32_t*)malloc(wantBytes);
        if (_buf) {
            Serial.printf("[Audio] Allocated %u KB in DRAM\n",
                          (unsigned)(wantBytes / 1024));
        }
    }

    if (!_buf) {
        Serial.println("[Audio] ERROR: failed to allocate audio buffer!");
        return false;
    }

    _installI2S();
    return true;
}

bool AudioCapture::startRecording() {
    if (_state == RecordState::RECORDING) return true;
    if (!_buf) return false;

    // Wipe the previous recording's samples before starting a new one, so
    // no stale audio data from an earlier take lingers in memory.
    memset(_buf, 0, (size_t)_capacity * BYTES_PER_SAMPLE);
    _sampleCount = 0;
    _peakDB      = -96.0f;
    _state       = RecordState::RECORDING;

    // Flush stale DMA data
    i2s_zero_dma_buffer(I2S_PORT);
    Serial.println("[Audio] Recording started.");
    return true;
}

void AudioCapture::stopRecording() {
    if (_state != RecordState::RECORDING) return;
    _state = RecordState::DONE;
    Serial.printf("[Audio] Recording stopped — %u samples (%.2f s)\n",
                  _sampleCount,
                  (float)_sampleCount / SAMPLE_RATE);
}

bool AudioCapture::update() {
    if (_state != RecordState::RECORDING) return false;

    static int32_t dmaBuf[DMA_CHUNK / BYTES_PER_SAMPLE];
    size_t bytesRead = 0;

    // Non-blocking read (timeout = 0)
    esp_err_t err = i2s_read(I2S_PORT, dmaBuf, DMA_CHUNK, &bytesRead, 0);
    if (err != ESP_OK || bytesRead == 0) return false;

    uint32_t gotSamples = bytesRead / BYTES_PER_SAMPLE;

    // Track peak level
    int32_t peak = 0;
    for (uint32_t i = 0; i < gotSamples; i++) {
        int32_t s = dmaBuf[i] >> 8;   // INMP441 is 24-bit left-justified in 32
        if (abs(s) > abs(peak)) peak = s;
    }
    constexpr float INT24_MAX_F = 8388607.0f;
    float peakNorm = fabsf((float)peak) / INT24_MAX_F;
    _peakDB = (peakNorm > 1e-9f) ? 20.0f * log10f(peakNorm) : -96.0f;

    // Copy into recording buffer
    uint32_t space   = _capacity - _sampleCount;
    uint32_t toCopy  = min(gotSamples, space);
    if (toCopy > 0) {
        for (uint32_t i = 0; i < toCopy; i++) {
            _buf[_sampleCount++] = dmaBuf[i] >> 8;  // store 24-bit value
        }
    }

    // Auto-stop when full
    if (_sampleCount >= _capacity) {
        stopRecording();
        return true;  // signal caller that buffer is full
    }
    return false;
}

void AudioCapture::getNormalisedWindow(float* dst, uint32_t offset,
                                        uint32_t length) const {
    constexpr float SCALE = 1.0f / 8388607.0f;
    for (uint32_t i = 0; i < length; i++) {
        uint32_t idx = offset + i;
        dst[i] = (idx < _sampleCount) ? ((float)_buf[idx] * SCALE) : 0.0f;
    }
}

void AudioCapture::freeBuffer() {
    if (_buf) {
        free(_buf);
        _buf = nullptr;
    }
    _sampleCount = 0;
    _state       = RecordState::IDLE;
}

void AudioCapture::clearAudio() {
    // Actually zero the previous recording's samples (not just the count) so
    // no stale audio data lingers in RAM once it's no longer reachable via
    // the UI — e.g. before starting a fresh recording or when returning to
    // the idle screen.
    if (_buf) {
        memset(_buf, 0, (size_t)_capacity * BYTES_PER_SAMPLE);
    }
    _sampleCount = 0;
    _peakDB      = -96.0f;
    _state       = RecordState::IDLE;
}

// ---- Private -------------------------------------------------

void AudioCapture::_installI2S() {
    if (_i2sInstalled) {
        // Already installed (e.g. a redundant begin()/reset() call) —
        // installing again would hit ESP_ERR_INVALID_STATE and abort().
        Serial.println("[Audio] I2S already installed — skipping re-install.");
        return;
    }

    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 4,
        .dma_buf_len          = 256,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0
    };

    i2s_pin_config_t pins = {
        .bck_io_num   = I2S_SCK_PIN,
        .ws_io_num    = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = I2S_SD_PIN
    };

    ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &cfg, 0, nullptr));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pins));
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_PORT));
    _i2sInstalled = true;
    Serial.println("[Audio] I2S driver installed (INMP441 / 44100 Hz / 32-bit)");
}

void AudioCapture::_uninstallI2S() {
    if (!_i2sInstalled) return;
    i2s_driver_uninstall(I2S_PORT);
    _i2sInstalled = false;
}