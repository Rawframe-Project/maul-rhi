#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The shader tools this release of Maul RHI pins, and how to tell which
# are installed. tools/mrhi_dxil.py and tools/mrhi_msl.py, which a cook
# outside the tree runs too, refuse a spirv-cross or a DXC other than
# the pin, so that a container cooked with a release's tools was made by
# that release's spirv-cross and DXC; tools/gen_test_shaders.py and
# tools/gen_kernels.py refuse every tool they run that is not its pin.
# `tools/mrhi_container.py --tools` prints the lines below, which a cook
# copies into the stamp it keeps beside its containers.
#
# Standard library only.

import re
import subprocess
import sys

# Each tool's identity as identities() reads it from the installed tool.
# spirv-cross is the family's distribution package, which names no
# revision of its own; DXC is the family's release of
# microsoft/DirectXShaderCompiler, as `dxc --version` names it.
PINNED = {
    "glslang": "glslang 15.1.0",
    "SPIRV-Tools": "SPIRV-Tools v2025.1",
    "spirv-cross": "spirv-cross 2021.01.15+1.3.239.0",
    "dxc": "libdxcompiler 1.9.0.1, 0d3ee6b5",
}
# The tools a cook runs through tools/mrhi_dxil.py and tools/mrhi_msl.py.
CROSSING = ("spirv-cross", "dxc")
# DXC's stamp lines: the release asset of microsoft/DirectXShaderCompiler
# and its SHA-256.
DXC_RELEASE = (
    "dxc linux_dxc_2026_07_29 (v1.9.2607, libdxcompiler 1.9.0.1, 0d3ee6b5)",
    "dxc sha256:55665c87824051ed4774ff3280a79ccbbb7d39243b9736ca5e98222134112d54",
)


def output(*command):
    """What a command prints, or nothing when it is missing or fails."""
    try:
        result = subprocess.run(command, capture_output=True, text=True)
    except OSError:
        return ""
    return result.stdout + result.stderr if result.returncode == 0 else ""


def dxc_identity(dxc="dxc"):
    """The installed DXC as DXC names it, or that it was not found."""
    found = re.search(r"\((\d+)-(\w+)\)\(([\d.]+)\)", output(dxc, "--version"))
    if found is None:
        return "dxc: not found"
    return f"libdxcompiler {found.group(3)}, {found.group(2)}"


def spirv_cross_identity():
    """The installed spirv-cross: its revision, or the version of the
    distribution package that has none."""
    cross = output("spirv-cross", "--revision").strip()
    if cross in ("", "(Debian package)"):
        cross = output("dpkg-query", "-W", "-f=${Version}", "spirv-cross").rsplit("-", 1)[0]
    return f"spirv-cross {cross}" if cross else "spirv-cross: not found"


def identities(names):
    """The installed tools of the names, as PINNED names them."""
    probes = {
        "glslang": lambda: "glslang " + output("glslangValidator", "--version")
        .split("\n")[0].rsplit(":", 1)[-1].strip(),
        "SPIRV-Tools": lambda: " ".join(output("spirv-link", "--version").split()[:2]),
        "spirv-cross": spirv_cross_identity,
        "dxc": dxc_identity,
    }
    return {name: probes[name]() for name in names}


def unpinned(found, names=None):
    """Each tool of the names (every pinned one by default) whose identity
    in found is not exactly its pin, as a line naming both."""
    return [f"{name}: installed {found.get(name)}, pinned {PINNED[name]}"
            for name in (names or PINNED) if found.get(name) != PINNED[name]]


def refuse_unpinned(program, names):
    """Exits, naming each, when an installed tool of the names is not its
    pin."""
    wrong = unpinned(identities(names), names)
    if wrong:
        sys.exit(f"{program}: the installed tools are not the ones this release of Maul RHI "
                 "pins:\n  " + "\n  ".join(wrong))


def pin_lines(dxc_flags):
    """The lines naming the tools a cook runs through this release's
    scripts, and the flags its DXC compiles pass, as this repository's
    stamps write them."""
    return [f"tool {PINNED['spirv-cross']}", *(f"tool {line}" for line in DXC_RELEASE),
            "tool dxc flags " + " ".join(dxc_flags)]
