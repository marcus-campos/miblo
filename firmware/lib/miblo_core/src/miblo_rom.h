#pragma once
// Large constant data (string tables) held in "ROM". On the host and on boards with .rodata in
// flash (ESP32) it's ordinary memory. A board can inject another implementation with
//   -D MIBLO_ROM_IMPL=\"<header>\"   (e.g. boards/geekmagic_ultra/miblo_rom_esp8266.h -> PROGMEM)
// This way the core doesn't include any Arduino/ESP header.
#include <stdint.h>
#include <string.h>

#if defined(MIBLO_ROM_IMPL)
#include MIBLO_ROM_IMPL
#else
#define MIBLO_ROM
inline uint8_t mibloRomByte(const char* p) { return (uint8_t)*p; }
#endif
