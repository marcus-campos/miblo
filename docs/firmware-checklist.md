# Firmware manual on-device checklist

This is the human-executed, on-device checklist for validating a Miblo firmware release. It
cannot be run by an agent: it requires a real device, a real Wi-Fi network, and a phone to scan
QR codes. A person should run every item below and record the result of each in the PR or release
notes.

## Before you start

- Computer on the same network as the gadget; Claude Code with the Miblo plugin installed.
- Note the gadget's current IP (shown on the reference-firmware screen).
- **Use a proper power supply.** A weak USB supply (phone charger, laptop port, hub without
  external power) can brown out the device under load. Six consecutive brown-out resets look
  identical to the intentional "6 quick power cycles" factory-reset sequence
  (checklist item 10) and can trigger an unwanted factory reset. Power the gadget from a proper
  5V/1A (or better) supply for all of the steps below, especially the OTA and stress items.
- **Keep the stock GeekMagic `.bin` before flashing.** Back up the device's original/stock
  firmware image (or confirm you already have a copy) before flashing `firmware/dist/miblo-*.bin`
  over it, so the device can be restored if a release build needs to be rolled back.

## Checklist

1. **First flash (two-stage, from the stock firmware):** open `http://<ip>/update` in the
   browser and upload `firmware/dist/miblo-loader-geekmagic_ultra-0.1.0.bin` (the full image does
   not fit the stock firmware's OTA space: "Not Enough Space"). Expected: the device reboots and
   shows "Miblo installer", the version and `http://<ip>/update`; `GET http://<ip>/info` returns
   `{"app":"miblo-loader",...}`. Then open that `/update` and upload
   `firmware/dist/miblo-geekmagic_ultra-0.1.0.bin` (the installer screen shows "Installing Miblo..." without
   progress; the browser's upload finishes with "OK"), then the device reboots and shows the blinking mascot (boot screen) with the firmware version near the bottom.
   After installation the unit boots in Miblo-Setup mode: the installer erased the bench Wi-Fi
   after the verified upload, so `Miblo-Setup-XXXX` and its QR code appear. Uploading to
   `/update?keepwifi=1` keeps the Wi-Fi instead; a failed upload (e.g. a truncated or wrong file:
   "Update error: ...") erases nothing and the installer stays up.
   Also verify once with the router off: after ~20 s the installer shows the open
   `Miblo-Installer-XXXX` network and `http://192.168.4.1/update`; a phone joined to that network
   stays connected (no automatic station retries), and with nobody on the AP it rejoins the saved
   Wi-Fi within ~3 min of the router coming back.
   Bench shortcut: `python3 firmware/scripts/flash-fleet.py --host <ip>` runs both stages and checks `/api/info` (one flash per release is still done by hand as above).
2. **Saved Wi-Fi:** after joining a network through the portal (or installing with
   `?keepwifi=1`), reboot: with no interaction, the boot screen gives way to the "Wi-Fi connected"
   welcome screen, which shows a QR code pointing to `https://github.com/marcus-campos/miblo`
   (the repo README has the install commands) above the `/plugin install miblo@miblo` label, the
   4-digit pairing code, and the IP — the SDK's saved Wi-Fi credentials were reused; no
   `Miblo-Setup-…` network appears.
3. **Page:** `http://miblo-xxxx.local` (or by IP) opens the config page in the browser's language;
   the time zone select lists IANA names (`GET /api/zones`) and preselects the browser's zone
   (saved automatically the first time); switching the language to `pt-BR` changes the screen.
4. **Pairing:** `/miblo:pair` finds `Miblo-XXXX`, asks for the code shown on screen, then shows
   "Paired with <host>" for 5 s, then "Disconnected"/clock until the first snapshot. `GET
   http://<ip>/api/info` with no token now shows only `{id, paired: true, proto}`; with the pairing
   token (`-H "Authorization: Bearer <token>"`, from the plugin's devices.json) it shows
   `board: "geekmagic_ultra"`, `screen: {w:240,h:240}`, `caps: []`. The settings page shows only an
   unlock button until the on-screen code is typed. Five wrong codes in a row cause a 429 for 60 s.
5. **Screens:** with real sessions — Working (compact `5h ▓░ 30%  7d ▓░ 13%` strip on top, up to
   3 session cards with name, time in state and current activity, footer "N RUNNING · 1/2" +
   clock; with 4+ sessions the cards page every 5 s), Needs you (amber band, compact strip, pending
   cards first in amber; request something that needs a permission), All done (nothing running:
   big 5h/week limits, last finished session, today's cost). No text has a dark box/halo around it
   (big "30%" digits, the amber flash name). Times in state read "<1m", "3m", "1h12" and change at
   most once a minute. Nothing blinks while you watch for a minute: the clock, timers and
   countdowns change in place (no dark flash of their area), and cards only redraw when they
   page or change state. `/miblo:mode limits` shows the limits arc; `/miblo:mode sessions` shows
   the session list — at most 3 big cards per page (state dot + name in bold + time in state on
   line 1, the activity on line 2, cut with "..."; no model/ctx/tokens line; pending cards
   amber), readable at arm's length, with "1/2" in the header and paging every 5 s when there are
   more than 3 sessions; `/miblo:mode overview` returns to the adaptive view.
6. **Alerts:** permission request causes an amber flash (~1.5 s), a hero screen (~10 s) with the
   command and "waiting for …", then a summary with the band; no response for 2 min repeats the
   alert; approving it clears the band. `Stop` causes a blue flash and a hero screen (~5 s, with
   duration and ctx/tokens), then "finished" in the list. Discreet mode on the config page hides
   commands and file paths.
7. **Disconnected:** close Claude Code/the bridge; after 30 s the device shows a "Disconnected"
   screen with the correct clock (NTP + timezone) and the pairing code in the footer.
8. **Wrong password:** factory-reset (item 10) and, on the setup portal, type the wrong Wi-Fi
   password; the device shows a "Wrong password" screen with the QR code; correcting it connects.
9. **Recovery:** power off the router; after ~2 minutes the `Miblo-Setup-XXXX` network and QR
   appear; power the router back on; the gadget returns on its own to the saved network and the
   setup network disappears.
10. **Power-cycle reset (no 3-cycle reset):** six quick power-on cycles in a row,
    each under 10 s of uptime. From the 3rd through the 5th quick boot, the screen shows an amber
    countdown ("N more quick restarts to reset" / "leave it on to cancel"); leaving the device
    powered on for 10 s at any point clears the counter and cancels the reset (verify this
    explicitly: interrupt the sequence once and confirm it does **not** reset). On the 6th
    consecutive quick boot, the device performs a full factory reset (erases Wi-Fi, pairings,
    config) and returns to the setup QR; the phone that scans the QR joins the network and the
    portal opens on its own in the phone's language.
11. **Own OTA:** open `http://<ip>/update`; the screen shows the 4-digit code (generated on
    load). Submit the same `.bin` with the wrong code: "Wrong code" and nothing changes. Submit
    with the correct code: a progress bar on screen, "OK", reboot, pairing preserved. The device's
    own `/update` requires the on-screen presence code, including when a Bearer token is also
    sent.
12. **OTA on the setup network:** with the gadget on the `Miblo-Setup-XXXX` AP,
    `http://192.168.4.1/update` also works, with the same code-based flow; only a never-configured unit (never joined a Wi-Fi submitted through the portal, never paired;
    no saved Wi-Fi and no pairings) accepts OTA from its own setup AP without the code. After a
    factory reset (web page, `/api/reset` or six quick power-ons) a unit that was configured once
    still requires the code on its setup AP.
13. **Extended-use health check:** after using the device normally for at least 10 minutes
    (receiving snapshots, switching modes, triggering a couple of alerts), request
    `GET http://<ip>/api/info` again (with the pairing token) and inspect the heap fields. Confirm free heap and max
    contiguous free block (`maxBlock`) are stable — not trending toward zero or badly fragmented —
    compared to a reading taken right after boot. A steadily shrinking heap or a `maxBlock` far
    smaller than free heap indicates a leak or fragmentation that should block the release.
14. **Quiet cycle:** with every session finished, "All done" stays 20 s, then the desk mascot with
    both limit rings (and their reset times), 15 s of Limits arc, the mascot again, 15 s of
    today's summary (responses, time worked, cost matching `/miblo:status`), and around. Any new
    prompt brings the Overview back at once; an alert interrupts any of them.
15. **Limit forecast and "limit freed":** while working steadily at 50%+ of the 5-hour window,
    the Limits arc and the desk ring show "runs out in …" in amber once the pace would exhaust it
    before the reset. When the window resets after 50%+ use, the green "LIMIT FREED" screen with
    the celebrating mascot shows for ~8 s, then the normal screens return.
16. **Mascot colours and settings command:** `/miblo:settings` opens the settings page; each
    mascot colour saves and applies at once (boot, desk and disconnected mascots), and survives a
    reboot. Night mode dims and restores the backlight at the configured times.
    Blue light filter: set "Always" at each strength and check that the whole screen (text,
    mascot, rings, pet mode) warms at once and stays readable. Set "Scheduled" with a start a
    minute or two ahead and watch the boundary: the whole screen redraws warm at that minute (and
    back at the end time). Reboot inside the window: it comes back warm once the clock is set.
    Set "Off": the original colours return everywhere.
17. **Screen care:** leave the gadget with Claude Code closed: after ~30 s "Disconnected", after
    `petMin` minutes (15 by default; 1 min to 1 h on the settings page) the mascot wanders around
    the whole screen (no trail, clock under it), and with the default "1 h" the panel goes dark
    (backlight off) at the one-hour mark. Opening the settings page, `/miblo:pair` (mDNS
    discovery) or any Claude Code activity lights it again with the normal screens. With
    "Never", pet mode keeps going. Over an hour, the whole picture shifts by 1-2 px every 5
    minutes.
18. **Update notice:** with an older firmware than the latest release, restart the gadget with
    Claude Code open: "Update available · vX (you have vY)" shows for ~5 s once, then the normal
    screens. On the settings page, **Check for updates** reports "Up to date" on the latest
    firmware, or the new version with a link to its .bin. `/miblo:pair` on an outdated gadget
    offers the update right after pairing.
19. **Pet antics:** in pet mode, watch for ~10 minutes: the mascot goes through its 30 antics
    (for example spilling its coffee). The sign it carries lies on the floor while it plays and
    the mascot does not blink during an antic. Interplay of `petMin` and `sleepMin`: with
    `sleepMin` above `petMin` the panel goes dark `sleepMin - petMin` minutes after pet mode
    starts; with `sleepMin` equal or below `petMin` it goes dark 15 min after pet mode starts
    (never before it shows); `sleepMin` "Never" keeps pet mode going.
20. **Visits (two or more Miblos):** run `/miblo:demo` and watch the first visit start within
    about 10 s. Over a few visits, see the 30 new activities besides the original ones (37 kinds
    of visit in all, from the duck and the code review to ping-pong, a picnic and a kite);
    guests walk in from the side set on the settings page. Let one Miblo's panel go dark (or set it to sleep soon): no visit starts into it, and a visit in
    progress ends with the guests going home. `/miblo:demo stop` ends the demo early.
21. **Setup-portal Wi-Fi code:** on a configured unit (saved Wi-Fi or paired), the setup network's
    portal asks for the on-screen code before it joins a network; a wrong code is refused. While
    another code is on the screen (an update or a reset) a second request gets a "busy" reply
    (429 `{"error":"busy"}` with `retryAfter`) and does not replace the code. After a factory
    reset the unit is fresh again and the portal asks for no code.
22. **Settings-page lock and reduced public info:** on a paired gadget, `http://miblo-xxxx.local`
    shows only an unlock button until the on-screen code is typed; then the owner, birthday and
    settings load. Without the token, `GET /api/info` returns only `{id, paired, proto}` and
    mDNS advertises only the id-based `Miblo-XXXX` (no name). Flood `/api/info` without a token
    until it answers 429, then check that a request with the pairing token still answers 200
    (`/miblo:status` keeps working).
23. **System panel:** on the settings page, open **Advanced**: the Processing and Memory (RAM)
    graphs fill from the right once a second (the last minute only) and the processing load reads
    a few percent while idle; Storage shows the data in use and the firmware size with the room
    left for updates. Closing Advanced stops the reads (no more `/settings-system` requests), and
    on a paired gadget a browser without the on-screen code gets 401 from `/settings-system`.
24. **Paired computers:** on the settings page, open **Advanced**: each paired computer is
    listed with its host name and "active now" (the one sending snapshots) or how long ago it
    was seen; opened through `/miblo:settings`, this computer is marked "(this computer)".
    Remove another computer: after the confirmation it leaves the list, its `/miblo:status`
    shows `unauthorized: true`, and `/miblo:pair` brings it back. Removing the last one leaves
    the gadget unpaired (the page reloads open).
25. **Daily life (firmware 1.11):** each part below, on one gadget, with the plugin from the same branch.

<!-- daily:focus -->

<!-- daily:alerts -->

<!-- daily:rhythm -->

<!-- daily:notes -->
    - **Say:** `/miblo:say "back in 10 min"`: the cat holds it on a violet sign over Main, Desk,
      Summary and Disconnected, and the panel doesn't go dark while it is up; in pet mode it shows
      on the pet's card instead. It goes away after 30 min, or `/miblo:say off`. A 40-character
      text of `W`s and 15 Chinese characters both fit on the sign without being cut; 41 characters
      or a control character is refused by the CLI.
    - **Remind:** `/miblo:remind 1 "call the client"`: one minute later the screen pulses and the
      cat holds it on an amber sign; `/miblo:remind off` puts it down (otherwise after 5 min).
      `/miblo:remind 16:30 "daily"` with the time known fires at 16:30 (tomorrow if 16:30 has
      passed). Right after a power cycle with no Wi-Fi, it answers that the gadget has no time
      yet. A permission request while a reminder is up still takes the screen.
    - **Recurring alarms:** `/miblo:remind every day HH:MM "standup"` (two minutes ahead) and
      `/miblo:remind weekdays ...`: `/miblo:remind` lists them with their ids; power-cycle the
      gadget and list again: they are still there, and the alarm fires at its minute, once.
      `/miblo:remind off N` deletes one. A fifth one of either kind is refused as full.
    - **Timer:** `/miblo:timer 2`: the cat watches an hourglass with the time left and a bar; at
      the end the screen pulses and the cat holds "Time's up!" on a green sign. `/miblo:timer
      stop` cancels it; an alert during the timer shows and the timer comes back after it.
    - **Countdown:** `/miblo:countdown "release" 15/10`: the Desk shows "release in N days"
      ("tomorrow", "is today!"); it survives a power cycle and `/miblo:countdown off` removes it.
    - **Daylight saving (expected, not a bug):** a recurring alarm at a time the clock skips
      when it springs forward (e.g. 02:30) does not fire that day; one at a time that repeats
      when the clock falls back fires once. A one-off `remind HH:MM` is counted in minutes from
      when it was set, so it fires 1 h early or late if the clock changes before it is due.
    - **Find:** `/miblo:find`: for 10 s the top and bottom bands pulse, the cat waves, and the QR
      code opens the settings page on a phone on the same Wi-Fi.

<!-- daily:cues -->
    - **Status frame:** turn on the frame on the settings page. With a permission pending, a
      thin amber frame surrounds every screen (not over the alert flash); answer it and the
      frame goes away with no leftovers. When a session finishes, the frame is green for 1 min,
      then disappears. With the frame off, nothing is drawn.
    - **Strong cue:** start a 1 min timer (`/miblo:timer 1`): when it ends, the whole screen
      pulses amber 3 times, slowly and without visible tearing or a blinking icon, and the
      backlight goes to full; then the held text follows and the brightness comes back. With
      night mode on (inside its window), the pulses are at most twice the night brightness and do
      not dazzle; with the blue light filter on, the pulse colour is warmed. A permission that
      arrives during the pulses takes the screen at once.

<!-- daily:look -->
    - **Look:** the special days follow the gadget's own date (NTP), so check them in the screenshots (`make screenshots`): `58-look-bunny`, `58-look-glasses`, `58-look-hearts`, `58-look-tie`, `58-look-headphones`, `58-look-tired`, `58-black-cat` and `58-black-cat-look`, and the three `58-look-sheet-*` (every piece on all four mascot colours and with each hat; look at the `@4x` PNGs). Nothing is cut off, and only the glasses sit over the eyes. On the gadget: `/miblo:meeting 2` puts a tie on the desk mascot (and on the pet), and it comes off when the meeting ends; on a day with more than 8 h of Claude working, the mascot has faint bags under its eyes.

<!-- daily:screens -->

<!-- daily:settings -->
    - **Settings page:** open the unlocked page with the browser language set to en, pt-BR, ru and zh: every new label is translated and nothing overflows on a phone-width window.
      Under Alerts, turn alerts off: insistence and the fanfare hide, the status frame stays. Pick each fanfare choice (off, 3, 5, 10 min) and save; reloading the page and `/miblo:status` (or `GET /api/info`) show the new value.
      In **Wellness**, change the break (off/60/90/120), water (off/60/90), eye rest, focus filter, end-of-day and weekly switches, save, and reload: each sticks.
      The work hours block shows only while the water reminder or the end-of-day summary is on. Set 18:00 to 09:00: saving is refused and the start or end time is outlined in red. Untick every work day: the last one ticks itself again. Save Mon, Wed, Fri: `workDays` reads 42.
      Under This device, the second time zone starts at "Off" (the browser's zone is never preselected); pick a city: its time shows next to the label and the "Its name on screen" field appears (12 characters at most). Save, reload: the zone and its name are kept. Set it back to Off and save: `tz2` is empty. Turn the desk QR on and save: the Desk screen shows the QR.

<!-- daily:bridge -->
