#pragma once
// ============================================================
//  SerialConsole.h  —  Interactive terminal command handler
// ============================================================
#include <Arduino.h>

enum class Cmd { NONE, START, STOP, STATUS, RESET, HELP, PREV, NEXT, VIEW };

class SerialConsole {
public:
    SerialConsole();

    void begin(uint32_t baud = 115200);

    // Call from loop() — returns parsed command if a full line arrived.
    Cmd poll();

    // Pretty-print helpers
    void printBanner();
    void printHelp();
    void printStatus(const char* state, uint32_t samples,
                     uint32_t capBytes, uint32_t freeHeap);
    void printInfo(const char* msg);
    void printError(const char* msg);
    void printOk(const char* msg);

private:
    char     _line[64];
    uint8_t  _pos;

    Cmd _parse(const char* line);
};
