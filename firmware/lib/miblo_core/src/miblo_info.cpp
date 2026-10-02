#include "miblo_info.h"

namespace miblo {

InfoView infoView(const TokenStore& tokens, const char* authHeader) {
  if (tokens.count() == 0) return InfoView::Full;
  char token[40];
  if (!bearerToken(authHeader, token, sizeof(token))) return InfoView::Public;
  return tokens.matches(token) ? InfoView::Full : InfoView::Public;
}

void writePublicInfo(JsonObject doc, const char* id, bool paired, int proto) {
  doc["id"] = id;
  doc["paired"] = paired;
  doc["proto"] = proto;
}

void writeSystemInfo(JsonObject doc, const SystemStats& s) {
  doc["cpu"] = s.cpu;
  doc["mhz"] = s.mhz;
  doc["ramUsed"] = s.ramUsed;
  doc["ram"] = s.ram;
  doc["fw"] = s.fw;
  doc["fwMax"] = s.fwMax;
  doc["fsUsed"] = s.fsUsed;
  doc["fs"] = s.fs;
}

}  // namespace miblo
