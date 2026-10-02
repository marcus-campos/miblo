// Daily-life state that changes how every screen draws (like setMascotAccessory in ui_base.cpp):
// set by app.cpp when it changes, read by the screens. Strings are copied (cut when too long).
#include <stdio.h>

#include "ui_screens.h"

namespace screens {

static bool g_tie = false;
static uint8_t g_mood = 0;
static char g_label[37] = "";      // second clock: Config::tz2Label's size
static char g_hhmm[6] = "";        // "23:59"
static char g_countdown[64] = "";  // miblo::countdownLine
static char g_qr[32] = "";         // "http://192.168.100.200/"

void setMascotTie(bool on) { g_tie = on; }
bool mascotTie() { return g_tie; }

void setCatMood(uint8_t mood) { g_mood = mood; }
uint8_t catMood() { return g_mood; }

void setSecondClock(const char* label, const char* hhmm) {
  snprintf(g_label, sizeof(g_label), "%s", label ? label : "");
  snprintf(g_hhmm, sizeof(g_hhmm), "%s", hhmm ? hhmm : "");
}
const char* secondClockLabel() { return g_label; }
const char* secondClockTime() { return g_hhmm; }

void setDeskExtras(const char* countdownLine, const char* qrUrl) {
  snprintf(g_countdown, sizeof(g_countdown), "%s", countdownLine ? countdownLine : "");
  snprintf(g_qr, sizeof(g_qr), "%s", qrUrl ? qrUrl : "");
}
const char* deskCountdown() { return g_countdown; }
const char* deskQrUrl() { return g_qr; }

}  // namespace screens
