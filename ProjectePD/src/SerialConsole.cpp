// ============================================================
//  SerialConsole.cpp  —  Interactive terminal (ANSI colours)
// ============================================================
#include "SerialConsole.h"
#include "config.h"
#include <string.h>
#include <ctype.h>

// ANSI escape codes
#define ANSI_RESET   "\033[0m"
#define ANSI_BOLD    "\033[1m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_RED     "\033[31m"
#define ANSI_GREY    "\033[90m"
#define ANSI_MAGENTA "\033[35m"

SerialConsole::SerialConsole() : _pos(0) {
    memset(_line, 0, sizeof(_line));
}

void SerialConsole::begin(uint32_t baud) {
    Serial.begin(baud);
    delay(500);
    printBanner();
}

Cmd SerialConsole::poll() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            _line[_pos] = '\0';
            _pos = 0;
            if (strlen(_line) == 0) return Cmd::NONE;
            Serial.printf(ANSI_GREY "> %s\n" ANSI_RESET, _line);
            Cmd cmd = _parse(_line);
            memset(_line, 0, sizeof(_line));
            return cmd;
        }
        if (_pos < sizeof(_line) - 1) {
            _line[_pos++] = tolower(c);
        }
    }
    return Cmd::NONE;
}

void SerialConsole::printBanner() {
    Serial.println();
    Serial.println(ANSI_CYAN ANSI_BOLD
        "╔══════════════════════════════════════════════╗\n"
        "║   ESP32-S3  Audio Spectrum Visualizer  v2.0 ║\n"
        "║   INMP441 I2S  •  ST7789V 2.0\" TFT          ║\n"
        "╚══════════════════════════════════════════════╝"
        ANSI_RESET);
    printHelp();
}

void SerialConsole::printHelp() {
    Serial.println(ANSI_CYAN
        "\n  Rotary encoder controls:\n"
        "    Short press  = start/stop recording  (IDLE/RECORDING)\n"
        "    Rotate       = step through snapshots (PLAYBACK)\n"
        "    Long hold    = cycle viz mode         (PLAYBACK)\n"
        "    Short press  = confirm re-record      (PLAYBACK, 2-step)\n"
        "\n  Serial commands:\n"
        "  " ANSI_BOLD "start" ANSI_RESET ANSI_CYAN "   — begin audio recording (max ");
    Serial.print(MAX_RECORD_SECONDS);
    Serial.println(ANSI_CYAN "s)\n"
        "  " ANSI_BOLD "stop" ANSI_RESET ANSI_CYAN "    — stop recording & show spectrum\n"
        "  " ANSI_BOLD "prev" ANSI_RESET ANSI_CYAN "    — step to previous snapshot\n"
        "  " ANSI_BOLD "next" ANSI_RESET ANSI_CYAN "    — step to next snapshot\n"
        "  " ANSI_BOLD "view" ANSI_RESET ANSI_CYAN "    — cycle visualization mode (bars/wave/radial)\n"
        "  " ANSI_BOLD "status" ANSI_RESET ANSI_CYAN "  — print memory & recorder state\n"
        "  " ANSI_BOLD "reset" ANSI_RESET ANSI_CYAN "   — discard recording & return to idle\n"
        "  " ANSI_BOLD "help" ANSI_RESET ANSI_CYAN "    — show this message\n"
        ANSI_RESET);
    Serial.printf(ANSI_CYAN
        "\n  Recording is divided into %d fixed snapshots\n"
        "  (e.g. %ds clip → 1s, 2s, ... %ds).\n" ANSI_RESET,
        SNAPSHOT_COUNT, MAX_RECORD_SECONDS, MAX_RECORD_SECONDS);
}

void SerialConsole::printStatus(const char* state, uint32_t samples,
                                 uint32_t capBytes, uint32_t freeHeap) {
    Serial.printf(ANSI_CYAN
        "\n  State   : " ANSI_BOLD "%s\n" ANSI_RESET ANSI_CYAN
        "  Samples : %lu  (%.2f s @ %d Hz)\n"
        "  Buffer  : %.1f KB / %.1f KB used\n"
        "  FreeHeap: %.1f KB\n" ANSI_RESET "\n",
        state,
        (unsigned long)samples,
        (float)samples / SAMPLE_RATE,
        SAMPLE_RATE,
        (float)capBytes / 1024.0f,
        (float)(MAX_SAMPLES * BYTES_PER_SAMPLE) / 1024.0f,
        (float)freeHeap / 1024.0f);
}

void SerialConsole::printInfo(const char* msg) {
    Serial.printf(ANSI_GREY "  [info] %s\n" ANSI_RESET, msg);
}

void SerialConsole::printError(const char* msg) {
    Serial.printf(ANSI_RED "  [ERR]  %s\n" ANSI_RESET, msg);
}

void SerialConsole::printOk(const char* msg) {
    Serial.printf(ANSI_GREEN "  [OK]   %s\n" ANSI_RESET, msg);
}

// ---- Private -------------------------------------------------

Cmd SerialConsole::_parse(const char* line) {
    if (strcmp(line, "start")  == 0) return Cmd::START;
    if (strcmp(line, "stop")   == 0) return Cmd::STOP;
    if (strcmp(line, "status") == 0) return Cmd::STATUS;
    if (strcmp(line, "reset")  == 0) return Cmd::RESET;
    if (strcmp(line, "help")   == 0) return Cmd::HELP;
    if (strcmp(line, "prev")   == 0) return Cmd::PREV;
    if (strcmp(line, "next")   == 0) return Cmd::NEXT;
    if (strcmp(line, "view")   == 0) return Cmd::VIEW;

    Serial.printf(ANSI_YELLOW "  Unknown command: '%s'  (type 'help')\n"
                  ANSI_RESET, line);
    return Cmd::NONE;
}
