#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes every shader container the tests and samples compile in, as the
# bytes test/shaders/*_container.h and samples/shaders/*_container.h
# hold, into a directory: the seeds of fuzz_container's corpus. Each
# reaches other parts of the format (vertex inputs, a second blend
# source, heap uses, 16-bit and 64-bit types, subgroups, multiview,
# storage bindings), so mutations start from every feature rather than
# from the conformance suite's container alone.
#
#   tools/container_seed.py <corpus dir>

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADERS = ("test/shaders", "samples/shaders")


def read(header):
    text = header.read_text()
    body = text[text.index("{") + 1 : text.index("};")]
    data = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", body))
    declared = int(re.search(r"s_\w+Container\[(\d+)\]", text).group(1))
    if len(data) != declared:
        sys.exit(f"container_seed: read {len(data)} bytes of {header}, it declares {declared}")
    return data


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: container_seed.py <corpus dir>")
    directory = pathlib.Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    for folder in HEADERS:
        for header in sorted((ROOT / folder).glob("*_container.h")):
            name = header.name.removesuffix("_container.h")
            (directory / f"{name}.mrsc").write_bytes(read(header))


if __name__ == "__main__":
    main()
