#pragma once
#include <stddef.h>
#include <stdint.h>

#include "miblo_snapshot.h"

namespace miblo {

// ---- Visão geral adaptativa (spec §4.1) ----
enum class OverviewKind : uint8_t { Attention, Working, Idle };

struct StateCounts {
  uint8_t pending;  // perm + question
  uint8_t running;
  uint8_t done;
  uint8_t idle;
};

// perm 0, question 1, done 2, running 3, idle 4 (mesma ordem do bridge).
uint8_t stateRank(SessionState st);
StateCounts countStates(const Snapshot& s);
// Attention se há pendência; Working se há sessão rodando; senão Idle.
OverviewKind classifyOverview(const Snapshot& s);
// Herói: permissão > pergunta > terminou (se includeDone); empate → menor `since`. -1 se nenhum.
int selectHero(const Snapshot& s, bool includeDone);
// Sessão `done` mais recente (maior `since`), ou -1.
int lastFinished(const Snapshot& s);

// ---- Duração da última resposta (herói "Terminou") ----
// O snapshot só traz `since` do estado atual; o gadget memoriza quando cada sessão começou a
// trabalhar para calcular quanto a resposta durou quando ela chega em `done`.
class RunTracker {
 public:
  void observe(const Snapshot& s);
  // true se a sessão terminou uma resposta observada do início ao fim.
  bool stats(const char* sid, uint32_t& durationSec) const;
  void clear();

 private:
  struct Entry {
    char id[9];
    bool used;
    bool active;
    bool finished;
    uint32_t start;
    uint32_t duration;
  };
  Entry entries_[kMaxSessions] = {};
  Entry* find(const char* id);
  const Entry* find(const char* id) const;
};

// ---- Paginação automática (lista a cada 5 s) ----
class Pager {
 public:
  explicit Pager(uint8_t perPage, uint32_t periodMs = 5000) : perPage_(perPage), periodMs_(periodMs) {}
  uint8_t pageCount(uint16_t itemCount) const;
  // Avança a página conforme o tempo e devolve a página atual (0-based).
  uint8_t update(uint16_t itemCount, uint32_t nowMs);
  uint8_t page() const { return page_; }
  uint8_t perPage() const { return perPage_; }

 private:
  uint8_t perPage_;
  uint32_t periodMs_;
  uint8_t page_ = 0;
  bool started_ = false;
  uint32_t lastFlipMs_ = 0;
};

// ---- Cache de regiões da tela (redesenhar só o que mudou) ----
constexpr uint32_t kHashSeed = 2166136261u;
uint32_t hashStr(uint32_t h, const char* s);
uint32_t hashInt(uint32_t h, uint32_t v);

class RegionCache {
 public:
  static constexpr uint8_t kRegions = 16;
  // true (e memoriza) se o conteúdo da região mudou desde o último desenho.
  bool changed(uint8_t region, uint32_t hash);
  void invalidate();

 private:
  uint32_t hash_[kRegions] = {};
  bool valid_[kRegions] = {};
};

}  // namespace miblo
