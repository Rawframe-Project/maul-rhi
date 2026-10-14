#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# tools/mrhi_dxil.py's stale_reads: SPIRV-Cross's HLSL reading a loop's
# variable after its continue block stepped it is found, as a scene's
# light loop once read the next light's intensity; the same loop with
# the read made in its body, and a step at a loop's end, are not.
#
# usage: test_dxil_order.py

import importlib.util
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

STALE = """
        uint _5736 = _3886.x;
        float3 _5737 = 0.0f.xxx;
        for (;;)
        {
            if (!(_5736 < (_3886.x + _3886.y)))
            {
                break;
            }
            float3 _5767 = asfloat(lights.Load4(_indices.Load(_5736 * 4 + 0) * 80 + 0)).xyz - _11417;
            _5736++;
            _5737 += (asfloat(lights.Load4(_indices.Load(_5736 * 4 + 0) * 80 + 16)).xyz * _5741);
            continue;
        }
"""

KEPT = """
        uint _5730 = _3880.x;
        float3 _5731 = 0.0f.xxx;
        for (;;)
        {
            if (!(_5730 < (_3880.x + _3880.y)))
            {
                break;
            }
            if (_5767 > 0.0f)
            {
                _5731 += (asfloat(lights.Load4(_indices.Load(_5730 * 4 + 0) * 80 + 16)).xyz * _5734);
            }
            _5730++;
            continue;
        }
        for (uint _12 = 0u; _12 < 4u; _12++)
        {
            _13 += _12;
        }
"""


def main():
    path = os.path.join(ROOT, "tools", "mrhi_dxil.py")
    spec = importlib.util.spec_from_file_location("mrhi_dxil", path)
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    failed = 0
    found = tool.stale_reads(STALE)
    if found != [(12, "_5736")]:
        print(f"FAIL the stale read: {found}")
        failed += 1
    found = tool.stale_reads(KEPT)
    if found:
        print(f"FAIL the loop read in its body: {found}")
        failed += 1
    print(f"{2 - failed} of 2 passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
