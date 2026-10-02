// Notes on the desk: the cat holding a text, the timer, find.
#include "ui_internal.h"
#include "ui_screens.h"

namespace screens {

void note(Lang lang, miblo::NoteKind kind, const char* text, const Clock& clk, uint32_t ms) {
  // Stub (daily-life foundation): track D draws it.
  (void)lang;
  (void)kind;
  (void)text;
  (void)clk;
  (void)ms;
}

void timer(Lang lang, const Clock& clk, uint32_t leftMs, uint32_t lenMs, uint32_t ms) {
  // Stub (daily-life foundation): track D draws it.
  (void)lang;
  (void)clk;
  (void)leftMs;
  (void)lenMs;
  (void)ms;
}

void findMe(Lang lang, const char* settingsUrl, uint32_t ms) {
  // Stub (daily-life foundation): track D draws it.
  (void)lang;
  (void)settingsUrl;
  (void)ms;
}

}  // namespace screens
