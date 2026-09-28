#pragma once

// Firmware version. scripts/build.sh reads this line to name dist/miblo-<version>.bin.
#define MIBLO_FW_VERSION "0.2.0"
// Build identity: scripts/build.sh passes the git short hash (-D MIBLO_BUILD=\"<sha>\");
// any other build (pio run, tests) is "dev".
#ifndef MIBLO_BUILD
#define MIBLO_BUILD "dev"
#endif
#define MIBLO_PROTO 1
