#pragma once

// Versão do firmware. scripts/build.sh lê esta linha para nomear dist/miblo-<versão>.bin.
#define MIBLO_FW_VERSION "0.1.0"
// Build identity: scripts/build.sh passes the git short hash (-D MIBLO_BUILD=\"<sha>\");
// any other build (pio run, tests) is "dev".
#ifndef MIBLO_BUILD
#define MIBLO_BUILD "dev"
#endif
#define MIBLO_PROTO 1
