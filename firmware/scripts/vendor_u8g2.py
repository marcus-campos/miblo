#!/usr/bin/env python3
"""Vendoriza U8g2_for_TFT_eSPI (Bodmer) + só as fontes u8g2 que o Miblo usa, em PROGMEM.

Por que vendorizar: no ESP8266 a biblioteca original deixa as fontes em `.rodata` (RAM!) e lê
os bytes com ponteiro direto. O patch abaixo coloca as fontes em `.irom.text` (flash) e troca a
leitura por pgm_read_byte — igual ao que o u8g2 oficial faz para ESP8266. Rodar uma vez; o
resultado (lib/U8g2TFT/) é commitado.

Uso: firmware/.venv/bin/python firmware/scripts/vendor_u8g2.py
"""
import os
import re
import sys
import urllib.request

COMMIT = "a170ef8b6d8414b1ee2ecc97b5b913e08f5597ac"
BASE = f"https://raw.githubusercontent.com/Bodmer/U8g2_for_TFT_eSPI/{COMMIT}"
HERE = os.path.dirname(os.path.abspath(__file__))
DEST = os.path.join(HERE, "..", "lib", "U8g2TFT", "src")

# Fontes usadas por src/display.cpp (estilos de texto). Tamanho total ≈ 224 KB.
FONTS = [
    "u8g2_font_helvR10_te",
    "u8g2_font_helvB10_te",
    "u8g2_font_helvR12_te",
    "u8g2_font_helvB12_te",
    "u8g2_font_helvB18_te",
    "u8g2_font_helvB24_te",
    "u8g2_font_fub20_tf",
    "u8g2_font_fub30_tn",
    "u8g2_font_6x13_t_cyrillic",
    "u8g2_font_6x13B_t_cyrillic",
    "u8g2_font_8x13_t_cyrillic",
    "u8g2_font_10x20_t_cyrillic",
    "u8g2_font_inr24_t_cyrillic",
    "u8g2_font_wqy14_t_gb2312a",
]

PATCH = """
/* --- Miblo: fontes em flash no ESP8266 (como no u8g2 oficial) --- */
#if defined(ESP8266)
#  include <pgmspace.h>
#  define U8X8_FONT_SECTION(name) __attribute__((section(".irom.text." name)))
#  define u8x8_pgm_read(adr) pgm_read_byte(adr)
#  define U8X8_PROGMEM
#endif
/* --- fim do patch Miblo --- */

"""


def fetch(path):
    with urllib.request.urlopen(f"{BASE}/{path}", timeout=120) as r:
        # latin-1 preserva os bytes exatamente (há comentários que não são UTF-8 válido)
        return r.read().decode("latin-1")


def main():
    os.makedirs(DEST, exist_ok=True)
    for name in ["U8g2_for_TFT_eSPI.h", "U8g2_for_TFT_eSPI.cpp"]:
        with open(os.path.join(DEST, name), "w", encoding="latin-1") as f:
            f.write(fetch(f"src/{name}"))
    with open(os.path.join(DEST, "LICENSE"), "w", encoding="latin-1") as f:
        f.write(fetch("LICENSE"))

    header = fetch("src/u8g2_fonts.h")
    anchor = "#ifndef U8X8_FONT_SECTION"
    if anchor not in header:
        sys.exit("u8g2_fonts.h mudou: âncora do patch não encontrada")
    header = header.replace(anchor, PATCH + anchor, 1)
    with open(os.path.join(DEST, "u8g2_fonts.h"), "w", encoding="latin-1") as f:
        f.write(header)

    print("baixando u8g2_fonts.c (~26 MB)...")
    source = fetch("src/u8g2_fonts.c").split("\n")
    out = ['/* Gerado por scripts/vendor_u8g2.py - apenas as fontes usadas pelo Miblo. */',
           '#include "u8g2_fonts.h"', ""]
    total = 0
    for font in FONTS:
        start = next((i for i, l in enumerate(source) if l.startswith(f"const uint8_t {font}[")), None)
        if start is None:
            sys.exit(f"fonte não encontrada: {font}")
        end = start
        while not source[end].rstrip().endswith('";'):
            end += 1
        out.extend(source[start:end + 1])
        out.append("")
        size = int(re.search(r"\[(\d+)\]", source[start]).group(1))
        total += size
        print(f"  {font}: {size} bytes")
    with open(os.path.join(DEST, "miblo_fonts.c"), "w", encoding="latin-1") as f:
        f.write("\n".join(out) + "\n")
    print(f"total das fontes: {total} bytes")


if __name__ == "__main__":
    main()
