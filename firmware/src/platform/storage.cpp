#include "storage.h"

#include <ArduinoJson.h>
#include <string.h>

#include "lockouts.h"
#include "platform.h"

namespace storage {

static const char* kConfig = "/miblo/config.json";
static const char* kConfigTmp = "/miblo/config.tmp";
static const char* kTokens = "/miblo/pairs.json";
static const char* kTokensTmp = "/miblo/pairs.tmp";
// The pairings document: 4 entries (32-character token, a host <= 20 characters, order, flag).
static constexpr size_t kTokensJsonCapacity = 1024;
static const char* kBoot = "/miblo/boot.cnt";
// Recurring alarms and the countdown (miblo_desknotes.h), apart from the config.
static const char* kNotes = "/miblo/notes.json";
static const char* kNotesTmp = "/miblo/notes.tmp";
// The notes document: 4 alarms (texts < 48 B) and a countdown need ~650 B on the ESP8266 when
// read back (strings copied). Transient, on the heap, only at boot and when the notes change.
static constexpr size_t kNotesJsonCapacity = 768;
// Root markers, outside /miblo so that factory reset (which empties /miblo) never touches them.
static const char* kFsMarker = "/.miblo_fs";      // this LittleFS was initialised by Miblo
static const char* kConfigured = "/.configured";  // the unit was configured at least once
static bool configured = false;

static void touch(const char* path) {
  File f = LittleFS.open(path, "w");
  if (!f) return;
  f.write((uint8_t)'1');
  f.close();
}

bool begin() {
#if defined(ESP32)
  if (!LittleFS.begin(true)) {  // true = format if it can't mount
#else
  if (!LittleFS.begin()) {
#endif
    LittleFS.format();
    if (!LittleFS.begin()) return false;
  }
  if (!LittleFS.exists(kFsMarker)) {
    if (LittleFS.exists("/miblo")) {
      // Filesystem of an older Miblo build (before the root markers): keep it. Conservatively
      // treat a unit that already holds pairings or a saved network as configured.
      if (LittleFS.exists(kTokens) || WiFi.SSID().length() > 0) touch(kConfigured);
    } else {
      // First Miblo boot over another firmware's LittleFS: start from a clean filesystem so no
      // foreign files eat the 1 MB partition (and nothing stale is ever read as ours). Keyed on
      // the root marker, not on /miblo, so a factory reset (which empties /miblo) never formats.
      LittleFS.format();
      if (!LittleFS.begin()) return false;
    }
    touch(kFsMarker);
  }
  if (!LittleFS.exists("/miblo")) LittleFS.mkdir("/miblo");
  configured = LittleFS.exists(kConfigured);
  return true;
}

bool everConfigured() { return configured; }

void markConfigured() {
  if (configured) return;
  touch(kConfigured);
  configured = LittleFS.exists(kConfigured);
}

// The saved file, or its complete temporary copy when a power cut fell between removing the old
// file and renaming the new one (writeAtomically's fallback): a damaged copy fails to parse.
static File openSaved(const char* path, const char* tmp) {
  File f = LittleFS.open(path, "r");
  if (!f && LittleFS.exists(tmp)) f = LittleFS.open(tmp, "r");
  return f;
}

bool loadConfig(miblo::Config& cfg) {
  File f = openSaved(kConfig, kConfigTmp);
  if (!f) return false;
  DynamicJsonDocument doc(miblo::kConfigJsonCapacity);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  miblo::Config loaded;
  // A time zone saved by an older firmware that today's rules reject must not cost the user
  // every other setting: drop just that field (back to UTC; the settings page re-detects it), and
  // the same for the second clock's zone (back to off). Both may be rejected: up to two retries.
  for (int attempt = 0;; attempt++) {
    const char* bad = nullptr;
    if (miblo::applyConfigPatch(loaded, doc.as<JsonObjectConst>(), &bad)) break;
    if (attempt == 2 || !bad) return false;
    if (strcmp(bad, "tz") == 0) doc.remove("tz");
    else if (strcmp(bad, "tz2") == 0) doc.remove("tz2");
    else return false;
    loaded = miblo::Config();
  }
  miblo::restoreStored(loaded, doc.as<JsonObjectConst>());
  cfg = loaded;
  return true;
}

// A missing, damaged or oversized file leaves the notes empty (DeskNotes::fromJson skips any
// entry it does not trust).
bool loadNotes(miblo::DeskNotes& n) {
  File f = openSaved(kNotes, kNotesTmp);
  if (!f) return false;
  if (f.size() > 1024) {  // never ours: don't parse it
    f.close();
    return false;
  }
  DynamicJsonDocument doc(kNotesJsonCapacity);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  return n.fromJson(doc.as<JsonObjectConst>());
}

// Writes `doc` to `tmp` and renames it over `path`, so a power cut never leaves half a file
// behind, and a short write (a full or failing flash) never replaces a good file: the temporary
// file is kept only when every byte measureJson() promised was written.
static bool writeAtomically(const JsonDocument& doc, const char* path, const char* tmp) {
  const size_t want = measureJson(doc);
  if (want == 0) return false;
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  const bool ok = serializeJson(doc, f) == want;
  f.close();
  if (!ok) {
    LittleFS.remove(tmp);
    return false;
  }
  if (LittleFS.rename(tmp, path)) return true;
  LittleFS.remove(path);  // in case this LittleFS won't rename over an existing file
  return LittleFS.rename(tmp, path);
}

// A save builds its JSON document on the heap. When the heap is too low for it (with the Wi-Fi
// SDK's reserve) it is not even tried: the caller retries later (miblo::SaveRetry).
static bool heapFor(size_t capacity) { return !heapLowForRequest(capacity); }

bool saveNotes(const miblo::DeskNotes& n) {
  if (!heapFor(kNotesJsonCapacity)) return false;
  DynamicJsonDocument doc(kNotesJsonCapacity);
  if (doc.capacity() == 0) return false;
  n.toJson(doc.to<JsonObject>());
  if (doc.overflowed()) return false;
  return writeAtomically(doc, kNotes, kNotesTmp);
}

bool saveConfig(const miblo::Config& cfg) {
  if (!heapFor(miblo::kConfigJsonCapacity)) return false;
  DynamicJsonDocument doc(miblo::kConfigJsonCapacity);
  if (doc.capacity() == 0) return false;
  miblo::configToStored(cfg, doc.to<JsonObject>());
  if (doc.overflowed()) return false;  // never a config cut short
  return writeAtomically(doc, kConfig, kConfigTmp);
}

bool loadTokens(miblo::TokenStore& tokens) {
  File f = openSaved(kTokens, kTokensTmp);
  if (!f) return false;
  DynamicJsonDocument doc(kTokensJsonCapacity);
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;
  miblo::tokensFromJson(doc.as<JsonObjectConst>(), tokens);
  return true;
}

bool saveTokens(const miblo::TokenStore& tokens) {
  if (!heapFor(kTokensJsonCapacity)) return false;
  DynamicJsonDocument doc(kTokensJsonCapacity);
  if (doc.capacity() == 0) return false;
  miblo::tokensToJson(tokens, doc.to<JsonObject>());
  if (doc.overflowed()) return false;  // never a pairing list cut short
  return writeAtomically(doc, kTokens, kTokensTmp);
}

uint8_t readBootCount() {
  File f = LittleFS.open(kBoot, "r");
  if (!f) return 0;
  int v = f.read();
  f.close();
  return v < 0 ? 0 : (uint8_t)v;
}

void writeBootCount(uint8_t n) {
  File f = LittleFS.open(kBoot, "w");
  if (!f) return;
  f.write(n);
  f.close();
}

void factoryReset() {
  // Only the files under /miblo: the root markers (kFsMarker, kConfigured) must survive.
  LittleFS.remove(kConfig);
  LittleFS.remove(kConfigTmp);
  LittleFS.remove(kTokens);
  LittleFS.remove(kTokensTmp);
  LittleFS.remove(kBoot);
  LittleFS.remove(kNotes);
  LittleFS.remove(kNotesTmp);
  lockouts::clear();  // the code lockouts start over with the unit (reboots keep them)
  WiFi.persistent(true);
#if defined(ESP8266)
  WiFi.disconnect(true);  // with persistent(true), erases the SSID/password saved in the SDK
  ESP.eraseConfig();
#else
  WiFi.disconnect(true, true);
#endif
  delay(200);
  ESP.restart();
  while (true) delay(100);
}

}  // namespace storage
