#pragma once
// Dados constantes grandes (tabelas de strings) em "ROM". No host e em placas com .rodata na
// flash (ESP32) é memória comum. Uma placa pode injetar outra implementação com
//   -D MIBLO_ROM_IMPL=\"<header>\"   (ex.: boards/geekmagic_ultra/miblo_rom_esp8266.h → PROGMEM)
// Assim o núcleo não inclui nenhum header de Arduino/ESP.
#include <stdint.h>
#include <string.h>

#if defined(MIBLO_ROM_IMPL)
#include MIBLO_ROM_IMPL
#else
#define MIBLO_ROM
inline uint8_t mibloRomByte(const char* p) { return (uint8_t)*p; }
#endif
