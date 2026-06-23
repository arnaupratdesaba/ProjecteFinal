// ============================================================
//  DisplayManager.cpp  —  ST7789V TFT renderer (LANDSCAPE)
//
//  Color palette: "Deep Space" — dark navy background, cyan-to-violet
//  spectrum gradient, amber warnings.  Replaces the old green theme.
//
//  Three spectrum visualization modes (long-press encoder to cycle):
//    BARS    — vertical gradient bars with top highlight tick
//    WAVEFORM — smooth line graph drawn across bar magnitudes
//    RADIAL   — polar bars radiating from screen centre
// ============================================================
#include "DisplayManager.h"
#include <math.h>

// ---- Layout constants ----------------------------------------
static constexpr uint16_t HEADER_H   = 26;
static constexpr uint16_t FOOTER_H   = 18;
static constexpr uint16_t TIMELINE_H = 14;
static constexpr uint16_t STATS_H    = 18;
static constexpr uint16_t GAP        = 2;
static constexpr uint16_t BAR_GAP    = 1;

// ---- Idle animation constants --------------------------------
static constexpr uint8_t  IDLE_BARS  = 32;
static constexpr uint16_t IDLE_BAR_W = TFT_W / IDLE_BARS;
static constexpr uint16_t IDLE_AREA_Y = HEADER_H + 14;
static constexpr uint16_t IDLE_AREA_H = TFT_H - HEADER_H - FOOTER_H - 28;

// ---- Recording screen layout (left col + right oscilloscope) -
static constexpr uint16_t REC_LEFT_W  = 130;
static constexpr uint16_t REC_GAP_X   = 8;
static constexpr uint16_t VU_X = 14;
static constexpr uint16_t VU_Y = HEADER_H + 14;
static constexpr uint16_t VU_W = REC_LEFT_W - 2 * 14;
static constexpr uint16_t VU_H = 18;

static constexpr uint16_t WV_X  = REC_LEFT_W + REC_GAP_X;
static constexpr uint16_t WV_Y  = HEADER_H + 8;
static constexpr uint16_t WV_W  = TFT_W - WV_X - 10;
static constexpr uint16_t WV_H  = TFT_H - HEADER_H - FOOTER_H - 16;
static constexpr uint16_t WV_CY = WV_Y + WV_H / 2;

// ---- Radial viz constants ------------------------------------
// NOTE: centre + outer radius are NOT hardcoded here anymore — they are
// computed in begin() from the actual bar-area geometry, so bars can
// never overshoot into the stats/timeline strip or off-screen. Only the
// hub (inner) radius is a fixed cosmetic constant.
static constexpr uint16_t RAD_INNER = 22;   // inner radius (hub)
static constexpr uint16_t RAD_MARGIN = 6;   // safety margin kept clear at full deflection

// ============================================================
//  Constructor
// ============================================================
DisplayManager::DisplayManager()
    : _tft()
    , _sprite(&_tft)
    , _barW(0)
    , _barAreaH(0)
    , _barAreaY(0)
    , _radCx(0)
    , _radCy(0)
    , _radInner(RAD_INNER)
    , _radOuterMax(0)
    , _screen(Screen::IDLE)
    , _vizMode(VizMode::BARS)
    , _dirty(true)
    , _animFrame(0)
    , _lastAnimMs(0)
{}

// ============================================================
//  begin()
// ============================================================
void DisplayManager::begin() {
    _tft.init();
    _tft.setRotation(1);          // Landscape 320×240
    _tft.fillScreen(COLOR_BG);
    _tft.setTextDatum(MC_DATUM);

    // Pre-compute spectrum layout geometry
    _barAreaY = HEADER_H + GAP;
    _barAreaH = TFT_H - HEADER_H - FOOTER_H - TIMELINE_H - STATS_H - 4 * GAP;
    _barW     = TFT_W / NUM_BARS;

    // Sprite for flicker-free rendering (bars + waveform share same region)
    _sprite.createSprite(TFT_W, _barAreaH);
    _sprite.setColorDepth(16);

    // ---- Radial mode geometry — fitted to the ACTUAL bar-drawing area ----
    // Centre sits in the middle of the bar area (same region BARS/WAVEFORM
    // use), so all three viz modes occupy the same visual "canvas".
    _radCx = TFT_W / 2;
    _radCy = _barAreaY + _barAreaH / 2;

    // The limiting dimension is whichever is smaller: vertical clearance
    // from centre to the top/bottom of the bar area, or horizontal
    // clearance from centre to the left/right screen edge. Bars must fit
    // within this radius at full deflection (norm = 1.0) in every direction.
    uint16_t vClearance = min((uint16_t)(_radCy - _barAreaY),
                              (uint16_t)(_barAreaY + _barAreaH - _radCy));
    uint16_t hClearance = min(_radCx, (uint16_t)(TFT_W - _radCx));
    uint16_t maxRadius  = min(vClearance, hClearance);

    // Reserve a small margin so the bright tip-dot and outer energy ring
    // never get clipped right at the boundary.
    _radOuterMax = (maxRadius > RAD_INNER + RAD_MARGIN)
                 ? (maxRadius - RAD_INNER - RAD_MARGIN)
                 : 10;  // degenerate fallback for absurdly small screens
}

void DisplayManager::setBrightness(uint8_t value) {
    (void)value;  // No BL pin on this module
}

// ============================================================
//  Viz mode cycling
// ============================================================
void DisplayManager::cycleVizMode() {
    _vizMode = (VizMode)(((int)_vizMode + 1) % (int)VizMode::_COUNT);
    _dirty = true;  // Force a full screen clear on next showSpectrum call
}

const char* DisplayManager::vizModeName() const {
    switch (_vizMode) {
        case VizMode::BARS:     return "BARS";
        case VizMode::WAVEFORM: return "WAVE";
        case VizMode::RADIAL:   return "RADIAL";
        default:                return "?";
    }
}

// ============================================================
//  showIdle()  —  Animated homepage
// ============================================================
void DisplayManager::showIdle() {
    if (_screen != Screen::IDLE || _dirty) {
        _tft.fillScreen(COLOR_BG);
        _drawHeader("  SPECTRUM VISUALIZER  ");
        _drawFooter("press knob or 'start' to record");

        // Subtitle
        _tft.setTextColor(_tft.color565(50, 55, 75), COLOR_BG);
        _tft.setTextSize(1);
        _tft.setTextDatum(MC_DATUM);
        _tft.drawString("ESP32-S3  |  INMP441  |  ST7789V", TFT_W / 2, HEADER_H + 8);

        // READY label
        _tft.setTextColor(COLOR_ACCENT, COLOR_BG);
        _tft.setTextSize(2);
        _tft.drawString("READY", TFT_W / 2, TFT_H - FOOTER_H - 30);
        _tft.setTextSize(1);
        _tft.setTextColor(_tft.color565(45, 50, 70), COLOR_BG);
        _tft.drawString("press encoder knob to start", TFT_W / 2, TFT_H - FOOTER_H - 14);

        _screen    = Screen::IDLE;
        _animFrame = 0;
        _dirty     = false;
    }

    uint32_t now = millis();
    if (now - _lastAnimMs < 50) return;   // ~20 fps
    _lastAnimMs = now;

    _drawIdleAnimation();
    _animFrame++;
}

// ============================================================
//  showRecording()
// ============================================================
void DisplayManager::showRecording(float peakDB, uint32_t elapsedMs) {
    if (_screen != Screen::RECORDING) {
        _tft.fillScreen(COLOR_BG);
        _screen    = Screen::RECORDING;
        _animFrame = 0;
    }

    uint32_t now = millis();
    if (now - _lastAnimMs < 66) return;   // ~15 fps
    _lastAnimMs = now;

    _drawRecordingAnimation(peakDB, elapsedMs);
    _animFrame++;
}

// ============================================================
//  showSpectrum()
// ============================================================
void DisplayManager::showSpectrum(const float* bars, uint16_t numBars,
                                   uint32_t durationMs, uint32_t windowMs,
                                   uint16_t snapshotIndex, uint16_t snapshotCount,
                                   float rms, float dominantHz) {
    bool modeChange = (_screen != Screen::SPECTRUM) || _dirty;
    if (modeChange) {
        _tft.fillScreen(COLOR_BG);
        _screen = Screen::SPECTRUM;
        _dirty  = false;
    }

    // Build header: snapshot info + current viz mode name
    char title[32];
    if (snapshotCount > 0) {
        snprintf(title, sizeof(title), " %u/%u  [%s] ",
                 (unsigned)snapshotIndex, (unsigned)snapshotCount,
                 vizModeName());
    } else {
        snprintf(title, sizeof(title), " SPECTRUM [%s] ", vizModeName());
    }
    _drawHeader(title);

    // Footer depends on viz mode
    _drawFooter("rotate:snapshot  hold:mode  press:re-record");

    // Draw the chosen visualization
    switch (_vizMode) {
        case VizMode::BARS:     drawBars(bars, numBars);     break;
        case VizMode::WAVEFORM: drawWaveform(bars, numBars); break;
        case VizMode::RADIAL:   drawRadial(bars, numBars);   break;
        default:                drawBars(bars, numBars);     break;
    }

    drawAudioStats(rms, dominantHz, windowMs);
    drawTimeline(durationMs, windowMs);
}

// ============================================================
//  showError()
// ============================================================
void DisplayManager::showError(const char* msg) {
    _tft.fillScreen(COLOR_BG);
    _screen = Screen::ERROR_MSG;
    _drawHeader(" ERROR ");
    _tft.setTextColor(TFT_RED, COLOR_BG);
    _tft.setTextSize(1);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString(msg, TFT_W / 2, TFT_H / 2);
    _drawFooter("ERROR");
}

// ============================================================
//  drawBars()  —  Classic vertical gradient bar graph
// ============================================================
void DisplayManager::drawBars(const float* bars, uint16_t numBars) {
    _sprite.fillSprite(COLOR_BG);

    for (uint16_t b = 0; b < numBars; b++) {
        float    norm = constrain(bars[b], 0.0f, 1.0f);
        uint16_t h    = (uint16_t)(norm * _barAreaH);
        uint16_t x    = b * _barW;
        uint16_t y    = _barAreaH - h;

        if (h > 0) {
            uint16_t col = _spectrumColour(norm);
            _sprite.fillRect(x + BAR_GAP, y, _barW - 2 * BAR_GAP, h, col);
            // Bright top highlight tick
            _sprite.drawFastHLine(x + BAR_GAP, y, _barW - 2 * BAR_GAP, TFT_WHITE);
        }
        // Faint baseline grid tick
        _sprite.drawFastVLine(x + _barW / 2, _barAreaH - 2, 2,
                              _tft.color565(18, 18, 30));
    }

    _sprite.pushSprite(0, _barAreaY);
}

// ============================================================
//  drawWaveform()  —  Oscilloscope-style bipolar sine wave.
//
//  Each bar magnitude (0..1) is treated as the *amplitude* of a
//  sinusoidal carrier at that bar's frequency.  The waveform is
//  synthesised by summing a set of sine components — one per bar —
//  centred on the mid-line of the drawing area, exactly as a real
//  oscilloscope would show a time-domain audio waveform.
//
//  Layout:
//    • Horizontal centre-line = silence (0 V)
//    • Positive peaks go UP,  negative peaks go DOWN
//    • The maximum half-excursion is (_barAreaH/2 - 4) pixels
//    • Colour tracks the instantaneous |amplitude| via _spectrumColour()
//
//  Amplitude handling (two passes — this is what makes the trace
//  actually move instead of looking flat):
//    1. Build the raw composite sample for every column, normalising
//       by sqrt(sum(amp²)) ("energy") rather than sum(amp) ("linear").
//       Dividing by the linear sum drives the result toward zero as
//       soon as more than one or two bars are active — summing several
//       out-of-phase sine waves and dividing by their total amplitude
//       is an incoherent average, and averages of out-of-phase signals
//       shrink as more components join in. Energy-based normalisation
//       avoids that dilution, so a handful of active bars still gives
//       a strong trace instead of a near-flat line.
//    2. Auto-gain the whole frame so its own peak sample reaches close
//       to the full available height — like a scope's vertical-gain
//       knob auto-adjusting to fill the screen — gated by the
//       snapshot's overall RMS so genuine silence still renders as a
//       near-flat line instead of amplifying noise into a fake "loud"
//       wave.
//
//  Rendering:
//    • TFT_W pixel columns are computed across one full cycle (0..2π).
//    • A dim, colour-matched fill is drawn from the centre-line out to
//      the trace, giving the wave visual weight instead of reading as
//      a thin wire.
//    • Adjacent columns are joined with drawLine() for a solid trace.
//    • A subtle glow line (1 px above) is drawn in the same colour at
//      reduced brightness, mimicking the phosphor bloom of a real scope.
//    • Subtle horizontal grid at ±50% amplitude and the zero line.
// ============================================================
void DisplayManager::drawWaveform(const float* bars, uint16_t numBars) {
    _sprite.fillSprite(COLOR_BG);

    const uint16_t centreY = _barAreaH / 2;          // zero-volt axis in sprite coords
    const uint16_t halfH   = centreY - 4;             // max pixel excursion from centre

    // ---- Grid ----
    // Zero line
    _sprite.drawFastHLine(0, centreY, TFT_W, _tft.color565(25, 25, 45));
    // ±50 % amplitude guide lines
    _sprite.drawFastHLine(0, centreY - halfH / 2, TFT_W, _tft.color565(15, 15, 30));
    _sprite.drawFastHLine(0, centreY + halfH / 2, TFT_W, _tft.color565(15, 15, 30));

    // ---- Loudness gate ----
    // This no longer sets the trace's scale directly (that crushed quiet
    // material) — it only decides how far the auto-gain below is allowed
    // to open up, so true silence still settles to a thin flat line
    // instead of getting amplified into noise.
    float rmsAmp = 0.0f;
    for (uint16_t b = 0; b < numBars; b++) rmsAmp += bars[b] * bars[b];
    rmsAmp = sqrtf(rmsAmp / (float)numBars);
    float envelope = constrain(rmsAmp / 0.06f, 0.0f, 1.0f);

    // ---- Pass 1: synthesise the raw (un-gained) composite sample ----
    // Each bar b contributes a sinusoidal component:
    //   freq ratio  = (b+1) / numBars  (so bars map to integer harmonics)
    //   amplitude   = bars[b]          (normalised 0..1)
    //   phase offset= b * π/numBars    (avoids all components starting in phase,
    //                                    which would produce an unrealistic spike)
    static float   rawY[TFT_W];   // composite sample per column, pre-gain
    static int16_t waveY[TFT_W];  // final pixel Y per column
    float peakAbs = 1e-6f;

    for (uint16_t px = 0; px < TFT_W; px++) {
        float phase  = ((float)px / (float)(TFT_W - 1)) * (2.0f * M_PI);
        float sample = 0.0f;
        float energy = 0.0f;

        for (uint16_t b = 0; b < numBars; b++) {
            float amp = constrain(bars[b], 0.0f, 1.0f);
            if (amp < 0.005f) continue;          // skip negligible components
            // Frequency: use the bar index to pick a harmonic ratio.
            // Lower bars (bass) get lower frequencies, higher bars get higher.
            float freq  = (float)(b + 1);
            float phOff = (float)b * M_PI / (float)numBars;
            sample += amp * sinf(freq * phase * 0.25f + phOff);
            energy += amp * amp;
        }

        // Energy-based normalisation (see header comment) — keeps the
        // composite well-scaled without crushing it toward zero as more
        // bars contribute.
        if (energy > 1e-9f) sample /= sqrtf(energy);

        rawY[px] = sample;
        float a = fabsf(sample);
        if (a > peakAbs) peakAbs = a;
    }

    // ---- Pass 2: auto-gain to this frame's own peak, then map to pixels ----
    // autoGain alone would stretch even quiet noise to full height, so it's
    // blended with `envelope`: loud snapshots get the full auto-gain, quiet
    // ones are pulled back toward a small but non-zero swing.
    float autoGain = constrain(0.92f / peakAbs, 1.0f, 6.0f);
    float gain     = autoGain * (0.12f + 0.88f * envelope);

    for (uint16_t px = 0; px < TFT_W; px++) {
        float sample = constrain(rawY[px] * gain, -1.0f, 1.0f);
        int16_t yPix = (int16_t)(centreY - sample * halfH);
        yPix = constrain(yPix, (int16_t)2, (int16_t)(_barAreaH - 3));
        waveY[px] = yPix;
    }

    // ---- Draw filled area + trace ----
    for (uint16_t px = 1; px < TFT_W; px++) {
        int16_t y0 = waveY[px - 1];
        int16_t y1 = waveY[px];

        // Colour tracks instantaneous normalised displacement from centre
        float disp = fabsf((float)(y1 - (int16_t)centreY) / (float)halfH);
        uint16_t col  = _spectrumColour(constrain(disp, 0.0f, 1.0f));

        // Dim fill from the centre-line out to the trace — gives the wave
        // visual mass instead of reading as a thin, flat wire.
        uint16_t fillCol = _tft.color565(
            (uint8_t)(((col >> 11) & 0x1F) * 2),   // R, ~25% brightness
            (uint8_t)(((col >>  5) & 0x3F) * 1),   // G, ~25% brightness
            (uint8_t)( (col        & 0x1F) * 2)    // B, ~25% brightness
        );
        if (y1 < (int16_t)centreY)
            _sprite.drawFastVLine(px, y1, centreY - y1, fillCol);
        else if (y1 > (int16_t)centreY)
            _sprite.drawFastVLine(px, centreY, y1 - centreY, fillCol);

        // Main trace (2 px thick: draw the line and one pixel above)
        _sprite.drawLine(px - 1, y0,     px, y1,     col);
        _sprite.drawLine(px - 1, y0 - 1, px, y1 - 1, col);

        // Glow: faint duplicate 1 px above the bright trace
        uint16_t glowCol = _tft.color565(
            (uint8_t)(((col >> 11) & 0x1F) * 4),          // R (5-bit → scale up slightly)
            (uint8_t)(((col >>  5) & 0x3F) * 2),          // G (6-bit)
            (uint8_t)( (col        & 0x1F) * 4)            // B
        );
        _sprite.drawPixel(px, y1 - 2, glowCol);
    }

    // ---- Dot at each bar's zero-crossing marker (subtle tick on the axis) ----
    for (uint16_t b = 0; b < numBars; b++) {
        float norm = constrain(bars[b], 0.0f, 1.0f);
        if (norm > 0.08f) {
            uint16_t px = (uint16_t)(b * _barW + _barW / 2);
            if (px < TFT_W) {
                _sprite.drawFastVLine(px, centreY - 2, 5, _tft.color565(40, 40, 70));
            }
        }
    }

    _sprite.pushSprite(0, _barAreaY);
}

// ============================================================
//  drawRadial()  —  Polar bar graph radiating from screen centre
//  Bars are evenly distributed around 360°.  Each bar is a
//  filled sector from _radInner outward, coloured by magnitude.
//  Geometry (_radCx/_radCy/_radInner/_radOuterMax) is computed once
//  in begin() from the real bar-area bounds, so bars always fit
//  perfectly within the screen — they can never overshoot into the
//  stats/timeline strip or past the screen edge.
// ============================================================
void DisplayManager::drawRadial(const float* bars, uint16_t numBars) {
    // For radial we draw directly to the TFT (not the sprite, which is bar-area only)
    // Clear the bar area first
    _tft.fillRect(0, _barAreaY, TFT_W,
                  _barAreaH + STATS_H + TIMELINE_H + 4 * GAP, COLOR_BG);

    uint16_t cx = _radCx;
    uint16_t cy = _radCy;

    // Draw a subtle hub circle
    _tft.fillCircle(cx, cy, _radInner - 2, _tft.color565(10, 10, 20));
    _tft.drawCircle(cx, cy, _radInner,     _tft.color565(40, 40, 80));

    float angleStep = (2.0f * (float)M_PI) / (float)numBars;

    for (uint16_t b = 0; b < numBars; b++) {
        float norm  = constrain(bars[b], 0.0f, 1.0f);
        float angle = (float)b * angleStep - (float)M_PI / 2.0f; // start at top
        float len   = _radInner + norm * _radOuterMax;

        float cosA = cosf(angle);
        float sinA = sinf(angle);

        int16_t x0 = (int16_t)(cx + cosA * (_radInner + 1));
        int16_t y0 = (int16_t)(cy + sinA * (_radInner + 1));
        int16_t x1 = (int16_t)(cx + cosA * len);
        int16_t y1 = (int16_t)(cy + sinA * len);

        uint16_t col = _spectrumColour(norm);

        // Draw 2-pixel-wide radial line by also offsetting by ±1 perp
        _tft.drawLine(x0, y0, x1, y1, col);

        // Perpendicular offset for thickness
        float px = -sinA;
        float py =  cosA;
        _tft.drawLine(x0 + (int16_t)px, y0 + (int16_t)py,
                      x1 + (int16_t)px, y1 + (int16_t)py, col);

        // Bright dot at tip for tall bars
        if (norm > 0.4f) {
            _tft.drawPixel(x1, y1, TFT_WHITE);
        }
    }

    // Outer ring showing overall energy — guaranteed to stay within
    // the reserved RAD_MARGIN since avgNorm <= 1.0 by construction
    float avgNorm = 0.0f;
    for (uint16_t b = 0; b < numBars; b++) avgNorm += bars[b];
    avgNorm /= numBars;
    uint16_t ringR = (uint16_t)(_radInner + avgNorm * _radOuterMax + 6);
    _tft.drawCircle(cx, cy, ringR, _tft.color565(20, 20, 50));
}

// ============================================================
//  drawAudioStats()  —  Stats strip between bars & timeline
// ============================================================
void DisplayManager::drawAudioStats(float rms, float dominantHz, uint32_t /*windowMs*/) {
    uint16_t sy = _barAreaY + _barAreaH + GAP;

    // Background strip — deep indigo
    _tft.fillRect(0, sy, TFT_W, STATS_H, _tft.color565(6, 6, 18));
    _tft.drawFastHLine(0, sy, TFT_W, _tft.color565(16, 16, 45));

    // ---- Left: RMS bar + dBFS label ----
    float rmsDB   = (rms > 1e-9f) ? 20.0f * log10f(rms) : -96.0f;
    float rmsNorm = constrain((rmsDB + 60.0f) / 60.0f, 0.0f, 1.0f);

    constexpr uint16_t RMS_BAR_X = 4;
    constexpr uint16_t RMS_BAR_W = 88;
    constexpr uint16_t RMS_BAR_H = 6;
    uint16_t rmsBarY = sy + (STATS_H - RMS_BAR_H) / 2;
    uint16_t rmsFill = (uint16_t)(rmsNorm * RMS_BAR_W);

    uint16_t rmsCol = (rmsNorm > 0.85f) ? (uint16_t)TFT_RED
                    : (rmsNorm > 0.65f) ? (uint16_t)COLOR_WARN
                    : _tft.color565(0, 200, 255);  // cyan

    _tft.fillRect(RMS_BAR_X, rmsBarY, RMS_BAR_W, RMS_BAR_H, _tft.color565(18, 18, 35));
    if (rmsFill > 0)
        _tft.fillRect(RMS_BAR_X, rmsBarY, rmsFill, RMS_BAR_H, rmsCol);
    _tft.drawRect(RMS_BAR_X, rmsBarY, RMS_BAR_W, RMS_BAR_H, _tft.color565(35, 35, 65));

    char dbBuf[12];
    snprintf(dbBuf, sizeof(dbBuf), "%.0f dB", rmsDB);
    _tft.setTextSize(1);
    _tft.setTextColor(_tft.color565(120, 140, 200), _tft.color565(6, 6, 18));
    _tft.setTextDatum(ML_DATUM);
    _tft.drawString(dbBuf, RMS_BAR_X + RMS_BAR_W + 4, sy + STATS_H / 2);

    // ---- Right: dominant frequency ----
    char freqBuf[20];
    if (dominantHz >= 1000.0f)
        snprintf(freqBuf, sizeof(freqBuf), "pk %.2f kHz", dominantHz / 1000.0f);
    else
        snprintf(freqBuf, sizeof(freqBuf), "pk %.0f Hz", dominantHz);

    _tft.setTextColor(_tft.color565(80, 200, 255), _tft.color565(6, 6, 18));
    _tft.setTextDatum(MR_DATUM);
    _tft.drawString(freqBuf, TFT_W - 4, sy + STATS_H / 2);
}

// ============================================================
//  drawTimeline()
// ============================================================
void DisplayManager::drawTimeline(uint32_t totalMs, uint32_t cursorMs) {
    uint16_t tlY = _barAreaY + _barAreaH + GAP + STATS_H + GAP;

    _tft.fillRect(0, tlY, TFT_W, TIMELINE_H, _tft.color565(10, 10, 18));

    if (totalMs > 0) {
        uint16_t cursorX = (uint16_t)((uint32_t)cursorMs * TFT_W / totalMs);
        // Filled portion: deep cyan-tinted navy
        _tft.fillRect(0, tlY, cursorX, TIMELINE_H, _tft.color565(0, 60, 90));
        // Cursor line: bright cyan
        _tft.fillRect(max(0, (int)cursorX - 1), tlY, 3, TIMELINE_H, COLOR_ACCENT);
    }

    char buf[14];
    _tft.setTextSize(1);
    _tft.setTextColor(_tft.color565(80, 90, 130), _tft.color565(10, 10, 18));
    _tft.setTextDatum(ML_DATUM);
    snprintf(buf, sizeof(buf), "%lu.%01lus",
             (unsigned long)(cursorMs / 1000),
             (unsigned long)((cursorMs % 1000) / 100));
    _tft.drawString(buf, 4, tlY + TIMELINE_H / 2);

    _tft.setTextDatum(MR_DATUM);
    snprintf(buf, sizeof(buf), "/%lus", (unsigned long)(totalMs / 1000));
    _tft.drawString(buf, TFT_W - 4, tlY + TIMELINE_H / 2);
}

// ============================================================
//  drawStatusBar()  —  Thin footer override (e.g. confirm prompts)
// ============================================================
void DisplayManager::drawStatusBar(const char* label, uint16_t colour) {
    // If a non-zero colour is passed use it as a warning tint on the footer bg
    uint16_t fy = TFT_H - FOOTER_H;
    uint16_t bg = (colour != 0) ? _tft.color565(25, 15, 0) : COLOR_FTR_BG;
    _tft.fillRect(0, fy, TFT_W, FOOTER_H, bg);
    _tft.drawFastHLine(0, fy, TFT_W,
                       (colour != 0) ? COLOR_WARN : _tft.color565(0, 40, 80));
    _tft.setTextColor(colour != 0 ? COLOR_WARN : _tft.color565(70, 80, 120), bg);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextSize(1);
    _tft.drawString(label, TFT_W / 2, fy + FOOTER_H / 2);
}

// ============================================================
//  _drawIdleAnimation()  —  Breathing aurora bars
// ============================================================
void DisplayManager::_drawIdleAnimation() {
    uint16_t areaY = IDLE_AREA_Y;
    uint16_t areaH = IDLE_AREA_H;

    for (uint8_t b = 0; b < IDLE_BARS; b++) {
        float phase = (float)_animFrame * 0.035f;
        float norm  = constrain(
            0.30f
            + sinf(phase + (float)b * 0.52f) * 0.38f
            + sinf(phase * 0.7f + (float)b * 0.28f) * 0.22f
            + sinf(phase * 1.2f + (float)b * 0.75f) * 0.14f,
            0.04f, 0.92f);

        uint16_t h = (uint16_t)(norm * areaH);
        uint16_t x = b * IDLE_BAR_W;
        uint16_t y = areaY + areaH - h;

        // Erase old bar
        _tft.fillRect(x + 1, areaY, IDLE_BAR_W - 2, areaH, COLOR_BG);

        // Hue cycles slowly: cyan (0.5) → violet (0.75) → back
        float hue = 0.50f + 0.25f * sinf((float)b / IDLE_BARS * (float)M_PI * 2.0f
                                          + (float)_animFrame * 0.004f);
        uint16_t col = _hsl(hue, 0.85f, 0.25f + norm * 0.35f);

        _tft.fillRect(x + 1, y, IDLE_BAR_W - 2, h, col);
    }
}

// ============================================================
//  _drawRecordingAnimation()
// ============================================================
void DisplayManager::_drawRecordingAnimation(float peakDB, uint32_t elapsedMs) {
    // ---- Header with blinking dot ----
    bool dotOn = (_animFrame % 8) < 4;
    _tft.fillRect(0, 0, TFT_W, HEADER_H, _tft.color565(30, 0, 8));
    _tft.drawFastHLine(0, HEADER_H - 1, TFT_W, TFT_RED);
    _tft.setTextColor(TFT_WHITE, _tft.color565(30, 0, 8));
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextSize(1);
    _tft.drawString("RECORDING", TFT_W / 2, HEADER_H / 2);
    _tft.fillCircle(18, HEADER_H / 2, 5,
                    dotOn ? (uint16_t)TFT_RED : _tft.color565(30, 0, 8));

    // ---- VU bar (left column) ----
    float    norm  = constrain((peakDB + 60.0f) / 60.0f, 0.0f, 1.0f);
    uint16_t fill  = (uint16_t)(norm * VU_W);
    uint16_t vuCol = (norm > 0.85f) ? (uint16_t)TFT_RED
                   : (norm > 0.65f) ? (uint16_t)COLOR_WARN
                   : (uint16_t)COLOR_ACCENT;

    _tft.fillRoundRect(VU_X, VU_Y, VU_W, VU_H, 3, _tft.color565(16, 16, 28));
    if (fill > 0)
        _tft.fillRoundRect(VU_X, VU_Y, fill, VU_H, 3, vuCol);
    _tft.drawRoundRect(VU_X, VU_Y, VU_W, VU_H, 3, _tft.color565(50, 50, 90));

    char dbStr[12];
    snprintf(dbStr, sizeof(dbStr), "%.1f dBFS", peakDB);
    uint16_t dbY = VU_Y + VU_H + 10;
    _tft.fillRect(VU_X, dbY - 7, VU_W, 14, COLOR_BG);
    _tft.setTextColor(_tft.color565(100, 110, 160), COLOR_BG);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextSize(1);
    _tft.drawString(dbStr, VU_X + VU_W / 2, dbY);

    // ---- Elapsed time ----
    uint32_t sec = elapsedMs / 1000;
    uint32_t cs  = (elapsedMs % 1000) / 10;
    char timeStr[10];
    snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu",
             (unsigned long)sec, (unsigned long)cs);
    uint16_t timeY = dbY + 36;
    _tft.fillRect(VU_X, timeY - 16, VU_W, 32, COLOR_BG);
    _tft.setTextSize(2);
    _tft.setTextColor(COLOR_ACCENT, COLOR_BG);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString(timeStr, VU_X + VU_W / 2, timeY);

    // ---- Progress bar ----
    uint16_t pbY = timeY + 26;
    uint16_t pbH = 8;
    uint32_t maxMs  = (uint32_t)MAX_RECORD_SECONDS * 1000;
    uint16_t pbFill = (uint16_t)((elapsedMs * VU_W) / maxMs);
    _tft.fillRoundRect(VU_X, pbY, VU_W, pbH, 3, _tft.color565(16, 16, 28));
    if (pbFill > 0)
        _tft.fillRoundRect(VU_X, pbY, pbFill, pbH, 3, COLOR_ACCENT);
    _tft.drawRoundRect(VU_X, pbY, VU_W, pbH, 3, _tft.color565(40, 40, 80));

    _tft.setTextSize(1);
    _tft.setTextColor(_tft.color565(50, 55, 80), COLOR_BG);
    _tft.fillRect(VU_X, pbY + pbH + 2, VU_W, 10, COLOR_BG);
    _tft.setTextDatum(ML_DATUM);
    _tft.drawString("0s", VU_X, pbY + pbH + 7);
    char maxStr[8];
    snprintf(maxStr, sizeof(maxStr), "%ds", MAX_RECORD_SECONDS);
    _tft.setTextDatum(MR_DATUM);
    _tft.drawString(maxStr, VU_X + VU_W, pbY + pbH + 7);

    // ---- Hint text ----
    uint16_t hintY = pbY + pbH + 24;
    _tft.fillRect(VU_X, hintY - 6, VU_W, 12, COLOR_BG);
    _tft.setTextColor(_tft.color565(40, 45, 70), COLOR_BG);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString("press knob to stop", VU_X + VU_W / 2, hintY);

    // ---- Oscilloscope waveform (right column) ----
    _tft.fillRect(WV_X, WV_Y, WV_W, WV_H, COLOR_BG);
    _tft.drawRect(WV_X, WV_Y, WV_W, WV_H, _tft.color565(20, 20, 40));
    // Centre line — subtle violet
    _tft.drawFastHLine(WV_X, WV_CY, WV_W, _tft.color565(20, 10, 45));

    float phase = (float)_animFrame * 0.18f;
    float amp   = norm * (WV_H / 2 - 4);
    uint16_t prevY = WV_CY;
    for (uint16_t px = 0; px < WV_W; px++) {
        float xn = (float)px / WV_W;
        float yn = sinf(phase + xn * 6.28f * 2.5f) * 0.60f
                 + sinf(phase * 1.4f + xn * 6.28f * 5.0f) * 0.25f
                 + sinf(phase * 0.8f + xn * 6.28f * 1.2f) * 0.15f;
        uint16_t cy = (uint16_t)constrain((int)(WV_CY - yn * amp),
                                          WV_Y + 1, WV_Y + WV_H - 2);
        if (px > 0)
            _tft.drawLine(WV_X + px - 1, prevY, WV_X + px, cy, COLOR_ACCENT);
        prevY = cy;
    }

    _drawFooter("RECORDING  —  press knob to stop");
}

// ============================================================
//  Private helpers
// ============================================================
void DisplayManager::_drawHeader(const char* title) {
    _tft.fillRect(0, 0, TFT_W, HEADER_H, COLOR_HDR_BG);
    // Bottom border: bright cyan line
    _tft.drawFastHLine(0, HEADER_H - 1, TFT_W, COLOR_ACCENT);
    _tft.setTextColor(COLOR_TEXT, COLOR_HDR_BG);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextSize(1);
    _tft.drawString(title, TFT_W / 2, HEADER_H / 2);
}

void DisplayManager::_drawFooter(const char* text) {
    uint16_t fy = TFT_H - FOOTER_H;
    _tft.fillRect(0, fy, TFT_W, FOOTER_H, COLOR_FTR_BG);
    _tft.drawFastHLine(0, fy, TFT_W, _tft.color565(0, 40, 80));
    _tft.setTextColor(_tft.color565(60, 70, 110), COLOR_FTR_BG);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextSize(1);
    _tft.drawString(text, TFT_W / 2, fy + FOOTER_H / 2);
}

// ============================================================
//  _spectrumColour()  —  Cyan → Blue → Violet gradient
//  norm = 0 → deep cyan, norm = 1 → bright violet/magenta
// ============================================================
uint16_t DisplayManager::_spectrumColour(float norm) {
    // Clamp
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    uint8_t r, g, b;

    if (norm < 0.33f) {
        // Cyan (0,220,255) → Blue-Cyan (0,120,255)
        float t = norm / 0.33f;
        r = 0;
        g = (uint8_t)(220 - t * 100);   // 220 → 120
        b = 255;
    } else if (norm < 0.66f) {
        // Blue-Cyan (0,120,255) → Indigo (80,0,255)
        float t = (norm - 0.33f) / 0.33f;
        r = (uint8_t)(t * 80);          // 0 → 80
        g = (uint8_t)(120 - t * 120);   // 120 → 0
        b = 255;
    } else {
        // Indigo (80,0,255) → Violet/Magenta (220,0,200)
        float t = (norm - 0.66f) / 0.34f;
        r = (uint8_t)(80  + t * 140);   // 80 → 220
        g = 0;
        b = (uint8_t)(255 - t * 55);    // 255 → 200
    }

    return _tft.color565(r, g, b);
}

// ============================================================
//  _catmullRom()  —  1D Catmull-Rom spline interpolation
//  Given 4 control values p0..p3 and t in [0,1] (the position
//  between p1 and p2), returns a smoothly interpolated value that
//  passes through p1 (t=0) and p2 (t=1) with continuous tangents —
//  this is what turns 64 discrete bar heights into a flowing,
//  sinusoidal-looking curve instead of a jagged polyline.
// ============================================================
float DisplayManager::_catmullRom(float p0, float p1, float p2, float p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * (
        (2.0f * p1) +
        (-p0 + p2) * t +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
    );
}

// ============================================================
//  _hsl()  —  Convert HSL to RGB565
//  h in [0,1), s in [0,1], l in [0,1]
// ============================================================
uint16_t DisplayManager::_hsl(float h, float s, float l) {
    float r, g, b;
    if (s < 0.001f) {
        r = g = b = l;
    } else {
        auto hue2rgb = [](float p, float q, float t) -> float {
            if (t < 0.0f) t += 1.0f;
            if (t > 1.0f) t -= 1.0f;
            if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
            if (t < 0.5f)      return q;
            if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
            return p;
        };
        float q = (l < 0.5f) ? (l * (1.0f + s)) : (l + s - l * s);
        float p = 2.0f * l - q;
        r = hue2rgb(p, q, h + 1.0f/3.0f);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1.0f/3.0f);
    }
    return _tft.color565((uint8_t)(r * 255), (uint8_t)(g * 255), (uint8_t)(b * 255));
}