#pragma once
#include <stddef.h>
#include <stdint.h>

namespace miblo {

// Decodifica o próximo code point de `p` e avança o ponteiro.
// Byte inválido ou sequência truncada → U+FFFD, avançando 1 byte. Fim da string → 0 (não avança).
uint32_t utf8Next(const char*& p);

// Quantidade de code points.
size_t utf8Length(const char* s);

// Copia no máximo `maxChars` code points de `src` para `dst` (capacidade `cap` bytes, sempre
// terminada em NUL), sem nunca cortar uma sequência UTF-8 no meio. Retorna os bytes escritos.
size_t utf8Copy(char* dst, size_t cap, const char* src, size_t maxChars = (size_t)-1);

}  // namespace miblo
