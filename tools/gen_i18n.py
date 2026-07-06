#!/usr/bin/env python3
"""Generate i18n_strings.inc from lang/en.json and include/i18n.h.

Reads the enum order from i18n.h, reads English values from en.json,
and emits the C initializers for g_en[] and g_key_names[].

Usage:  python3 tools/gen_i18n.py [topdir]
        Defaults to current directory if topdir is omitted.
"""

import json, re, sys, os

topdir = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

header = os.path.join(topdir, "include", "i18n.h")
en_json = os.path.join(topdir, "lang", "en.json")
out_inc = os.path.join(topdir, "source", "i18n_strings.inc")

# Parse enum names from i18n.h in declaration order
with open(header, "r", encoding="utf-8") as f:
    src = f.read()

enum_block = re.search(r"enum\s*\{(.*?)S__COUNT", src, re.DOTALL)
if not enum_block:
    sys.exit("Could not find enum in i18n.h")

enums = re.findall(r"\b(S_\w+)\b", enum_block.group(1))

# Load English strings
with open(en_json, "r", encoding="utf-8") as f:
    strings = json.load(f)

# Enum name -> JSON key: strip S_ prefix, lowercase
def enum_to_key(name):
    return name[2:].lower()

# C-escape a string value
def c_escape(s):
    s = s.replace("\\", "\\\\")
    s = s.replace('"', '\\"')
    s = s.replace("\n", "\\n")
    s = s.replace("\t", "\\t")
    return s

# Check all enums have a matching key
missing = []
for e in enums:
    k = enum_to_key(e)
    if k not in strings:
        missing.append((e, k))
if missing:
    print("WARNING: missing keys in en.json:", file=sys.stderr)
    for e, k in missing:
        print(f"  {e} -> \"{k}\"", file=sys.stderr)

# Generate
lines = []
lines.append("/* AUTO-GENERATED from lang/en.json — do not edit by hand. */")
lines.append("/* Run: python3 tools/gen_i18n.py */")
lines.append("")
lines.append("static const char *const g_en[S__COUNT] = {")
for e in enums:
    k = enum_to_key(e)
    val = strings.get(k, "")
    lines.append(f'    /* {e} */ "{c_escape(val)}",')
lines.append("};")
lines.append("")
lines.append("static const char *const g_key_names[S__COUNT] = {")
for e in enums:
    k = enum_to_key(e)
    lines.append(f'    [{e}] = "{k}",')
lines.append("};")
lines.append("")

output = "\n".join(lines)

# Only rewrite if changed
if os.path.exists(out_inc):
    with open(out_inc, "r", encoding="utf-8") as f:
        if f.read() == output:
            sys.exit(0)

with open(out_inc, "w", encoding="utf-8") as f:
    f.write(output)

print(f"Generated {out_inc}")
