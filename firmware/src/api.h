#pragma once
#include "platform/platform.h"

// HTTP API used by the bridge (contract: plugin/test/fakes/fake-device.js).
namespace api {

void begin(WebServerT& server);

}  // namespace api
