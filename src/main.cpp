// ============================================================
//  main.cpp  —  ESP32-S3 Audio Spectrum Visualizer  v2.0
//
//  Hardware:
//    • ESP32-S3 DevKitC-1 (or compatible)
//    • INMP441 I2S MEMS microphone
//    • 2.0" ST7789V TFT (240×320 panel, run in landscape 320×240) via SPI
//    • Rotary encoder (v5 / KEY / s1 / s2 / GND) — primary UI control
//
//  Pin mapping (see config.h / platformio.ini):
//    I2S  SCK → GPIO 41   WS → GPIO 42   SD → GPIO 2
//    TFT  MOSI→ GPIO 11   SCLK→ GPIO 12  CS → GPIO 13
//    TFT  DC  → GPIO 9    RST → GPIO 10
//    ENC  KEY → GPIO 4    s1(CLK)→ GPIO 15   s2(DT)→ GPIO 16
//         (v5 → 3.3V, GND → GND — power pins, not GPIOs)
//
//  Control scheme (PLAYBACK state):
//    • Rotate encoder      → step backward/forward through snapshots
//    • Long-hold (≥600ms)  → cycle spectrum visualization mode
//                             (BARS → WAVEFORM → RADIAL → BARS …)
//    • Short press         → arm re-record confirmation (2-step guard)
//    • Second short press  → execute re-record reset
//    • Any rotation        → cancel pending re-record confirmation
//
//  Control scheme (other states):
//    • Short press in IDLE      → start recording
//    • Short press in RECORDING → stop recording
//
//  Serial commands mirror all actions; type 'help' for list.
// ============================================================
#include <Arduino.h>
#include "config.h"
#include "AudioCapture.h"
#include "SpectrumAnalyzer.h"
#include "DisplayManager.h"
#include "SerialConsole.h"
#include "RotaryEncoder.h"

// ---- Module instances ----------------------------------------
static AudioCapture     audio;
static SpectrumAnalyzer fft;
static DisplayManager   display;
static SerialConsole    console;
static RotaryEncoder    encoder;

// ---- Application state ---------------------------------------
enum class AppState { IDLE, RECORDING, PLAYBACK };
static AppState appState = AppState::IDLE;

// Spectrum playback state
static float    specBars[NUM_BARS];
static uint32_t snapshotIndex    = 0;   // 0 .. SNAPSHOT_COUNT-1
static uint32_t recordStartMs    = 0;
static uint32_t recordDurationMs = 0;

// Two-press reset guard
static bool     pendingReset      = false;
static uint32_t pendingResetArmMs = 0;
static constexpr uint32_t PENDING_RESET_TIMEOUT_MS = 3000;

// ---- Forward declarations ------------------------------------
static void handleCommand(Cmd cmd);
static void updateSpectrum();
static uint32_t snapshotToSampleOffset(uint32_t idx);

// ============================================================
//  setup()
// ============================================================
void setup() {
    console.begin(115200);
    encoder.begin();

    display.begin();
    display.showIdle();

    if (!audio.begin()) {
        const char* err = "Audio init failed — check PSRAM / pins";
        console.printError(err);
        display.showError(err);
        while (true) { delay(1000); }
    }

    console.printOk("System ready. Press encoder knob (or type 'start') to record.");
    console.printInfo("In PLAYBACK: long-hold encoder to cycle viz mode.");
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    // ---- 1. Handle serial commands ----
    Cmd cmd = console.poll();
    if (cmd != Cmd::NONE) {
        handleCommand(cmd);
    }

    // ---- 2. Handle rotary encoder ----

    // Long-press: cycle visualization mode (PLAYBACK only, checked first so
    // the hold is consumed before a potential click fires on release).
    if (encoder.pollButtonLongPress()) {
        if (appState == AppState::PLAYBACK) {
            // Cancel any pending reset when mode-switching
            if (pendingReset) {
                pendingReset = false;
            }
            display.cycleVizMode();
            console.printInfo(
                String("Viz mode → " + String(display.vizModeName())).c_str());
            // Re-render spectrum with new mode
            updateSpectrum();
        }
    }

    // Short click: context-dependent
    if (encoder.pollButtonClicked()) {
        switch (appState) {
            case AppState::IDLE:
                handleCommand(Cmd::START);
                break;
            case AppState::RECORDING:
                handleCommand(Cmd::STOP);
                break;
            case AppState::PLAYBACK:
                // Two-press reset guard
                if (!pendingReset) {
                    pendingReset      = true;
                    pendingResetArmMs = millis();
                    display.drawStatusBar("press again to re-record  |  rotate to cancel",
                                          COLOR_WARN);
                    console.printInfo("Press knob again to reset, or rotate to cancel.");
                } else {
                    pendingReset = false;
                    handleCommand(Cmd::RESET);
                }
                break;
        }
    }

    // Rotation: step through snapshots (PLAYBACK) or cancel pending reset
    EncDir dir;
    while ((dir = encoder.pollRotation()) != EncDir::NONE) {
        if (appState == AppState::PLAYBACK) {
            if (pendingReset) {
                pendingReset = false;
                // Restore normal footer
                display.drawStatusBar(
                    "rotate:snapshot  hold:mode  press:re-record", 0);
                console.printInfo("Reset cancelled.");
            }
            handleCommand(dir == EncDir::CW ? Cmd::NEXT : Cmd::PREV);
        }
    }

    // ---- 3. State-specific update ----
    switch (appState) {

        case AppState::IDLE:
            display.showIdle();
            break;

        case AppState::RECORDING: {
            bool full = audio.update();

            uint32_t elapsed = millis() - recordStartMs;
            display.showRecording(audio.getPeakDB(), elapsed);

            static uint32_t lastPrint = 0;
            if (millis() - lastPrint >= 1000) {
                lastPrint = millis();
                console.printInfo(
                    String("Level: " + String(audio.getPeakDB(), 1) +
                           " dBFS  |  " +
                           String(elapsed / 1000) + "s / " +
                           String(MAX_RECORD_SECONDS) + "s").c_str());
            }

            // Primary stop: DMA buffer filled to capacity (audio.update() returns true).
            // Safety stop: elapsed wall-clock time has reached the limit even if the
            // buffer somehow didn't fill (e.g. slow DMA, I2S underrun, etc.).
            bool timedOut = (elapsed >= (uint32_t)MAX_RECORD_SECONDS * 1000);

            if (full || timedOut) {
                if (timedOut && !full) {
                    // Make sure the AudioCapture state is also updated
                    audio.stopRecording();
                    console.printOk("Time limit reached — showing spectrum.");
                } else {
                    console.printOk("Buffer full — showing spectrum.");
                }
                recordDurationMs = elapsed;   // use actual elapsed, not a constant
                snapshotIndex = 0;
                appState = AppState::PLAYBACK;
                updateSpectrum();
            }
            break;
        }

        case AppState::PLAYBACK:
            // Tick pending-reset timeout
            if (pendingReset &&
                (millis() - pendingResetArmMs) > PENDING_RESET_TIMEOUT_MS) {
                pendingReset = false;
                display.drawStatusBar(
                    "rotate:snapshot  hold:mode  press:re-record", 0);
                console.printInfo("Reset prompt timed out.");
            }
            break;
    }
}

// ============================================================
//  handleCommand()
// ============================================================
static void handleCommand(Cmd cmd) {
    switch (cmd) {

        case Cmd::START:
            if (appState == AppState::RECORDING) {
                console.printInfo("Already recording.");
                return;
            }
            if (appState == AppState::PLAYBACK) {
                console.printInfo("Reset first ('reset') to re-record.");
                return;
            }
            if (!audio.startRecording()) {
                console.printError("Failed to start recording.");
                display.showError("Recording failed");
                return;
            }
            appState      = AppState::RECORDING;
            recordStartMs = millis();
            console.printOk("Recording… press knob (or type 'stop') to finish.");
            break;

        case Cmd::STOP:
            if (appState != AppState::RECORDING) {
                console.printInfo("Not currently recording.");
                return;
            }
            audio.stopRecording();
            recordDurationMs = millis() - recordStartMs;
            snapshotIndex = 0;
            appState = AppState::PLAYBACK;
            console.printOk("Stopped. Computing spectrum…");
            updateSpectrum();
            break;

        case Cmd::PREV:
            if (appState != AppState::PLAYBACK) return;
            if (snapshotIndex > 0) snapshotIndex--;
            updateSpectrum();
            break;

        case Cmd::NEXT:
            if (appState != AppState::PLAYBACK) return;
            if (snapshotIndex < SNAPSHOT_COUNT - 1) snapshotIndex++;
            updateSpectrum();
            break;

        case Cmd::VIEW:
            if (appState != AppState::PLAYBACK) {
                console.printInfo("'view' only works in PLAYBACK mode.");
                return;
            }
            display.cycleVizMode();
            console.printInfo(
                String("Viz mode → " + String(display.vizModeName())).c_str());
            updateSpectrum();
            break;

        case Cmd::RESET:
            if (appState == AppState::RECORDING)
                audio.stopRecording();

            audio.clearAudio();
            pendingReset     = false;
            appState         = AppState::IDLE;
            snapshotIndex    = 0;
            recordDurationMs = 0;
            display.invalidate();
            display.showIdle();
            console.printOk("Reset complete. Press knob (or type 'start') to record.");
            break;

        case Cmd::STATUS: {
            const char* s = (appState == AppState::IDLE)      ? "IDLE"
                          : (appState == AppState::RECORDING)  ? "RECORDING"
                          : "PLAYBACK";
            console.printStatus(s,
                                audio.getSampleCount(),
                                audio.getSampleCount() * BYTES_PER_SAMPLE,
                                ESP.getFreeHeap());
            console.printInfo(
                String("Viz mode: " + String(display.vizModeName())).c_str());
            break;
        }

        case Cmd::HELP:
            console.printHelp();
            break;

        default:
            break;
    }
}

// ============================================================
//  snapshotToSampleOffset()
// ============================================================
static uint32_t snapshotToSampleOffset(uint32_t idx) {
    uint32_t total = audio.getSampleCount();
    if (total == 0) return 0;

    float fraction = (float)(idx + 1) / (float)SNAPSHOT_COUNT;
    uint32_t targetSample = (uint32_t)(fraction * (float)total);

    int64_t offset = (int64_t)targetSample - (int64_t)(FFT_SIZE / 2);
    if (offset < 0) offset = 0;
    if (offset + FFT_SIZE > total)
        offset = (total > FFT_SIZE) ? (int64_t)total - FFT_SIZE : 0;

    return (uint32_t)offset;
}

// ============================================================
//  updateSpectrum()  —  Run FFT and push to display
// ============================================================
static void updateSpectrum() {
    uint32_t total = audio.getSampleCount();
    if (total < FFT_SIZE) {
        console.printError("Not enough samples for FFT.");
        display.showError("Too short — record more audio");
        return;
    }

    uint32_t offset = snapshotToSampleOffset(snapshotIndex);

    static float pcmWindow[FFT_SIZE];
    audio.getNormalisedWindow(pcmWindow, offset, FFT_SIZE);

    fft.compute(pcmWindow, specBars);

    uint32_t cursorMs = (uint32_t)(
        ((float)(snapshotIndex + 1) / (float)SNAPSHOT_COUNT) *
        (float)recordDurationMs);

    console.printInfo(
        String("Snapshot " + String(snapshotIndex + 1) + "/" +
               String(SNAPSHOT_COUNT) + "  @ " +
               String(cursorMs / 1000.0f, 2) +
               "s  [" + String(display.vizModeName()) + "]").c_str());

    display.showSpectrum(specBars, NUM_BARS, recordDurationMs, cursorMs,
                          (uint16_t)(snapshotIndex + 1), (uint16_t)SNAPSHOT_COUNT,
                          fft.getRMS(), fft.getDominantFreqHz());
}