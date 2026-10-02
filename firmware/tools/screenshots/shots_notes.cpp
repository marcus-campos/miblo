// Screenshots of the desk note screens (see shots.h): the cat holding a message, a reminder, an
// alarm and "Time's up!", the timer, find; and the longest texts a sign must hold.
#include "shots.h"

namespace shots {

void renderNotes(miblo::Lang L) {
  using miblo::NoteKind;
  const bool pt = L == miblo::Lang::PtBR || L == miblo::Lang::PtPT;
  const screens::Clock clk = shots::clock();
  { Shot s; screens::note(L, NoteKind::Say, pt ? "volto em 10 min" : "back in 10 min", clk, 1000); save(s, "56-say"); }
  { Shot s; screens::note(L, NoteKind::Reminder, pt ? "ligar pro cliente" : "call the client", clk, 1000); save(s, "56-reminder"); }
  { Shot s; screens::note(L, NoteKind::Alarm, "daily", clk, 1000); save(s, "56-alarm"); }
  { Shot s; screens::note(L, NoteKind::Timer, "", clk, 1000); save(s, "56-times-up"); }
  { Shot s; screens::timer(L, clk, (6 * 60 + 42) * 1000, 10 * 60 * 1000, 1000); save(s, "56-timer"); }
  { Shot s; screens::findMe(L, "http://192.168.100.200/", 600); save(s, "56-find"); }
  // The limits of a text (40 characters, 47 bytes): the widest Latin letters, a sentence, CJK.
  { Shot s; screens::note(L, NoteKind::Say, "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW", clk, 1000); save(s, "56-say-40w"); }
  {
    Shot s;
    const char* text = pt ? "revisar o PR do Jo\xC3\xA3o antes da daily 1:1" : "review the PR before the 1:1 at 4 today!";
    screens::note(L, NoteKind::Reminder, text, clk, 1000);
    save(s, "56-reminder-40");
  }
  {
    Shot s;
    // 15 hanzi: 45 bytes.
    screens::note(L, NoteKind::Say, "\xE4\xB8\x80\xE4\xB8\x8B\xE4\xB8\x8A\xE4\xB8\xAD\xE5\xA4\xA7\xE5\xB0\x8F"
                  "\xE5\xA4\xA9\xE5\x9C\xB0\xE4\xBA\xBA\xE5\xB1\xB1\xE6\xB0\xB4\xE7\x81\xAB\xE6\x9C\xA8\xE6\x97\xA5"
                  "\xE6\x9C\x88", clk, 1000);
    save(s, "56-say-cjk");
  }
}

}  // namespace shots
