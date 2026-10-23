#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The generated reference pages tools/build_docs.py adds to the
# documentation site after the API reference: how every concept of the
# contract maps onto Vulkan, D3D12, Metal and WebGPU, each row classed
# direct, emulated, restricted or absent-rejected. Generated from
# contract/mrhi.json by tools/gen_contract.py and never committed.

import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_contract  # noqa: E402


def pages():
    """(output name, navigation title, Markdown) of each page."""
    with open(os.path.join(gen_contract.ROOT, gen_contract.CONTRACT), encoding="utf-8") as f:
        contract = json.load(f)
    return [("mappings", "Mappings", gen_contract.emit_mappings(contract))]
