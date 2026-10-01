#!/usr/bin/env python3
"""Vendors U8g2_for_TFT_eSPI (Bodmer) + only the u8g2 fonts Miblo uses, in PROGMEM.

Why vendor it: on the ESP8266 the original library leaves the fonts in `.rodata` (RAM!) and
reads the bytes with a direct pointer. The patch below puts the fonts in `.irom.text` (flash)
and swaps the read for pgm_read_byte — same as what official u8g2 does for the ESP8266. Run
once; the result (lib/U8g2TFT/) is committed.

The CJK font is not taken from Bodmer: u8g2 only ships Unifont with a few hundred Chinese glyphs,
so the GB2312 set below is converted here from the official Unifont BDF with u8g2's own bdfconv
(built from source at a pinned commit; needs a C compiler).

Usage: firmware/.venv/bin/python firmware/scripts/vendor_u8g2.py
"""
import codecs
import glob
import gzip
import io
import os
import re
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

COMMIT = "a170ef8b6d8414b1ee2ecc97b5b913e08f5597ac"
BASE = f"https://raw.githubusercontent.com/Bodmer/U8g2_for_TFT_eSPI/{COMMIT}"
HERE = os.path.dirname(os.path.abspath(__file__))
DEST = os.path.join(HERE, "..", "lib", "U8g2TFT", "src")

# Fonts used by src/display.cpp (text styles). Total size ~= 224 KB.
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
    "u8g2_font_unifont_t_gb2312a",
]

# Unifont (SIL OFL 1.1, or GPLv2+ with the font embedding exception; Miblo uses it under the OFL).
UNIFONT_VERSION = "18.0.01"
UNIFONT_BDF = (f"https://unifoundry.com/pub/unifont/unifont-{UNIFONT_VERSION}/font-builds/"
               f"unifont-{UNIFONT_VERSION}.bdf.gz")
# u8g2 commit whose tools/font/bdfconv converts the BDF.
U8G2_COMMIT = "d6c8499c5f2707cac8eccd09fd8f677d12b17977"
# GB2312 rows drawn by the CJK font: punctuation (1), numbered symbols (2), full-width ASCII (3),
# pinyin (8), box drawing (9) and the 3755 level-1 hanzi (16-55). Latin, Cyrillic and digits come
# from the fonts before it in each stack.
GB2312_ROWS = [1, 2, 3, 8, 9] + list(range(16, 56))
# Characters outside those rows that a translated line needs: GB2312 level 2 (rows 56-87) has
# about 3000 rarer hanzi, too many to carry for a few words, so only the ones in use are added.
# 筝: 风筝 (kite), in the visit line FriendKite.
EXTRA_CHARS = "筝"

# Fonts only used for a few characters: vendored as a subset (same glyph data and metrics, the
# other glyphs dropped) under a new name. fub20 draws percentages and "--" (NumM) and the "%"
# fallback of the big numbers (NumL), so the full 5.4 KB font is not needed.
SUBSETS = {
    "u8g2_font_fub20_tf": [("u8g2_font_fub20_miblo", " %+,-./0123456789:"),
                           ("u8g2_font_fub20_brand", "bilmo")],  # the "miblo" wordmark
}

PATCH = """
/* --- Miblo: fonts in flash on the ESP8266 (same as official u8g2) --- */
#if defined(ESP8266)
#  include <pgmspace.h>
#  define U8X8_FONT_SECTION(name) __attribute__((section(".irom.text." name)))
#  define u8x8_pgm_read(adr) pgm_read_byte(adr)
#  define U8X8_PROGMEM
#endif
/* --- end of Miblo patch --- */

"""


def fetch(path):
    with urllib.request.urlopen(f"{BASE}/{path}", timeout=120) as r:
        # latin-1 preserves the bytes exactly (some comments are not valid UTF-8)
        return r.read().decode("latin-1")


def font_bytes(lines):
    """The bytes of a u8g2 font from its C definition lines."""
    body = "\n".join(lines)
    body = body[body.index("=") + 1:]
    lits = re.findall(r'"((?:[^"\\]|\\.)*)"', body, re.S)
    return b"".join(codecs.escape_decode(l.encode("latin-1"))[0] for l in lits)


def subset_font(data, chars):
    """u8g2 font with only the 8-bit glyphs in `chars` (no Unicode section).

    Layout: 23-byte header (glyph count at 0; offsets of 'A', 'a' and the Unicode table at
    17..22, big endian, relative to byte 23), then glyph records [encoding, size, bits...]
    ended by [0, 0], then the Unicode table ([0, 4, 0xFF, 0xFF] = empty).
    """
    keep = {ord(c) for c in chars}
    glyphs, p = [], 23
    while data[p + 1]:
        if data[p] in keep:
            glyphs.append(data[p:p + data[p + 1]])
        p += data[p + 1]
    body = b"".join(glyphs) + b"\0\0"
    uni = len(body)
    header = bytearray(data[:23])
    header[0] = len(glyphs)
    header[17:21] = b"\0\0\0\0"  # no 'A'/'a' shortcuts: lookups scan from the first glyph
    header[21], header[22] = uni >> 8, uni & 0xFF
    return bytes(header) + body + b"\0\4\377\377"


def gb2312_chars():
    """Unicode code points of ASCII, the GB2312_ROWS of GB2312 and EXTRA_CHARS."""
    chars = set(range(32, 127))
    for row in GB2312_ROWS:
        for col in range(1, 95):
            try:
                chars.add(ord(bytes([0xA0 + row, 0xA0 + col]).decode("gb2312")))
            except UnicodeDecodeError:
                pass  # unassigned cell
    chars.update(ord(c) for c in EXTRA_CHARS)
    return sorted(chars)


def unifont_lines(name):
    """C definition lines of a u8g2 font with gb2312_chars(), converted from the Unifont BDF."""
    with tempfile.TemporaryDirectory() as tmp:
        url = f"https://codeload.github.com/olikraus/u8g2/tar.gz/{U8G2_COMMIT}"
        with urllib.request.urlopen(url, timeout=300) as r:
            tar = tarfile.open(fileobj=io.BytesIO(r.read()))
        prefix = f"u8g2-{U8G2_COMMIT}/tools/font/bdfconv/"
        tar.extractall(tmp, members=[m for m in tar.getmembers() if m.name.startswith(prefix)], filter="data")
        src = os.path.join(tmp, prefix)
        tool = os.path.join(tmp, "bdfconv")
        subprocess.run(["cc", "-O2", "-w", "-o", tool] + glob.glob(os.path.join(src, "*.c")), check=True)

        with urllib.request.urlopen(UNIFONT_BDF, timeout=300) as r:
            bdf = os.path.join(tmp, "unifont.bdf")
            with open(bdf, "wb") as f:
                f.write(gzip.decompress(r.read()))
        mapfile = os.path.join(tmp, "chars.map")
        with open(mapfile, "w") as f:
            f.write(",\n".join(f"${c:x}" for c in gb2312_chars()) + "\n")
        out = os.path.join(tmp, "font.c")
        subprocess.run([tool, "-b", "0", "-f", "1", "-M", mapfile, "-n", name, "-o", out, bdf], check=True)
        with open(out, encoding="utf-8") as f:
            lines = f.read().split("\n")
    start = next(i for i, l in enumerate(lines) if l.startswith(f"const uint8_t {name}["))
    end = start
    while not lines[end].rstrip().endswith('";'):
        end += 1
    return lines[start:end + 1]


def font_c(name, data):
    """C definition of a font, in the u8g2 style (octal escapes, 64 bytes per line)."""
    out = [f'const uint8_t {name}[{len(data) + 1}] U8G2_FONT_SECTION("{name}") = ']
    for i in range(0, len(data), 64):
        # 3-digit octal escapes: a following digit can never be read as part of one
        chunk = "".join(chr(b) if 32 <= b < 127 and chr(b) not in '"\\?' else f"\\{b:03o}" for b in data[i:i + 64])
        out.append(f'  "{chunk}"')
    out[-1] += ";"
    return out


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
        sys.exit("u8g2_fonts.h changed: patch anchor not found")
    header = header.replace(anchor, PATCH + anchor, 1)
    with open(os.path.join(DEST, "u8g2_fonts.h"), "w", encoding="latin-1") as f:
        f.write(header)

    print("downloading u8g2_fonts.c (~26 MB)...")
    source = fetch("src/u8g2_fonts.c").split("\n")
    out = ['/* Generated by scripts/vendor_u8g2.py - only the fonts used by Miblo. */',
           '#include "u8g2_fonts.h"', ""]
    total = 0
    for font in FONTS:
        if font == "u8g2_font_unifont_t_gb2312a":
            print(f"converting Unifont {UNIFONT_VERSION}...")
            lines = unifont_lines(font)
            out.extend(lines)
            out.append("")
            size = int(re.search(r"\[(\d+)\]", lines[0]).group(1))
            total += size
            print(f"  {font}: {size} bytes")
            continue
        start = next((i for i, l in enumerate(source) if l.startswith(f"const uint8_t {font}[")), None)
        if start is None:
            sys.exit(f"font not found: {font}")
        end = start
        while not source[end].rstrip().endswith('";'):
            end += 1
        lines = source[start:end + 1]
        subsets = SUBSETS.get(font, [(font, None)])
        for name, chars in subsets:
            part = font_c(name, subset_font(font_bytes(lines), chars)) if chars else lines
            out.extend(part)
            out.append("")
            size = int(re.search(r"\[(\d+)\]", part[0]).group(1))
            total += size
            print(f"  {name}: {size} bytes")
    with open(os.path.join(DEST, "miblo_fonts.c"), "w", encoding="latin-1") as f:
        f.write("\n".join(out) + "\n")
    print(f"total font size: {total} bytes")


if __name__ == "__main__":
    main()
