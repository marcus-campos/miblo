#pragma once
// ESP8266: .rodata vai para a RAM; as tabelas de strings precisam ficar na flash (PROGMEM) e
// ser lidas byte a byte com pgm_read_byte. Injetado no núcleo por -D MIBLO_ROM_IMPL.
#include <pgmspace.h>
#define MIBLO_ROM PROGMEM
inline uint8_t mibloRomByte(const char* p) { return pgm_read_byte(p); }
