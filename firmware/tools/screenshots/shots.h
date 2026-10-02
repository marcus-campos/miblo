#pragma once
// What the screenshot tool's per-feature files share (main.cpp defines it): the sample snapshot and
// its builders, the clock, and Shot/save() to draw and write one PNG. Each daily-life feature draws
// its screens in its own shots_<feature>.cpp, called at the end of main.cpp's renderAll().
//
//   void shots::renderFocus(Lang L) {
//     shots::working();
//     { shots::Shot s; screens::focus(L, shots::clock(), ...); shots::save(s, "50-focus-focus"); }
//   }
//
// Call clock() qualified (shots::clock): unqualified it clashes with the C library's clock().
#include <stdint.h>

#include <string>

#include "TFT_eSPI.h"
#include "fonts.h"
#include "miblo_i18n.h"
#include "miblo_snapshot.h"
#include "platform/tft_canvas.h"
#include "ui_screens.h"

namespace shots {

extern uint32_t gNow;        // "now" in Unix seconds: 2026-09-29 14:32 local time (America/Sao_Paulo)
extern miblo::Snapshot snap;  // the sample snapshot the builders below fill

// Appends a session to `snap` that has been in `st` for `ago` seconds.
void session(const char* id, const char* name, miblo::SessionState st, const char* tool, const char* det,
             uint32_t ago, int ctx = 42, int64_t tok = 186000, const char* model = "Opus");
// Clears `snap` and sets the limits (5h, week) and today's cost.
void usage(uint8_t h5, uint8_t d7);
void attention();  // sessions waiting (permission, question), one running, one done
void working();    // three running (one is "worker" running Bash "npm test"), one done
void idle();       // all done or idle
screens::Clock clock();  // 14:32 at gNow

// Drawn like on the gadget: through a ShiftCanvas (no shift here), which applies the blue light
// filter when `warmth` > 0.
struct Shot {
  TFT_eSPI tft{240, 240};
  TftCanvas canvas{tft, {240, 240}, board::fonts::kStacks};
  ui::ShiftCanvas shifted{canvas};
  explicit Shot(uint8_t warmth = 0) {
    canvas.begin();
    shifted.setWarmth(warmth);
    screens::bind(shifted);
    screens::reset();
  }
};
// Writes <lang dir>/<name>.png and <name>@4x.png, and checks the margins (make check-margins).
void save(Shot& s, const std::string& name);

// The overlays shared by every daily-life screen (shots_daily.cpp): the waiting mark.
void renderDaily(miblo::Lang L);
// One per daily-life feature (shots_<feature>.cpp).
void renderFocus(miblo::Lang L);      // focus (Pomodoro)
void renderAlerts(miblo::Lang L);     // meeting mode, insistence, fanfare
void renderDayRhythm(miblo::Lang L);  // wellness, end of the day, Monday recap
void renderNotes(miblo::Lang L);      // say, reminders, timer, countdown, find
void renderCues(miblo::Lang L);       // strong cue, status frame
void renderLook(miblo::Lang L);       // special days, meeting tie, extras
void renderPets(miblo::Lang L);       // every pet: looks, dress, colours, main screens (70-pet-*)

}  // namespace shots
