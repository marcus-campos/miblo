#pragma once
// Font stacks of the GeekMagic Ultra, one per ui::Font (Latin -> Cyrillic -> CJK; the first font
// that has a glyph draws it). Vendored in lib/U8g2TFT. Shared by the board and by the host
// screenshot tool (tools/screenshots), so both render with exactly the same fonts.
#include <U8g2_for_TFT_eSPI.h>

#include "platform/tft_canvas.h"

// Subset of u8g2_font_fub20_tf (digits and "%+,-./: "), made by scripts/vendor_u8g2.py.
extern const uint8_t u8g2_font_fub20_miblo[] U8G2_FONT_SECTION("u8g2_font_fub20_miblo");
// Subset of u8g2_font_fub20_tf with the letters of "miblo" only (the wordmark).
extern const uint8_t u8g2_font_fub20_brand[] U8G2_FONT_SECTION("u8g2_font_fub20_brand");

namespace board {
namespace fonts {

inline const uint8_t* const kSmall[] = {u8g2_font_helvR10_te, u8g2_font_6x13_t_cyrillic, u8g2_font_wqy14_t_gb2312a, nullptr};
inline const uint8_t* const kSmallBold[] = {u8g2_font_helvB10_te, u8g2_font_6x13B_t_cyrillic, u8g2_font_wqy14_t_gb2312a,
                                            nullptr};
inline const uint8_t* const kBody[] = {u8g2_font_helvR12_te, u8g2_font_8x13_t_cyrillic, u8g2_font_wqy14_t_gb2312a, nullptr};
inline const uint8_t* const kBodyBold[] = {u8g2_font_helvB12_te, u8g2_font_8x13_t_cyrillic, u8g2_font_wqy14_t_gb2312a,
                                           nullptr};
inline const uint8_t* const kTitle[] = {u8g2_font_helvB18_te, u8g2_font_10x20_t_cyrillic, u8g2_font_wqy14_t_gb2312a, nullptr};
inline const uint8_t* const kHero[] = {u8g2_font_helvB24_te, u8g2_font_inr24_t_cyrillic, u8g2_font_wqy14_t_gb2312a, nullptr};
inline const uint8_t* const kNumL[] = {u8g2_font_fub30_tn, u8g2_font_fub20_miblo, u8g2_font_helvB18_te, nullptr};
inline const uint8_t* const kNumM[] = {u8g2_font_fub20_miblo, u8g2_font_helvB12_te, nullptr};
inline const uint8_t* const kBrand[] = {u8g2_font_fub20_brand, u8g2_font_helvB18_te, nullptr};
inline const TftCanvas::FontStack kStacks[] = {kSmall, kSmallBold, kBody, kBodyBold, kTitle, kHero, kNumL, kNumM, kBrand};
static_assert(sizeof(kStacks) / sizeof(kStacks[0]) == (size_t)ui::Font::Count, "one stack per ui::Font");

}  // namespace fonts
}  // namespace board
