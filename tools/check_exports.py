#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The symbols a shared build exports are the public functions, no more
# and no fewer: every function the public headers declare MRHI_API, and
# nothing else, so the core's and the drivers' internal functions, the
# generated tables and any global stay out of the binary interface. Every
# build compiles the Vulkan, D3D12 and Metal headers' functions, so each
# one is exported whatever drivers the build has. Every defined dynamic
# symbol counts, whatever its name or kind: one without the mrhi prefix
# or one holding data fails it, as the public headers declare neither.
#
#   python3 tools/check_exports.py out/linux-shared/libmaul-rhi.so
#
# Reads the dynamic symbol table with nm; for ELF shared libraries.

import glob
import os
import re
import subprocess
import sys


def declared(include):
    names = set()
    for path in glob.glob(os.path.join(include, "maul-rhi", "*.h")):
        with open(path, encoding="utf-8") as f:
            names.update(re.findall(r"MRHI_API\s[^(;]*?\b(mrhi\w+)\s*\(", f.read()))
    return names


# nm's letters for code: global, local or weak.
CODE = set("TtWw")


def exported(library):
    """Each defined dynamic symbol's name and nm's letter for it."""
    out = subprocess.run(["nm", "-D", "--defined-only", library], check=True,
                         capture_output=True, text=True).stdout
    symbols = {}
    for line in out.splitlines():
        fields = line.split()
        if len(fields) == 3:
            symbols[fields[2]] = fields[1]
    return symbols


def main():
    if len(sys.argv) != 2:
        print("usage: tools/check_exports.py LIBRARY", file=sys.stderr)
        return 2
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    public = declared(os.path.join(root, "include"))
    symbols = exported(sys.argv[1])
    wrong = []
    for name, letter in sorted(symbols.items()):
        if not name.startswith("mrhi"):
            wrong.append(f"exported without the mrhi prefix: {name}")
        elif letter not in CODE:
            wrong.append(f"exported data: {name}")
        elif name not in public:
            wrong.append(f"exported but not public: {name}")
    for name in sorted(public - symbols.keys()):
        wrong.append(f"public but not exported: {name}")
    for line in wrong:
        print(line)
    if wrong or not public:
        return 1
    print(f"exports: the {len(public)} public functions, nothing else")
    return 0


if __name__ == "__main__":
    sys.exit(main())
