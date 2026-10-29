#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Builds the shader containers of the conformance suite and the samples
# the way a program's cook would, offline: test/shaders/conformance.*
# (vert, frag, comp, placed.vert, root.frag, add.comp, wgsl, json),
# test/shaders/bindless.{comp,json}, whose entry reads heaps and so has
# no WGSL or MSL, test/shaders/{f16,subgroups,int64}.*, one optional
# shader feature each (int64 without WGSL, which has no 64-bit integers),
# test/shaders/instanced.*, a color per instance, test/shaders/dual.*, two
# blend sources, and each sample's samples/shaders/NAME.*. glslangValidator
# compiles each stage under its entry name, spirv-link joins them into
# one module, spirv-val checks it for Vulkan 1.3, tools/mrhi_msl.py
# crosses each entry of a container with WGSL to MSL through
# spirv-cross, tools/mrhi_dxil.py compiles every entry to DXIL through
# spirv-cross and dxc, and tools/mrhi_container.py writes the
# container, which lands beside its sources in NAME_container.h as
# bytes. CI runs none of these tools; the
# headers are committed, and this script is run again whenever a source
# changes.
#
# Each header carries a stamp (tools/shader_stamp.py): the digest of its
# inputs, the pinned tools that made it, and the digest of its bytes.
# The script refuses to generate with tools other than PINS, and --check
# tells, with no tools, whether every header still matches its inputs,
# the pins and its stamp; --self-test proves that check on a copy of the
# tree (--skip-inputs turns off the input comparison, which the
# self-test must then fail; it is refused without --self-test).
# docs/operations/development.md names where each pinned tool comes from.
#
# usage: gen_test_shaders.py [--check | --self-test [--skip-inputs]]

import argparse
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import shader_stamp  # noqa: E402

# The tools every committed header was made by, as tool_identities()
# reads them, DXC the family's (shader_stamp.DXC).
PINNED = {
    "glslang": "glslang 15.1.0",
    "SPIRV-Tools": "SPIRV-Tools v2025.1",
    "spirv-cross": "spirv-cross 2021.01.15+1.3.239.0",
    "dxc": shader_stamp.DXC_IDENTITY,
}
# The stamp's tool lines.
PINS = (PINNED["glslang"], PINNED["SPIRV-Tools"], PINNED["spirv-cross"], *shader_stamp.DXC)
# The scripts every container is made by, and the contract's values the
# writer reads; mrhi_msl.py too for those with MSL.
SCRIPTS = ("tools/gen_test_shaders.py", "tools/shader_stamp.py", "tools/mrhi_container.py",
           "tools/mrhi_dxil.py", shader_stamp.CONTRACT_VALUES)
# Each container: its directory, its name, its stages' sources and
# entries, and whether it has WGSL (a container reading heaps or the
# view index has none); a container reading no heap has MSL, and every
# container has DXIL.
CONTAINERS = (
    ("test/shaders", "conformance", (("vert", "vs"), ("frag", "fs"), ("comp", "cs"),
                                     ("placed.vert", "vp"), ("root.frag", "fr"),
                                     ("add.comp", "ca")), True),
    ("test/shaders", "bindless", (("comp", "cs"),), False),
    ("test/shaders", "multiview", (("vert", "vs"), ("frag", "fs")), False),
    ("test/shaders", "noop", (("vert", "vs"), ("frag", "fs"), ("comp", "cs")), True),
    ("test/shaders", "storage", (("comp", "cs"),), True),
    ("test/shaders", "f16", (("comp", "cs"),), True),
    ("test/shaders", "subgroups", (("comp", "cs"),), True),
    ("test/shaders", "int64", (("comp", "cs"),), False),
    ("test/shaders", "instanced", (("vert", "vs"), ("frag", "fs")), True),
    ("test/shaders", "dual", (("vert", "vs"), ("frag", "fs")), True),
    ("samples/shaders", "triangle", (("vert", "vs"), ("frag", "fs")), True),
    ("samples/shaders", "textured", (("vert", "vs"), ("frag", "fs")), True),
    ("samples/shaders", "compute_plan", (("comp", "plan"),), True),
    ("samples/shaders", "compute_square", (("comp", "square"),), True),
    ("samples/shaders", "shadow_depth", (("vert", "place"),), True),
    ("samples/shaders", "shadow_scene", (("vert", "place"), ("lit.frag", "lit"),
                                         ("solid.frag", "solid")), True),
)


def tool_identities():
    """The installed tools, as PINS names them."""
    output = shader_stamp.output
    glslang = output("glslangValidator", "--version").split("\n")[0].rsplit(":", 1)[-1]
    tools = " ".join(output("spirv-link", "--version").split()[:2])
    cross = output("spirv-cross", "--revision").strip()
    if cross in ("", "(Debian package)"):
        cross = output("dpkg-query", "-W", "-f=${Version}", "spirv-cross").rsplit("-", 1)[0]
    return {"glslang": f"glslang {glslang.strip()}", "SPIRV-Tools": tools,
            "spirv-cross": f"spirv-cross {cross}", "dxc": shader_stamp.dxc_identity()}


def units(root):
    """Each header and the inputs it is made from, relative to the root."""
    found = []
    for folder, name, stages, wgsl in CONTAINERS:
        inputs = [f"{folder}/{name}.{suffix}" for suffix, _ in stages]
        inputs.append(f"{folder}/{name}.json")
        if wgsl:
            inputs.append(f"{folder}/{name}.wgsl")
        with open(os.path.join(root, folder, f"{name}.json"), encoding="utf-8") as file:
            heaps = any(e.get("heap_uses") for e in json.load(file)["entries"])
        inputs += list(SCRIPTS) + ([] if heaps else ["tools/mrhi_msl.py"])
        found.append((f"{folder}/{name}_container.h", inputs))
    return found


def contract_edit(emitted):
    """A self-test change to a copy's contract: swapping the values of the
    first two sample types, which containers emit, or only rewording that
    enum's doc."""
    def change(work):
        path = os.path.join(work, "contract", "mrhi.json")
        with open(path, encoding="utf-8") as file:
            contract = json.load(file)
        item = next(item for header in contract["headers"] for item in header["items"]
                    if item.get("name") == "sample_type")
        if emitted:
            first, second = item["values"][1], item["values"][2]
            first["value"], second["value"] = second["value"], first["value"]
        else:
            item["doc"] += " Reworded."
        with open(path, "w", encoding="utf-8", newline="\n") as file:
            json.dump(contract, file, indent=2)
    return change


def run(*command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"{command[0]} failed:\n{result.stdout}{result.stderr}")


def build(work, shaders, name, stages, wgsl):
    modules = []
    for suffix, entry in stages:
        module = os.path.join(work, f"{name}_{entry}.spv")
        run("glslangValidator", "-V", "--target-env", "vulkan1.3", "-e", entry,
            "--source-entrypoint", "main", "-o", module,
            os.path.join(shaders, f"{name}.{suffix}"))
        modules.append(module)
    linked = os.path.join(work, f"{name}.spv")
    run("spirv-link", "--target-env", "vulkan1.3", *modules, "-o", linked)
    run("spirv-val", "--target-env", "vulkan1.3", linked)
    container = os.path.join(work, f"{name}.mrsc")
    reflection = os.path.join(shaders, f"{name}.json")
    dxil = os.path.join(work, "dxil")
    run(sys.executable, os.path.join(ROOT, "tools", "mrhi_dxil.py"), linked, reflection, dxil)
    code = ["--dxil", dxil]
    with open(reflection, encoding="utf-8") as file:
        heaps = any(e.get("heap_uses") for e in json.load(file)["entries"])
    if not heaps:
        msl = os.path.join(work, "msl")
        run(sys.executable, os.path.join(ROOT, "tools", "mrhi_msl.py"), linked, reflection, msl)
        code += ["--msl", msl]
    run(sys.executable, os.path.join(ROOT, "tools", "mrhi_container.py"), *code, linked,
        os.path.join(shaders, f"{name}.wgsl") if wgsl else "-",
        reflection, container)
    with open(container, "rb") as file:
        return file.read()


def header(folder, name, data, stamp):
    guard = f"MAUL_RHI_TEST_{name.upper()}_CONTAINER_H"
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Copyright (c) 2026 Sirac Ozmen",
        "//",
        "// A shader container, generated by tools/gen_test_shaders.py",
        f"// from {folder}/{name}.*: edit the sources and run the script,",
        "// not this file.",
        "//",
        *stamp,
        "",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "#include <stdalign.h>",
        "#include <stdint.h>",
        "",
        f"alignas(8) static const uint8_t s_{name}Container[{len(data)}] = {{",
    ]
    for at in range(0, len(data), 16):
        lines.append("    " + " ".join(f"0x{b:02X}," for b in data[at:at + 16]))
    lines += ["};", "", f"#endif // {guard}", ""]
    return "\n".join(lines)


def parse(argv):
    """The arguments. --skip-inputs is refused without --self-test, where
    it would quietly turn off the input comparison of --check."""
    parser = argparse.ArgumentParser(prog="gen_test_shaders.py")
    parser.add_argument("--check", action="store_true",
                        help="check every header against its inputs and the pins, with no tools")
    parser.add_argument("--self-test", action="store_true",
                        help="prove the check on a copy of the tree")
    parser.add_argument("--skip-inputs", action="store_true",
                        help="with --self-test only: leave out the input comparison, which the "
                        "self-test must fail")
    args = parser.parse_args(argv)
    if args.skip_inputs and not args.self_test:
        parser.error("--skip-inputs goes only with --self-test")
    return args


def main(argv=None):
    args = parse(argv)
    if args.check or args.self_test:
        if args.self_test:
            cases = (("a changed contract value", contract_edit(True), {"inputs"}),
                     ("the contract's prose reworded", contract_edit(False), set()))
            return shader_stamp.self_test(ROOT, ("tools", "test/shaders", "samples/shaders",
                                                 "contract"),
                                          units, PINS, parse, not args.skip_inputs, cases)
        return shader_stamp.check(ROOT, units(ROOT), PINS, "tools/gen_test_shaders.py",
                                  not args.skip_inputs)
    shader_stamp.refuse_unpinned(tool_identities(), PINNED)
    stamped = dict(units(ROOT))
    for folder, name, stages, wgsl in CONTAINERS:
        shaders = os.path.join(ROOT, folder)
        with tempfile.TemporaryDirectory() as work:
            data = build(work, shaders, name, stages, wgsl)
        relative = f"{folder}/{name}_container.h"
        stamp = shader_stamp.stamp_lines(ROOT, stamped[relative], PINS, data)
        path = os.path.join(ROOT, relative)
        with open(path, "w", encoding="utf-8", newline="\n") as file:
            file.write(header(folder, name, data, stamp))
        print(f"{relative}: {len(data)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
