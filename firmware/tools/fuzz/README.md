# Validation harness: sanitizers and fuzzing

The audit to run before a release, and again after merging a feature branch. All commands run
from `firmware/`.

| Command | What it does |
| --- | --- |
| `make asan` | Every native test suite (`pio test -e native_asan`) built with `-fsanitize=address,undefined -fno-sanitize-recover=undefined`. Any report fails the suite. |
| `make fuzz` | Builds `.pio/fuzz/miblo_fuzz` (ASan + UBSan) and runs every target `FUZZ_ITERS` times (default 200000, seed `FUZZ_SEED`=1). Then it runs `snapshot` again with `../fixtures/snapshots` as extra seeds. |
| `make fuzz FUZZ_TARGET=daily FUZZ_ITERS=1000000` | One target, more iterations. |
| `make fuzz-repro FUZZ_TARGET=screens FILE=.pio/fuzz/out/crash-screens-2661.bin` | Replays a saved input with the sanitizer report. |
| `make validate` | `asan`, then `fuzz`, then the plugin fuzz tests at scale 20 (`PLUGIN_FUZZ_SCALE`). |
| `MIBLO_LONGRUN_DAYS=61 MIBLO_LONGRUN_SEEDS=8 pio test -e native_asan -f test_longrun` | Long-run stability: every daily-life module driven together over 61 simulated days per seed (random sessions and alerts, API commands, late NTP, clock jumps and DST, Wi-Fi drops, stalls, two millis() wraps or reboots). The suite runs 3 days x 4 seeds; see `test/test_longrun`. |
| `make validate-clean` | Deletes the build output (`.pio/build/native_asan`, `.pio/fuzz`), about 25 MB. |

`.pio/fuzz/miblo_fuzz list` prints the targets. Run it directly for more options:
`miblo_fuzz <target|all> [-n N] [-seed S] [-corpus DIR]... [-seeds DIR] [-out DIR] [-timeout SEC]`.

## Driver

Apple clang ships without libFuzzer. Homebrew LLVM's libFuzzer built a binary that spun forever on
this macOS. So `driver.cpp` is a deterministic mutation fuzzer that needs neither. Each iteration
does the following:

1. It picks a seed (half the time), or a pool entry: the seeds plus mutants that got past parsing.
2. It applies 1 to 16 mutations. These include JSON-aware value and key swaps, interesting numbers
   (`1e999`, `NaN`, `4294967296`, ...), bit flips, dictionary tokens, splices, deep nesting and
   long runs.
3. It runs the target.

The random stream is seeded from `-seed` and the target name, so a run is reproducible. On a
crash, a sanitizer report, a failed invariant (`FUZZ_CHECK`) or a hang (5 s per input), the input
goes to `.pio/fuzz/out/crash-<target>-<n>.bin` (or `timeout-...`). The `deep` column is the share
of inputs that got past parsing into the logic. If it drops to around 0, the seeds or the mutator
need attention.

The target functions also build as a libFuzzer entry point (`-DMIBLO_LIBFUZZER
-DMIBLO_FUZZ_TARGET=\"snapshot\" -fsanitize=fuzzer`) wherever a working libFuzzer exists.

## Targets

| Target | Untrusted input | Main invariants |
| --- | --- | --- |
| `snapshot` | `POST /api/state` body → `parseSnapshot` (in place, exact-size buffer), then every consumer (format helpers, overview, RunTracker, LimitWatch, etaFor) | Fields terminated and within their character caps; pct ≤ 100; costs finite; oversize → `TooLarge` |
| `config` | `PATCH /api/config`, `POST /config` (one patch after another on the running settings, one per input line, plus the Wi-Fi portal's tz/lang patch) and a damaged `/config.json` (`loadConfig` retry logic) | A refused patch changes nothing; `badField` is a plain identifier; saved config fits `kConfigJsonCapacity` and loads back identical |
| `daily` | Scripts of `/api/focus`, `/api/meeting`, `/api/say`, `/api/remind`, `/api/timer`, `/api/countdown`, `/api/find` bodies (≤ 300 B, `StaticJsonDocument<384>`), with the clock moving (and wrapping) | Status ∈ {200, 400, 409}; error field is a plain identifier; replies and `notes.json` fit their documents; notes.json reloads identical |
| `notes` | `/notes.json` (≤ 1024 B, 768 B document) → `DeskNotes::fromJson` | Same as above, plus `countdownLine` in every language |
| `utf8` | `utf8Next`/`utf8Length`/`utf8Copy`, the `format*` helpers, `compareVersions`, `constantTimeEquals` | Always advances; copies stay in cap, are a prefix and never cut a sequence |
| `tz` | `tzLookup`/`tzLooksPosix`/`tzResolve`, `zoneHHMM`/`zoneLabel`, `parseDate`/`parseMonthDay`, `modeFromCode` | Outputs stay within cap |
| `friends_packet` | LAN UDP packets → `decodeFriendPacket` | A decoded packet re-encodes and decodes to the same fields |
| `friends_play` | Streams of packets, `update`/`demo`/identity changes into a `FriendPlay` | Every outgoing packet encodes and decodes; ≤ `kMaxFriends`; visit and greeting names bounded |
| `mdns` | mDNS queries → `mdnsRespond`, with any reply buffer size | Reply ≤ cap |
| `mdns_announce` | Any id/name/buffer sizes → `mdnsPublicIdentity` + `mdnsAnnounce` | Within cap |
| `http` | Raw header bytes (`findContentLength`, `checkRequestHeaders`: the multipart boundary guard; the header read-ahead `gatherHeaders`/`HeaderBuffer` over random segment sizes), `bearerToken`, `infoView`, TokenStore, PairingGuard, PresenceGate, WebSession | Within cap; read-ahead ≤ 2 KB, replayed unchanged, freed once drained; the refusal page fits 448 B |
| `canvas_text` | Any bytes drawn with the real `TftCanvas` and u8g2 fonts (every font, width, alignment) | No out-of-bounds glyph lookup |
| `ui_note` | Any text on the cat's sign (`wrap()`), fanfare, flash, hello, updateAvailable, paired | No crash or UB |
| `screens` | A fuzzed snapshot drawn by overview, sessions, limits, hero, desk, summary, limitReset, roam, dayEnd and weekRecap; then again with meeting mode, the second clock, the desk's countdown and QR from its text, and the daily screens and overlays (focus, nudge, timer, find, cue, state frame, fanfare, waiting mark, meeting badge, passerby) | No crash, UB or hang |
| `alerts` | Snapshots from two computers and an old plugin (sessions from a small id pool, alert records, bridge restarts) with settings, insistence, meeting and focus modifiers and the fanfare changing, into one `AlertSequencer` | Phase/level/queue bounded; a needs-you alert starts only for a waiting session; without reminders a wait alerts once; insistence only when on; nothing on screen past its length; with nothing on screen every waiting session has had its alert |

Inputs that once found a bug live in `corpus/<target>/` and run first, as seeds, on every
`make fuzz`. Add the crash file there when you fix something.

## Plugin

`plugin/test/fuzz.test.js` (node:test, no dependencies) is part of `npm test` at a small scale.
`make validate` runs it with `MIBLO_FUZZ_SCALE=20`. It covers:

- Random hook event and status line sequences through `pickEvent` → SessionTracker →
  MetricsStore/DayStats → `buildSnapshot`. It checks the following:
  - Nothing throws.
  - The snapshot is ≤ 6144 B and fits the firmware field caps.
  - There are ≤ 8 alerts, and each matches its session's state.
  - No session is left in perm/question after a main-thread Stop or UserPromptSubmit.
  - Planted private text never appears in the snapshot.
- A long stream of sessions and agents. Kept sessions stay bounded by the TTL.
- Raw HTTP requests at the bridge server: bad JSON, huge bodies, chunked bodies, a wrong
  Content-Type, Host/Origin variants and duplicate Host headers. The guard must hold and
  `/health` must keep answering.
