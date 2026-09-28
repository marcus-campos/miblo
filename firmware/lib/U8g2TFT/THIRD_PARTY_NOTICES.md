# Third-party notices — firmware/lib/U8g2TFT

This directory is vendored by `firmware/scripts/vendor_u8g2.py` from upstream
[Bodmer/U8g2_for_TFT_eSPI](https://github.com/Bodmer/U8g2_for_TFT_eSPI) (pinned commit
`a170ef8b6d8414b1ee2ecc97b5b913e08f5597ac`), which itself re-packages the font renderer and a
subset of the bitmap fonts from [olikraus/u8g2](https://github.com/olikraus/u8g2). The vendored
`LICENSE` file in `src/` carries the primary notices reproduced below; this file adds per-font
attribution for the specific fonts Miblo embeds (`src/miblo_fonts.c`), since each font family
under u8g2 carries its own upstream license.

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
project. Sources consulted: the u8g2 font-group wiki pages (one per family, linked below) and the
Adobe/DEC X11 license text vendored in `src/LICENSE`.

| Font symbols | Family | License | Upstream |
| --- | --- | --- | --- |
| `u8g2_font_helvR10_te`, `u8g2_font_helvR12_te`, `u8g2_font_helvB10_te`, `u8g2_font_helvB12_te`, `u8g2_font_helvB18_te`, `u8g2_font_helvB24_te` | Helvetica-style X11 bitmap fonts (HELVR/HELVB BDF) | Adobe/DEC X11 font license (permissive; see full text below) | [u8g2 X11 font group](https://github.com/olikraus/u8g2/wiki/fntgrpx11); license text vendored in `src/LICENSE` |
| `u8g2_font_fub20_miblo` (subset of `u8g2_font_fub20_tf`), `u8g2_font_fub30_tn` | FreeUniversal (Bold) | SIL Open Font License (OFL) | [u8g2 FreeUniversal font group](https://github.com/olikraus/u8g2/wiki/fntgrpfreeuniversal); font at <http://openfontlibrary.org/font/freeuniversal> |
| `u8g2_font_6x13_t_cyrillic`, `u8g2_font_6x13B_t_cyrillic`, `u8g2_font_8x13_t_cyrillic`, `u8g2_font_10x20_t_cyrillic` | misc-fixed (X11 `-Misc-Fixed-*`) | Public domain ("Share and enjoy.") | [u8g2 X11 font group](https://github.com/olikraus/u8g2/wiki/fntgrpx11) |
| `u8g2_font_inr24_t_cyrillic` | Inconsolata (LGC, with Cyrillic) | SIL Open Font License 1.1 | [u8g2 Inconsolata font group](https://github.com/olikraus/u8g2/wiki/fntgrpinconsolata); font at <http://openfontlibrary.org/en/font/inconsolata-lgc> |
| `u8g2_font_wqy14_t_gb2312a` | WenQuanYi Bitmap Song | GPLv2, with a font-embedding exception | [u8g2 WenQuanYi font group](https://github.com/olikraus/u8g2/wiki/fntgrpwqy); upstream <http://wenq.org/wqy2/>, u8g2 conversion source <https://github.com/larryli/u8g2_wqy> |

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

- **License:** SIL Open Font License (OFL). Verified via the u8g2 wiki's FreeUniversal font-group
  page, which states the font is licensed under OFL (full text at
  <http://scripts.sil.org/OFL>) and available from
  <http://openfontlibrary.org/font/freeuniversal>.
- **Copyright:** FreeUniversal (c) Stephen Wilson, 2009. Derived from Sil-Sophia, copyright (c)
  SIL International, 1994-2008.
- **TO VERIFY:** the u8g2 wiki page does not state the exact OFL version number for FreeUniversal
  itself (OFL 1.1 is the SIL-recommended current version and is what nearly all OFL fonts now
  use, including Inconsolata below, but this has not been independently confirmed against a
  FreeUniversal-specific OFL file/header).

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

- **License:** SIL Open Font License 1.1 (full text at <http://scripts.sil.org/OFL>).
- **Copyright:** original Roman design (c) 2006 Raph Levien; Cyrillic extension (c) 2012 MihailJP;
  Greek extension (c) 2010-2012 Dimosthenis Kaponis; further modifications by Greg Omelaenko. The
  u8g2-embedded variant used here (`_t_cyrillic`) draws on the "Inconsolata LGC" (Latin/Greek/
  Cyrillic) fork.
- **Upstream:** <https://github.com/olikraus/u8g2/wiki/fntgrpinconsolata>;
  <http://openfontlibrary.org/en/font/inconsolata-lgc>.

### WenQuanYi Bitmap Song (wqy14)

- **License:** GNU General Public License v2, with a font-embedding exception that permits
  embedding the bitmap font in a device/application without extending GPL source-distribution
  requirements to that application.
- **Copyright:** (c) 2004-2010 WenQuanYi Project Board of Trustees and Qianqian Fang.
- **Upstream:** project home <http://wenq.org/wqy2/>; the u8g2 conversion is sourced from
  <https://github.com/larryli/u8g2_wqy>; u8g2 wiki entry
  <https://github.com/olikraus/u8g2/wiki/fntgrpwqy>.
- **Note:** this is the only copyleft (GPLv2) font in this set. The embedding exception is what
  makes it usable in Miblo's firmware image without triggering GPL obligations on the rest of the
  firmware; the exception applies to the font's embedded/rendered use, not to redistributing the
  font's own source/build tooling standalone.
