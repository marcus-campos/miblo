#include "routes.h"

namespace routes {

#if defined(ESP8266)
namespace {

// One request handler for a whole table of routes in flash. Same contract as the core's
// FunctionRequestHandler: canHandle() = exact method and path; an upload goes to the route's
// upload function only when it has one and the request is a POST to that path.
class TableHandler : public esp8266webserver::RequestHandler<LookaheadServer> {
 public:
  TableHandler(const Route* table, size_t n) : table_(table), n_(n) {}

  bool canHandle(HTTPMethod method, const String& uri) override {
    Route r;
    return find(method, uri, r);
  }
  bool canUpload(const String& uri) override {
    Route r;
    return find(HTTP_POST, uri, r) && r.upload;
  }
  bool handle(WebServerT& server, HTTPMethod method, const String& uri) override {
    Route r;
    if (!find(method, uri, r)) return false;
    r.fn();
    server.client().rearm();  // a second request on this connection waits until it is all here
    return true;
  }
  void upload(WebServerT&, const String& uri, HTTPUpload&) override {
    Route r;
    if (find(HTTP_POST, uri, r) && r.upload) r.upload();
  }

 private:
  // The first route with this method and path, copied out of flash into `out`.
  bool find(HTTPMethod method, const String& uri, Route& out) const {
    for (size_t i = 0; i < n_; i++) {
      memcpy_P(&out, &table_[i], sizeof(Route));
      if (out.method == method && strcmp(uri.c_str(), out.path) == 0) return true;
    }
    return false;
  }

  const Route* table_;
  size_t n_;
};

}  // namespace

void add(WebServerT& server, const Route* table, size_t n) { server.addHandler(new TableHandler(table, n)); }

#else

void add(WebServerT& server, const Route* table, size_t n) {
  for (size_t i = 0; i < n; i++) {
    const Route& r = table[i];
    if (r.upload) server.on(r.path, r.method, r.fn, r.upload);
    else server.on(r.path, r.method, r.fn);
  }
}

#endif

}  // namespace routes
