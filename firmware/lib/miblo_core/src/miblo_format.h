#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

// Tempo decorrido: 42 → "0:42", 192 → "3:12", 3725 → "1:02:05".
void formatElapsed(uint32_t secs, char* out, size_t cap);

// Tempo curto (lista/"há X"): 45 → "45s", 150 → "2m", 7300 → "2h", 180000 → "2d".
void formatAgo(uint32_t secs, char* out, size_t cap);

// Contagem regressiva: 30 → "<1min", 2700 → "45min", 7800 → "2h10", 240000 → "2d18h".
void formatCountdown(uint32_t secs, char* out, size_t cap);

// Tokens (sempre arredondado para baixo): 950 → "950", 1500 → "1.5k", 12300 → "12.3k",
// 98000 → "98k", 412000 → "412k", 1510000 → "1.5M", 2000000 → "2M", 123456789 → "123M".
void formatTokens(uint64_t tok, char* out, size_t cap);

// "HH:MM" com zeros à esquerda.
void formatHHMM(int hour, int minute, char* out, size_t cap);

// Dólares com 2 casas: 4.8 → "$4.80".
void formatUsd(float usd, char* out, size_t cap);

}  // namespace miblo
