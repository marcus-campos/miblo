#!/usr/bin/env python3
"""Packs the translated strings (lib/miblo_core/src/miblo_strings.cpp) for the firmware.

miblo_strings.cpp stays the text people edit (kLangSource); the firmware carries this packed copy
(miblo_strings_packed.cpp, kLangTables) instead, about 22 KB smaller. Run it after every change to
miblo_strings.cpp: test_i18n compares every packed string with its source and fails until then.

Packing (per language, byte pair encoding): byte values that never occur in the language's UTF-8
text (control characters, unused letters, the bytes UTF-8 never uses...) become tokens, each
standing for a pair of bytes (plain bytes or other tokens), chosen greedily: the most frequent
pair of the remaining text first, while a pair repeats at least 3 times. Entries still end with
\\0 (never a token), so tr() skips to an entry exactly as before, over fewer bytes, then expands
just that one entry. Layout of a language blob:
  [token count N] [32-byte bitmap: bit b set = byte b is a token] [N pairs, by token byte value]
  [entries, each ended by \\0, in miblo::S order]

Usage: python3 firmware/scripts/pack_strings.py
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "lib", "miblo_core", "src")
MAX_DEPTH = 14  # tokens nest at most this deep: tr() expands with a 16-byte stack
MIN_REPEAT = 3  # a pair costs 2 bytes in the table: worth a token from 3 repeats on

SIMPLE_ESCAPES = {"n": 10, "t": 9, "r": 13, "\\": 92, '"': 34, "'": 39, "?": 63, "a": 7, "b": 8,
                  "f": 12, "v": 11}


def lex(src):
    """The C source as ('str', bytes) and ('code', text) pieces, comments dropped."""
    out, code, i, n = [], "", 0, len(src)
    while i < n:
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue
        if src.startswith("/*", i):
            i = src.index("*/", i) + 2
            continue
        c = src[i]
        if c == "'":
            j = i + 1
            while src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            code += src[i:j + 1]
            i = j + 1
            continue
        if c != '"':
            code += c
            i += 1
            continue
        if code:
            out.append(("code", code))
            code = ""
        i += 1
        lit = bytearray()
        while src[i] != '"':
            if src[i] != "\\":
                lit += src[i].encode("utf-8")
                i += 1
                continue
            i += 1
            e = src[i]
            if e in "01234567":  # up to 3 octal digits
                j = i
                while j < i + 3 and src[j] in "01234567":
                    j += 1
                lit.append(int(src[i:j], 8))
                i = j
            elif e == "x":  # as many hex digits as follow (C rule)
                j = i + 1
                while src[j] in "0123456789abcdefABCDEF":
                    j += 1
                lit.append(int(src[i + 1:j], 16))
                i = j
            else:
                lit.append(SIMPLE_ESCAPES[e])
                i += 1
        out.append(("str", bytes(lit)))
        i += 1
    if code:
        out.append(("code", code))
    return out


def source_tables(path):
    """{table name: text without the final NUL} and the table order of kLangSource."""
    with open(path, encoding="utf-8") as f:
        pieces = lex(f.read())
    tables, current, order = {}, None, None
    for kind, value in pieces:
        if kind == "str":
            if current:
                tables[current] += value
            continue
        if current and ";" in value:
            current = None
        m = re.search(r"static const char (k\w+)\[\]\s*MIBLO_ROM\s*=\s*$", value)
        if m:
            current = m.group(1)
            tables[current] = b""
        m = re.search(r"kLangSource\[\]\s*MIBLO_ROM\s*=\s*\{([^}]*)\}", value)
        if m:
            order = [name.strip() for name in m.group(1).split(",") if name.strip()]
    if not order:
        sys.exit("kLangSource not found in miblo_strings.cpp")
    return tables, order


def pack(text):
    """(token pairs {byte: (a, b)}, packed entries) of one language's text."""
    entries = [list(e) for e in text.split(b"\0")]
    used = {b for e in entries for b in e}
    free = [b for b in range(1, 256) if b not in used]
    depth = [0] * 256
    pairs = {}
    for token in free:
        counts = {}
        for e in entries:
            for a, b in zip(e, e[1:]):
                if max(depth[a], depth[b]) < MAX_DEPTH:
                    counts[(a, b)] = counts.get((a, b), 0) + 1
        if not counts:
            break
        # the most frequent pair; ties: the smallest pair (deterministic output)
        (a, b), count = min(counts.items(), key=lambda kv: (-kv[1], kv[0]))
        if count < MIN_REPEAT:
            break
        pairs[token] = (a, b)
        depth[token] = max(depth[a], depth[b]) + 1
        for e in entries:
            i, merged = 0, []
            while i < len(e):
                if i + 1 < len(e) and e[i] == a and e[i + 1] == b:
                    merged.append(token)
                    i += 2
                else:
                    merged.append(e[i])
                    i += 1
            e[:] = merged
    return pairs, b"".join(bytes(e) + b"\0" for e in entries)


def expand(pairs, packed):
    out = bytearray()
    for b in packed:
        stack = [b]
        while stack:
            x = stack.pop()
            if x in pairs:
                stack += [pairs[x][1], pairs[x][0]]
            else:
                out.append(x)
    return bytes(out)


def c_string(data, indent="    ", per_line=32):
    """`data` as C string literals (3-digit octal escapes: a digit after one is never read as
    part of it; '?' escaped too, no trigraphs)."""
    lines = []
    for i in range(0, len(data), per_line):
        chunk = "".join(chr(b) if 32 <= b < 127 and chr(b) not in '"\\?' else f"\\{b:03o}"
                        for b in data[i:i + per_line])
        lines.append(f'{indent}"{chunk}"')
    return "\n".join(lines)


def main():
    tables, order = source_tables(os.path.join(SRC, "miblo_strings.cpp"))
    out = ["// GENERATED by firmware/scripts/pack_strings.py from miblo_strings.cpp: do not edit, run the",
           "// script again after changing the strings (test_i18n fails until then). Format: the script.",
           '#include "miblo_i18n.h"',
           '#include "miblo_rom.h"',
           "",
           "namespace miblo {",
           ""]
    plain = packed_total = 0
    for name in order:
        text = tables[name]
        # The source tables end with "\0" before the compiler's own NUL: drop that last empty entry.
        if not text.endswith(b"\0"):
            sys.exit(f"{name}: the last entry must end with \\0")
        text = text[:-1]
        pairs, entries = pack(text)
        if expand(pairs, entries) != text + b"\0":
            sys.exit(f"{name}: packing does not round-trip")
        bitmap = bytearray(32)
        for t in pairs:
            bitmap[t // 8] |= 1 << (t % 8)
        blob = bytes([len(pairs)]) + bytes(bitmap)
        blob += b"".join(bytes(pairs[t]) for t in sorted(pairs)) + entries
        plain += len(text) + 2
        packed_total += len(blob)
        out.append(f"// {len(text) + 2} bytes plain, {len(pairs)} tokens")
        out.append(f"static const char {name}[] MIBLO_ROM =")
        out.append(c_string(blob) + ";")
        out.append("")
    out.append(f"const char* const kLangTables[] MIBLO_ROM = {{{', '.join(order)}}};")
    out.append("")
    out.append("}  // namespace miblo")
    with open(os.path.join(SRC, "miblo_strings_packed.cpp"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print(f"{len(order)} languages: {plain} bytes plain, {packed_total} packed")


if __name__ == "__main__":
    main()
