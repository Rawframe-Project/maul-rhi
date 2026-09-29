#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The wasm budget (mrhi-0001): links a web build of the library, made at
# -Oz without its tests, into a program that exports every public
# function, so nothing the API reaches is dropped, prints the wasm and
# JavaScript sizes, and fails when the wasm passes BUDGET. emcc must be
# on the path.
#
# usage: wasm_size.py <libmaul-rhi.a>

import glob
import os
import re
import subprocess
import sys
import tempfile

BUDGET = 128 * 1024
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def public_functions():
    names = set()
    for header in glob.glob(os.path.join(ROOT, "include", "maul-rhi", "*.h")):
        with open(header, encoding="utf-8") as file:
            text = file.read()
        names.update(re.findall(r"MRHI_API\s+[^;{(]*?\b(mrhi\w+)\s*\(", text))
    return sorted(names)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: wasm_size.py <libmaul-rhi.a>")
    names = public_functions()
    if not names:
        sys.exit("no public functions found in the headers")
    with tempfile.TemporaryDirectory() as work:
        program = os.path.join(work, "size.js")
        exports = ",".join("_" + name for name in names)
        result = subprocess.run(["emcc", "-Oz", "--no-entry", f"-sEXPORTED_FUNCTIONS={exports}",
                                 "-o", program, sys.argv[1]], capture_output=True, text=True)
        if result.returncode != 0:
            sys.exit(f"emcc failed:\n{result.stdout}{result.stderr}")
        wasm = os.path.getsize(os.path.join(work, "size.wasm"))
        script = os.path.getsize(program)
    print(f"{len(names)} public functions: {wasm} bytes of wasm, {script} of JavaScript; "
          f"budget {BUDGET} bytes of wasm")
    if wasm > BUDGET:
        sys.exit(f"the wasm is {wasm - BUDGET} bytes past the budget")


if __name__ == "__main__":
    main()
