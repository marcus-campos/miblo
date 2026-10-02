#pragma once
// Differences between the ESP8266 and ESP32 Arduino cores, in one place. Only ESP8266 is
// compiled today; the ESP32 branch flags where a future board will need adjusting.
#include <Arduino.h>

// The largest program the flash layout takes (the settings page's "Program" bar): set by the
// board's env next to its linker script (platformio.ini); 0 = unknown (the bar stays empty).
#ifndef MIBLO_FW_MAX_BYTES
#define MIBLO_FW_MAX_BYTES 0
#endif

#if defined(ESP8266)
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <Updater.h>

#include "lookahead_client.h"
// The web server reads request headers ahead (lookahead_client.h); otherwise ESP8266WebServer.
using WebServerT = esp8266webserver::ESP8266WebServerTemplate<LookaheadServer>;
inline uint32_t hwRandom() { return RANDOM_REG32; }  // hardware generator
inline uint32_t chipId() { return ESP.getChipId(); }
inline uint32_t flashChipId() { return ESP.getFlashChipId(); }
inline uint32_t freeHeap() { return ESP.getFreeHeap(); }
inline uint32_t maxFreeBlock() { return ESP.getMaxFreeBlockSize(); }
inline String resetReason() { return ESP.getResetReason(); }
inline uint32_t ramTotal() { return 81920; }  // the ESP8266's data RAM: 80 KB (static data + heap)
inline uint32_t flashChipBytes() { return ESP.getFlashChipRealSize(); }
inline void fsUsage(uint32_t& used, uint32_t& total) {
  FSInfo i{};
  if (!LittleFS.info(i)) i.usedBytes = i.totalBytes = 0;
  used = i.usedBytes, total = i.totalBytes;
}
#elif defined(ESP32)
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
using WebServerT = WebServer;
inline uint32_t hwRandom() { return esp_random(); }
inline uint32_t chipId() { return (uint32_t)ESP.getEfuseMac(); }
inline uint32_t flashChipId() { return ESP.getFlashChipSize(); }
inline uint32_t freeHeap() { return ESP.getFreeHeap(); }
inline uint32_t maxFreeBlock() { return ESP.getMaxAllocHeap(); }
inline String resetReason() { return String((int)esp_reset_reason()); }
inline uint32_t ramTotal() { return ESP.getHeapSize(); }
inline uint32_t flashChipBytes() { return ESP.getFlashChipSize(); }
inline void fsUsage(uint32_t& used, uint32_t& total) { used = LittleFS.usedBytes(), total = LittleFS.totalBytes(); }
#else
#error "Miblo: unsupported platform (use ESP8266 or ESP32)"
#endif

// A big request (parsing a snapshot, rendering a page) must never drive the heap so low that the
// Wi-Fi SDK, which allocates its own packet buffers, is starved and faults. When the largest free
// block or the total free heap is below these, such a request is refused (503) instead. The Wi-Fi
// stack keeps well over 10 KB of headroom this way.
// The Wi-Fi SDK allocates its own packet buffers; if the heap is too low it faults. A request
// that needs `needBytes` of body/parse space is refused (503) when serving it would leave the SDK
// under this reserve. Smaller requests (e.g. an alerts-only snapshot) still get through when a
// full one would not, so an alert is never lost to low memory.
inline uint32_t kSdkHeapReserve() { return 12288; }
inline bool heapLowForRequest(uint32_t needBytes) {
  return maxFreeBlock() < needBytes + 4096 || freeHeap() < needBytes + kSdkHeapReserve();
}
