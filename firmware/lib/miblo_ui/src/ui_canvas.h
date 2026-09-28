#pragma once
#include <stdint.h>

// Interface de desenho independente de placa. Cada placa implementa um Canvas (ex.: TFT_eSPI +
// fontes u8g2 na GeekMagic Ultra); os layouts em ui_screens.h só falam com esta interface.
namespace ui {

struct ScreenSpec {
  int16_t w;
  int16_t h;
};

// Estilos de texto; a placa mapeia cada um para fontes reais (com fallback latim/cirílico/CJK).
enum class Font : uint8_t { Small, SmallBold, Body, BodyBold, Title, Hero, NumL, NumM, Count };
enum class Align : uint8_t { Left, Center, Right };

// Paleta dos mockups (RGB565).
namespace color {
constexpr uint16_t BG = 0x0841;          // #0b0b0d
constexpr uint16_t TEXT = 0xEF7D;        // #eeeeee
constexpr uint16_t MUTED = 0xAD55;       // #aaaaaa
constexpr uint16_t DIM = 0x73AE;         // #777777
constexpr uint16_t FAINT = 0x52AA;       // #555555
constexpr uint16_t AMBER = 0xF524;       // #f5a524 precisa de você
constexpr uint16_t GREEN = 0x4EF0;       // #4ade80 rodando
constexpr uint16_t BLUE = 0x653F;        // #60a5fa terminou
constexpr uint16_t FLASH_BLUE = 0x3C1E;  // #3b82f6 flash azul
constexpr uint16_t CORAL = 0xDBAA;       // #d97757 janela de 5h / mascote
constexpr uint16_t VIOLET = 0x8C5E;      // #8b8bf5 semana
constexpr uint16_t RED = 0xEA28;         // #ef4444 limite ≥ 95%
constexpr uint16_t TRACK = 0x2125;       // #262629 fundo das barras
constexpr uint16_t DIVIDER = 0x2104;     // #222222
constexpr uint16_t CARD = 0x10A3;        // #16161a
constexpr uint16_t CARD_AMBER = 0x18A1;  // #1c160a
constexpr uint16_t CMD_BG = 0x18C3;      // #1a1a1e
constexpr uint16_t BLACK = 0x0000;
constexpr uint16_t WHITE = 0xFFFF;
}  // namespace color

class Canvas {
 public:
  virtual ~Canvas() = default;
  virtual ScreenSpec spec() const = 0;
  virtual void fillRect(int x, int y, int w, int h, uint16_t c) = 0;
  virtual void fillRoundRect(int x, int y, int w, int h, int r, uint16_t c) = 0;
  virtual void drawRect(int x, int y, int w, int h, uint16_t c) = 0;
  virtual void fillCircle(int cx, int cy, int r, uint16_t c) = 0;
  virtual void wideLine(int x0, int y0, int x1, int y1, int width, uint16_t c, uint16_t bg) = 0;
  // Arco anti-aliased: ângulos em graus, 0 = 6 h, sentido horário (convenção do TFT_eSPI).
  virtual void arc(int cx, int cy, int r, int ir, int a0, int a1, uint16_t fg, uint16_t bg) = 0;
  // Texto UTF-8 com linha de base em y; corta com "..." se passar de maxW. Retorna a largura.
  // Glyph ausente nas fontes → retângulo, nunca trava.
  virtual int text(int x, int y, const char* s, Font f, uint16_t fg, Align a, int maxW) = 0;
  virtual int textWidth(const char* s, Font f) = 0;
};

}  // namespace ui
