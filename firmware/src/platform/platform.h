#pragma once
// Diferenças entre os cores Arduino do ESP8266 e do ESP32, num só lugar. Só o ESP8266 é
// compilado hoje; o ramo ESP32 marca onde uma placa futura precisa de ajuste.
#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <Updater.h>
using WebServerT = ESP8266WebServer;
inline uint32_t hwRandom() { return RANDOM_REG32; }  // gerador de hardware
inline uint32_t chipId() { return ESP.getChipId(); }
inline uint32_t flashChipId() { return ESP.getFlashChipId(); }
#elif defined(ESP32)
#include <LittleFS.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
using WebServerT = WebServer;
inline uint32_t hwRandom() { return esp_random(); }
inline uint32_t chipId() { return (uint32_t)ESP.getEfuseMac(); }
inline uint32_t flashChipId() { return ESP.getFlashChipSize(); }
#else
#error "Miblo: plataforma não suportada (use ESP8266 ou ESP32)"
#endif
