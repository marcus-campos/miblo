#pragma once
#include <stdint.h>

// Orchestrates everything: persistence, Wi-Fi, HTTP server, mDNS, alerts and screens.
namespace app {

// Least free heap ever seen at the peak of a snapshot parse (0 until the first parse). Diagnostics.
uint32_t minHeapDuringParse();

void setup();
void loop();

}  // namespace app
