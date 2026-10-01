# Third-party notices — firmware/lib/U8g2TFT

This directory is vendored by `firmware/scripts/vendor_u8g2.py` from upstream
[Bodmer/U8g2_for_TFT_eSPI](https://github.com/Bodmer/U8g2_for_TFT_eSPI) (pinned commit
`a170ef8b6d8414b1ee2ecc97b5b913e08f5597ac`), which itself re-packages the font renderer and a
subset of the bitmap fonts from [olikraus/u8g2](https://github.com/olikraus/u8g2). The vendored
`LICENSE` file in `src/` carries the primary notices reproduced below; this file adds per-font
attribution for the specific fonts Miblo embeds (`src/miblo_fonts.c`), since each font family
under u8g2 carries its own upstream license. The CJK font is the exception: it is converted by the
same script from the official GNU Unifont BDF with u8g2's `bdfconv`, not taken from Bodmer.

The full text of the SIL Open Font License 1.1, which covers FreeUniversal, Inconsolata and
Unifont, is in [`firmware/licenses/OFL-1.1.txt`](../../licenses/OFL-1.1.txt).

## Library: U8g2_for_TFT_eSPI

- **Component:** `U8g2_for_TFT_eSPI.h` / `U8g2_for_TFT_eSPI.cpp` (the u8g2-compatible text
  renderer adapted by Bodmer for the TFT_eSPI library).
- **License:** BSD 2-Clause License.
- **Copyright:** (c) 2018, olikraus. TFT_eSPI adaptation by Bodmer, June 2020.
- **Upstream:** <https://github.com/Bodmer/U8g2_for_TFT_eSPI>, original renderer from
  <https://github.com/olikraus/u8g2>.
- **Full text:** see `src/LICENSE` (vendored verbatim).

## Fonts embedded in `src/miblo_fonts.c`

All fonts below are u8g2 bitmap fonts (`u8g2_font_*`), converted from BDF sources by the u8g2
project (Unifont: by `scripts/vendor_u8g2.py`, with u8g2's `bdfconv`). Sources consulted: the u8g2 font-group wiki pages (one per family, linked below) and the
Adobe/DEC X11 license text vendored in `src/LICENSE`.

| Font symbols | Family | License | Upstream |
| --- | --- | --- | --- |
| `u8g2_font_helvR10_te`, `u8g2_font_helvR12_te`, `u8g2_font_helvB10_te`, `u8g2_font_helvB12_te`, `u8g2_font_helvB18_te`, `u8g2_font_helvB24_te` | Helvetica-style X11 bitmap fonts (HELVR/HELVB BDF) | Adobe/DEC X11 font license (permissive; see full text below) | [u8g2 X11 font group](https://github.com/olikraus/u8g2/wiki/fntgrpx11); license text vendored in `src/LICENSE` |
| `u8g2_font_fub20_miblo` (subset of `u8g2_font_fub20_tf`), `u8g2_font_fub30_tn` | FreeUniversal (Bold) | SIL Open Font License (OFL) | [u8g2 FreeUniversal font group](https://github.com/olikraus/u8g2/wiki/fntgrpfreeuniversal); font at <http://openfontlibrary.org/font/freeuniversal> |
| `u8g2_font_6x13_t_cyrillic`, `u8g2_font_6x13B_t_cyrillic`, `u8g2_font_8x13_t_cyrillic`, `u8g2_font_10x20_t_cyrillic` | misc-fixed (X11 `-Misc-Fixed-*`) | Public domain ("Share and enjoy.") | [u8g2 X11 font group](https://github.com/olikraus/u8g2/wiki/fntgrpx11) |
| `u8g2_font_inr24_t_cyrillic` | Inconsolata (LGC, with Cyrillic) | SIL Open Font License 1.1 | [u8g2 Inconsolata font group](https://github.com/olikraus/u8g2/wiki/fntgrpinconsolata); font at <http://openfontlibrary.org/en/font/inconsolata-lgc> |
| `u8g2_font_unifont_t_gb2312a` | GNU Unifont 18.0.01 (GB2312 punctuation, symbols and level-1 hanzi) | SIL Open Font License 1.1 (dual-licensed upstream; Miblo uses it under the OFL) | <https://unifoundry.com/unifont/>; license <https://unifoundry.com/LICENSE.txt> |

### Helvetica-style bitmap fonts (helvR / helvB)

- **License:** Adobe/DEC X11 font license — permissive, requires the copyright/permission notice
  to be reproduced; explicitly *not* an open-source license in the OSI sense, but unrestricted for
  use/copy/modify/distribute.
- **Copyright:** 1984-1989, 1994 Adobe Systems Incorporated; 1988, 1994 Digital Equipment
  Corporation.
- **Trademark note:** "Helvetica" is a registered trademark of Monotype/Adobe. The license text
  vendored in `src/LICENSE` grants permission to use the "Adobe" and "Digital Equipment
  Corporation" names only in connection with the glyphs it describes, and forbids using those
  names in advertising or publicity without prior written permission. These are bitmap
  reproductions derived from Adobe/DEC's X11 BDF distribution (HELVB*/HELVR* files), not the
  Helvetica(R) outline font itself.
- **Full text:** see `src/LICENSE`, section "X11 Fonts COUR, HELV, NCEN, TIM, SYMB".

### FreeUniversal (fub)

- **License:** SIL Open Font License, Version 1.1. Verified against the license field embedded in
  the original TrueType files (`FreeUniversal-Regular.ttf`, `FreeUniversal-Bold.ttf`) from
  <https://fontlibrary.org/en/font/freeuniversal>. Full text: `firmware/licenses/OFL-1.1.txt`.
- **Copyright:** FreeUniversal (c) Stephen Wilson, 2009. Derived from Sil-Sophia, copyright (c)
  SIL International, 1994-2008.
- **Reserved Font Names:** "FreeUniversal" (Regular) and "SILSophia" (Bold). Miblo embeds
  modified versions of the Bold (bitmap conversion, and the `_miblo`/`_brand` subsets), so under
  OFL section 3 they are never presented under either reserved name; the names appear here only
  as attribution.

### misc-fixed (6x13, 6x13B, 8x13, 10x20)

- **License:** Public domain. The u8g2 wiki's X11 font-group page records the upstream copyright
  string for these fonts as "Public domain font. Share and enjoy." (some other X11 misc-fixed
  fonts instead carry "These glyphs are unencumbered".)
- **Source:** the X11 `misc-fixed` distribution (`-Misc-Fixed-Medium-R-SemiCondensed--13-...` and
  siblings), as shipped with the X Window System / x.org and converted to u8g2 BDF-derived C
  arrays.
- **Upstream:** <https://github.com/olikraus/u8g2/wiki/fntgrpx11>; original X11 font sources at
  x.org / freedesktop.org.

### Inconsolata (inr, Cyrillic variant)

- **License:** SIL Open Font License 1.1. Full text: `firmware/licenses/OFL-1.1.txt`.
- **Copyright:** original Roman design (c) 2006 Raph Levien; Cyrillic extension (c) 2012 MihailJP;
  Greek extension (c) 2010-2012 Dimosthenis Kaponis; further modifications by Greg Omelaenko. The
  u8g2-embedded variant used here (`_t_cyrillic`) draws on the "Inconsolata LGC" (Latin/Greek/
  Cyrillic) fork.
- **Upstream:** <https://github.com/olikraus/u8g2/wiki/fntgrpinconsolata>;
  <http://openfontlibrary.org/en/font/inconsolata-lgc>.

### GNU Unifont (unifont, GB2312 subset)

- **License:** SIL Open Font License 1.1. Upstream dual-licenses the compiled fonts under the OFL
  1.1 and under GPLv2+ with the GNU font embedding exception; Miblo uses the OFL option, which
  places no copyleft obligations on the rest of the firmware. Unifont declares no Reserved Font
  Name. Full text: `firmware/licenses/OFL-1.1.txt`.
- **Copyright:** (C) 1998-2026 Roman Czyborra, Paul Hardy, Qianqian Fang, Andrew Miller, Johnnie
  Weaver, David Corbett, Ælla Chiana Moskopp, Rebecca Bettencourt, Minseo Lee, Ho-Seok Ee, et al.
- **Source:** `unifont-18.0.01.bdf.gz` from <https://unifoundry.com/unifont/>, converted by
  `scripts/vendor_u8g2.py` with `bdfconv` from u8g2 (commit
  `d6c8499c5f2707cac8eccd09fd8f677d12b17977`) into `u8g2_font_unifont_t_gb2312a`: ASCII plus
  GB2312 rows 1-3, 8-9 and 16-55. This subset is a Modified Version under the OFL.
