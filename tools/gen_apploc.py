#!/usr/bin/env python3
"""Generate c/src/apploc.c + c/include/edit/apploc.h from src/bin/edit/localization.rs.

Usage: gen_apploc.py <repo-root>
Verifies: LocId order/count vs LUT rows, 11 langs per row; prints coverage.
"""
import re
import sys

root = sys.argv[1]
src = open(root + "/src/bin/edit/localization.rs", encoding="utf-8").read()

# LocId variants in order (stop at Count).
m = re.search(r"pub enum LocId \{(.*?)\n\}", src, re.S)
body = m.group(1)
loc_ids = [v.strip().rstrip(",") for v in body.split("\n")]
loc_ids = [v for v in loc_ids if v and not v.startswith("//") and v != "Count"]

# LangIds in order (stop at Count).
m = re.search(r"enum LangId \{(.*?)\n\}", src, re.S)
body = m.group(1)
langs = [v.strip().rstrip(",") for v in body.split("\n")]
langs = [v for v in langs if v and not v.startswith("//") and v != "Count"]
assert langs[0] == "en", langs
print(f"loc_ids={len(loc_ids)} langs={langs}")

# LUT rows: [...] blocks inside S_LANG_LUT, each with one /* lang */ "str" per lang.
m = re.search(r"S_LANG_LUT.*=\s*\[(.*)" + chr(10) + r"\];", src, re.S)
lut = m.group(1)
rows = re.findall(r"\[(.*?)\],\s*(?://|$)", lut, re.S)
# Fallback: split top-level [...] blocks.
if len(rows) != len(loc_ids):
    rows = []
    depth = 0
    cur = None
    in_str = False
    esc = False
    for ch in lut:
        if in_str:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
            if cur is not None:
                cur.append(ch)
        else:
            if ch == '"':
                in_str = True
                if cur is not None:
                    cur.append(ch)
            elif ch == "[":
                if depth == 0:
                    cur = []
                else:
                    cur.append(ch)
                depth += 1
            elif ch == "]":
                depth -= 1
                if depth == 0 and cur is not None:
                    rows.append("".join(cur))
                    cur = None
                elif cur is not None:
                    cur.append(ch)
print(f"rows={len(rows)}")
assert len(rows) == len(loc_ids), f"{len(rows)} != {len(loc_ids)}"


def rust_unescape(s):
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\":
            i += 1
            e = s[i]
            out.append({"n": "\n", "r": "\r", "t": "\t", "\\": "\\", '"': '"', "0": "\0"}.get(e, e))
            if e == "u":
                assert s[i + 1] == "{", s[i:]
                j = s.index("}", i)
                out[-1] = chr(int(s[i + 2 : j], 16))
                i = j
        else:
            out.append(c)
        i += 1
    return "".join(out)


def c_escape(s):
    out = []
    for c in s:
        o = ord(c)
        if c == '"':
            out.append('\\"')
        elif c == "\\":
            out.append("\\\\")
        elif c == "\n":
            out.append("\\n")
        elif o < 0x20 or o == 0x7F:
            out.append(f"\\x{o:02x}")
        else:
            out.append(c)
    return "".join(out)


table = []
for idx, row in enumerate(rows):
    strs = re.findall(r'"((?:[^"\\]|\\.)*)"', row)
    assert len(strs) == len(langs), f"row {idx} ({loc_ids[idx]}): {len(strs)} strs"
    table.append([rust_unescape(x) for x in strs])

# Sanity: English File must be "File", German "Datei".
en = langs.index("en")
de = langs.index("de")
assert table[loc_ids.index("File")][en] == "File"
assert table[loc_ids.index("File")][de] == "Datei"

def snake(name):
    out = []
    for i, c in enumerate(name):
        if c.isupper() and i > 0:
            out.append("_")
        out.append(c.upper())
    return "".join(out)

with open(root + "/c/include/edit/apploc.h", "w", encoding="utf-8") as f:
    f.write("#ifndef EDIT_APPLOC_H\n#define EDIT_APPLOC_H\n\n")
    f.write("// App string table (generated from src/bin/edit/localization.rs).\n")
    f.write("// Threading: init once before use; loc() is read-only afterwards.\n\n")
    f.write("typedef enum {\n")
    for lid in loc_ids:
        f.write(f"    EDIT_LOC_{snake(lid)},\n")
    f.write(f"    EDIT_LOC_COUNT = {len(loc_ids)}\n")
    f.write("} edit_loc_id_t;\n\n")
    f.write("void edit_loc_init(void);\n")
    f.write("const char *edit_loc(edit_loc_id_t id);\n\n#endif\n")

with open(root + "/c/src/apploc.c", "w", encoding="utf-8") as f:
    f.write('#include "edit/apploc.h"\n\n#include <stdbool.h>\n#include <stdlib.h>\n#include <string.h>\n\n')
    f.write("// Rows: LocId order; cols: en,de,es,fr,it,ja,ko,pt_br,ru,zh_hans,zh_hant.\n")
    f.write("static const char *const TABLE[][11] = {\n")
    for lid, vals in zip(loc_ids, table):
        f.write(f"    /* {lid} */ {{\n")
        for lang, val in zip(langs, vals):
            f.write(f'        /* {lang} */ "{c_escape(val)}",\n')
        f.write("    },\n")
    f.write("};\n\n")
    f.write("static int lang_index = 0;\n\n")
    f.write("void edit_loc_init(void) {\n")
    f.write("    static const char *const keys[11] = {\"en\", \"de\", \"es\", \"fr\", \"it\", \"ja\",\n")
    f.write("                                         \"ko\", \"pt-br\", \"ru\", \"zh\", \"zh-hant\"};\n")
    f.write("    int idx = 0; // Rust resets to en on every init without a match.\n")
    f.write("    const char *envs[3] = {getenv(\"LANGUAGE\"), getenv(\"LC_ALL\"), getenv(\"LANG\")};\n")
    f.write("    for (int e = 0; e < 3; ++e) {\n")
    f.write("        const char *val = envs[e];\n")
    f.write("        if (val == NULL) {\n            continue;\n        }\n")
    f.write("        // First set variable wins (mirrors Rust: extend + break).\n")
    f.write("        // Unknown pieces are skipped (mirrors Rust `_ => continue`).\n")
    f.write("        bool done = false;\n")
    f.write("        const char *p = val;\n")
    f.write("        for (; !done; p = strchr(p, \':\') != NULL ? strchr(p, \':\') + 1 : NULL) {\n")
    f.write("            const char *sep = strchr(p, \':\');\n")
    f.write("            size_t n = sep != NULL ? (size_t)(sep - p) : strlen(p);\n")
    f.write("            if (n > 0) {\n")
    f.write("                for (int k = 0; k < 11 && !done; ++k) {\n")
    f.write("                    if (strlen(keys[k]) == n && memcmp(keys[k], p, n) == 0) {\n")
    f.write("                        idx = k;\n                        done = true;\n                    }\n")
    f.write("                }\n            }\n")
    f.write("            if (sep == NULL) {\n                break;\n            }\n")
    f.write("        }\n        break;\n    }\n")
    f.write("    lang_index = idx;\n")
    f.write("}\n\n")
    f.write("const char *edit_loc(edit_loc_id_t id) {\n")
    f.write("    if (id < 0 || id >= EDIT_LOC_COUNT) {\n")
    f.write('        return "";\n')
    f.write("    }\n")
    f.write("    return TABLE[(int)id][lang_index];\n")
    f.write("}\n")
print(f"wrote {len(table)}x{len(langs)} table")
