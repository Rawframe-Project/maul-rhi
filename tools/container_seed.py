#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes the conformance suite's shader container, as the bytes
# test/shaders/conformance_container.h holds, into a directory: the seed
# of fuzz_container's corpus.
#
#   tools/container_seed.py <corpus dir>

import pathlib
import re
import sys

HEADER = pathlib.Path(__file__).resolve().parent.parent / "test/shaders/conformance_container.h"


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: container_seed.py <corpus dir>")
    text = HEADER.read_text()
    body = text[text.index("{") + 1 : text.index("};")]
    data = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", body))
    declared = int(re.search(r"s_conformanceContainer\[(\d+)\]", text).group(1))
    if len(data) != declared:
        sys.exit(f"container_seed: read {len(data)} bytes, the header declares {declared}")
    directory = pathlib.Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "conformance.mrsc").write_bytes(data)


if __name__ == "__main__":
    main()
