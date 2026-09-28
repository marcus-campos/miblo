#pragma once
// Differences between the ESP8266 and ESP32 Arduino cores, in one place. Only ESP8266 is
// compiled today; the ESP32 branch flags where a future board will need adjusting.
#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <Updater.h>
using WebServerT = ESP8266WebServer;
inline uint32_t hwRandom() { return RANDOM_REG32; }  // hardware generator
inline uint32_t chipId() { return ESP.getChipId(); }
inline uint32_t flashChipId() { return ESP.getFlashChipId(); }
inline uint32_t freeHeap() { return ESP.getFreeHeap(); }
inline uint32_t maxFreeBlock() { return ESP.getMaxFreeBlockSize(); }
inline String resetReason() { return ESP.getResetReason(); }
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
#else
#error "Miblo: unsupported platform (use ESP8266 or ESP32)"
#endif
