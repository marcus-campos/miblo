#pragma once
#include "platform.h"

// HTTP routes kept in flash. Each server.on() of ESP8266WebServer allocates a handler object
// (~64 B), a Uri copy (~24 B) and, for paths over 10 characters, the path's text, all on the heap
// for as long as the unit runs: ~3 KB for Miblo's ~30 routes. A module lists its routes in a
// table in flash instead and registers it once (one ~32 B handler on the heap per table).
// Matching is the same as server.on(): exact path and exact method, first match wins.
namespace routes {

using Fn = void (*)();

struct Route {
  char path[28];          // exact request path (the longest today: "/library/test/success.html")
  HTTPMethod method;      // HTTP_GET, HTTP_POST, ...
  Fn fn;                  // the request handler
  Fn upload = nullptr;    // file upload handler (multipart POST), as server.on(path, method, fn, upload)
};

// Registers a table of n routes; the table must stay alive (a static const array; in flash on
// the ESP8266, declared with PROGMEM).
void add(WebServerT& server, const Route* table, size_t n);

template <size_t N>
inline void add(WebServerT& server, const Route (&table)[N]) {
  add(server, table, N);
}

}  // namespace routes
