#pragma once
#include <stddef.h>

#include "miblo_i18n.h"
#include "miblo_snapshot.h"

namespace miblo {

// Verbo localizado para as ferramentas conhecidas do Claude Code. Bash e desconhecidas → false
// (o nome da ferramenta é mostrado como veio).
bool toolVerb(const char* tool, S& out);

// Atividade de uma sessão rodando:
//   verbo conhecido → "Editando Header.tsx" (ou só "Editando" sem det / em modo discreto)
//   outras          → "Bash · npm test"    (ou só "Bash")
//   sem ferramenta  → "Trabalhando"
void activityText(Lang lang, const char* tool, const char* det, bool discreet, char* out, size_t cap);

// Linha de estado de uma sessão para as listas:
//   perm → "permissão · Bash", question → "pergunta", done → "terminou", idle → "ociosa",
//   running → activityText(...).
void sessionLine(Lang lang, const SessionRow& row, bool discreet, char* out, size_t cap);

}  // namespace miblo
