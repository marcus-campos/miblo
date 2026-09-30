#pragma once
#include <ArduinoJson.h>

// Field diagnostics: when the unit crashes (exception or watchdog), the ESP8266 core calls
// custom_crash_callback before restarting. It keeps the cause and the code addresses found on the
// stack in RTC memory (it survives the restart), and GET /api/info reports them as "crash", to be
// decoded against the release's .elf with xtensa-lx106-elf-addr2line.
namespace crashlog {

// Adds "crash": {reason, exccause, epc1, excvaddr, addrs: [...]} when the last restart was a crash.
void report(JsonObject info);

}  // namespace crashlog
