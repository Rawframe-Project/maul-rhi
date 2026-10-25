#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# tools/mrhi_dxil.py's stale_reads: SPIRV-Cross's HLSL reading a loop's
# variable after its continue block stepped it is found, as a scene's
# light loop once read the next light's intensity, and with a decrement
# too; the same loop with the read made in its body, a step at a loop's
# end, and a step of the body's own followed by a read of the stepped
# value, as maul-vfx's kernels write them, are not.
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

# A count stepped down in a continue block, then read in the next
# statement of it.
STALE_DOWN = """
        for (;;)
        {
            if (!(_40 > 0u))
            {
                break;
            }
            _41 += _data.Load(_40 * 4 + 0);
            _40--;
            _42 = max(_42, _data.Load(_40 * 4 + 0));
            continue;
        }
"""

# Steps of a loop body's own, each followed by a read of the stepped value
# as the WGSL makes it, in the shapes maul-vfx's kernels cross to.
ACCEPTED = {
    # sim/convex.wgsl: a swap-remove, the last edge moved into the hole.
    "a swap-remove": """
        for (;;)
        {
            if (_12030 == _12031)
            {
                _12027--;
                _9.Store(_12031 * 4 + 0, _9.Load(_12027 * 4 + 0));
            }
            _12031++;
            continue;
        }
""",
    # plexus.wgsl: a count tested against the room once stepped.
    "a count then a test": """
        for (;;)
        {
            if (_371)
            {
                _43.Store((2u * (_341 + _355)) * 4 + 0, _338);
            }
            _355++;
            if (_340)
            {
                _373 = _355 == _342;
            }
            else
            {
                _373 = false;
            }
            if (_373)
            {
                return _355;
            }
            uint _571 = _363;
            _363 = _571 + 1u;
            continue;
        }
""",
    # ribbon_records.wgsl: a chain's next word, written once stepped.
    "a chain's next word": """
        for (;;)
        {
            if (_2317(_2466))
            {
                _2464++;
                _81.Store(_2279(2u, _2464) * 4 + 0, _2466);
            }
            _81.Store(_2279(0u, _2466) * 4 + 0, _2464);
            _2466++;
            continue;
        }
""",
    # sim/ribbons.wgsl: a walk back, each step's slot tested.
    "a walk back": """
        for (;;)
        {
            if (!(_520 > 0u))
            {
                break;
            }
            _520--;
            if ((_source.Load((_519 + _520) * 4 + 0) & 2u) == 0u)
            {
                break;
            }
            continue;
        }
""",
    # sim/ribbons.wgsl: the latest kept, chosen once the count stepped.
    "a kept count": """
        for (;;)
        {
            if (_610)
            {
                _611++;
                _612 = select(_612, _611, _613);
            }
            _614++;
            continue;
        }
""",
    # grid.wgsl: a path walk wrapping its cell, before the loop's own step.
    "a wrapping walk": """
        for (;;)
        {
            _2193 = _2247;
            _2197++;
            if (_2197 == _39_m0._m7.z)
            {
                if (!_2190)
                {
                    return;
                }
                _2197 = 0u;
            }
            _2203++;
            continue;
        }
""",
}


def main():
    path = os.path.join(ROOT, "tools", "mrhi_dxil.py")
    spec = importlib.util.spec_from_file_location("mrhi_dxil", path)
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    failed = 0
    cases = 0
    for source, expected, what in ((STALE, [(12, "_5736")], "the stale read"),
                                   (STALE_DOWN, [(10, "_40")], "the stale read stepping down"),
                                   (KEPT, [], "the loop read in its body")):
        cases += 1
        found = tool.stale_reads(source)
        if found != expected:
            print(f"FAIL {what}: {found}")
            failed += 1
    for what, source in ACCEPTED.items():
        cases += 1
        found = tool.stale_reads(source)
        if found:
            print(f"FAIL {what}: {found}")
            failed += 1
    print(f"{cases - failed} of {cases} passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
