// Firmware mínimo "seguro" (Task 1): tela + Wi-Fi salvo no SDK + /update sem senha, igual ao
// firmware de referência. Toda build do Miblo mantém uma página /update; este main é
// substituído na Task 11 (demo da tela) e na Task 13 (aplicativo completo).
#include <Arduino.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <TFT_eSPI.h>

#include "miblo_version.h"

TFT_eSPI tft;
ESP8266WebServer server(80);
ESP8266HTTPUpdateServer updater;

void setup() {
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(2);
  tft.drawString("Miblo " MIBLO_FW_VERSION, 120, 100, 1);
  tft.setTextSize(1);

  WiFi.mode(WIFI_STA);
  WiFi.begin();  // credenciais salvas no SDK pelo firmware anterior
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Miblo-Recovery");
  }
  updater.setup(&server, "/update");
  server.begin();
  String ip = (WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  tft.drawString("http://" + ip + "/update", 120, 130, 1);
}

void loop() { server.handleClient(); }
