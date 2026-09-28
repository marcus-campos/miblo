#pragma once
// ESP8266: .rodata goes into RAM; the string tables need to stay in flash (PROGMEM) and
// be read byte by byte with pgm_read_byte. Injected into the core via -D MIBLO_ROM_IMPL.
#include <pgmspace.h>
#define MIBLO_ROM PROGMEM
inline uint8_t mibloRomByte(const char* p) { return pgm_read_byte(p); }
