#include "mdns_service.h"

#include <WiFiUdp.h>

#include "../context.h"
#include "miblo_mdns.h"
#include "miblo_version.h"
#include "net.h"
#include "platform.h"

namespace mdns {

static WiFiUDP udp;
static uint32_t boundConn = 0;
static bool bound = false;
static uint8_t announcesLeft = 0;
static uint32_t nextAnnounceMs = 0;
static uint8_t in[512];
static uint8_t out[512];
static char txtId[32];
static char txtName[80];
static char txtFw[24];

static miblo::MdnsInfo info() {
  miblo::MdnsInfo i{};
  i.instance = deviceName();
  i.host = ctx.ident.id;
  IPAddress ip = WiFi.localIP();
  for (int k = 0; k < 4; k++) i.ip[k] = ip[k];
  i.port = 80;
  snprintf(txtId, sizeof(txtId), "id=%s", ctx.ident.id);
  snprintf(txtName, sizeof(txtName), "name=%s", deviceName());
  snprintf(txtFw, sizeof(txtFw), "fw=%s", MIBLO_FW_VERSION);
  i.txt[0] = txtId;
  i.txt[1] = txtName;
  i.txt[2] = txtFw;
  i.txtCount = 3;
  return i;
}

static void sendMulticast(const uint8_t* data, size_t len) {
#if defined(ESP8266)
  udp.beginPacketMulticast(IPAddress(224, 0, 0, 251), miblo::kMdnsPort, WiFi.localIP());
#else
  udp.beginPacket(IPAddress(224, 0, 0, 251), miblo::kMdnsPort);
#endif
  udp.write(data, len);
  udp.endPacket();
}

void announce() {
  announcesLeft = 2;  // RFC 6762: at least two announcements, 1 s apart
  nextAnnounceMs = millis();
}

void loop(uint32_t nowMs) {
  if (!net::connected()) {
    if (bound) {
      udp.stop();
      bound = false;
    }
    return;
  }
  if (!bound || boundConn != net::connectionId()) {
    udp.stop();
#if defined(ESP8266)
    bound = udp.beginMulticast(WiFi.localIP(), IPAddress(224, 0, 0, 251), miblo::kMdnsPort);
#else
    bound = udp.beginMulticast(IPAddress(224, 0, 0, 251), miblo::kMdnsPort);
#endif
    boundConn = net::connectionId();
    announce();
  }
  if (!bound) return;

  if (announcesLeft > 0 && (int32_t)(nowMs - nextAnnounceMs) >= 0) {
    miblo::MdnsInfo i = info();
    size_t n = miblo::mdnsAnnounce(i, out, sizeof(out));
    if (n) sendMulticast(out, n);
    announcesLeft--;
    nextAnnounceMs = nowMs + 1000;
  }

  int len = udp.parsePacket();
  if (len <= 0) return;
  if (len > (int)sizeof(in)) {
    udp.flush();
    return;
  }
  udp.read(in, len);
  IPAddress from = udp.remoteIP();
  uint16_t port = udp.remotePort();
  miblo::MdnsInfo i = info();
  miblo::MdnsReply r = miblo::mdnsRespond(in, (size_t)len, port, i, out, sizeof(out));
  if (!r.len) return;
  if (r.unicast) {
    udp.beginPacket(from, port);
    udp.write(out, r.len);
    udp.endPacket();
  } else {
    sendMulticast(out, r.len);
  }
}

}  // namespace mdns
