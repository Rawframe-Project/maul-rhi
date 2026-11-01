#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes fuzz_container's seeds into a directory:
# - every shader container the tests and samples compile in, as the
#   bytes test/shaders/*_container.h and samples/shaders/*_container.h
#   hold;
# - minimal containers of a few hundred bytes, one per feature of the
#   format (MINIMAL below), so that mutations land in the section table
#   and records rather than in compiled code. tools/mrhi_container.py
#   writes them from a reflection and the least code its checks and the
#   library's take: a SPIR-V header with OpCapabilities and
#   OpEntryPoints, a WGSL line per entry, an MSL function per entry, and
#   a DXIL container per entry holding only a program header and a PSV0
#   part. test/test_container_writer.py reads each with the library.
#
#   tools/container_seed.py <corpus dir>

import json
import pathlib
import re
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADERS = ("test/shaders", "samples/shaders")
sys.path.insert(0, str(ROOT / "tools"))
import mrhi_container  # noqa: E402

CAPABILITY_SHADER = 1
CAPABILITY_FLOAT16 = 9
CAPABILITY_INT64 = 11
CAPABILITY_MULTIVIEW = 4439
# PSV0 resource classes: a shader resource view and a sampler.
PSV_SRV = 3
PSV_SAMPLER = 1

VERTEX = {"name": "v", "stage": "vertex",
          "inputs": [{"location": 0, "type": "float32", "components": 3}],
          "variables": [{"location": 0, "type": "float32", "components": 2}]}
FRAGMENT = {"name": "f", "stage": "fragment",
            "variables": [{"location": 0, "type": "float32", "components": 2}],
            "outputs": [{"location": 0, "type": "float32", "components": 4}]}
COMPUTE = {"name": "c", "stage": "compute", "workgroup": [1, 1, 1]}
# Inter-stage variables of every kind WGSL allows: integers flat from
# the first vertex or either, floats linear at the centroid, and 16-bit
# floats per sample.
VARYINGS = [
    {"location": 0, "type": "float32", "components": 2},
    {"location": 1, "type": "sint32", "components": 1},
    {"location": 2, "type": "uint32", "components": 4, "interpolation": "flat",
     "sampling": "either"},
    {"location": 3, "type": "float32", "components": 4, "interpolation": "linear",
     "sampling": "centroid"},
    {"location": 4, "type": "float16", "components": 2, "interpolation": "perspective",
     "sampling": "sample"},
]

# Each seed: its reflection, the SPIR-V capabilities beyond Shader, and
# whether it carries Metal and D3D12 code (neither reads a heap yet).
MINIMAL = {
    "min_compute": ({"entries": [COMPUTE]}, (), True),
    "min_graphics": ({"entries": [VERTEX, FRAGMENT]}, (), True),
    "min_bindings": ({"root_block_bytes": 16, "entries": [COMPUTE], "bindings": [
        {"table": 0, "slot": 0, "kind": "uniform_buffer", "stages": ["compute"], "min_size": 16},
        {"table": 0, "slot": 1, "kind": "storage_buffer", "stages": ["compute"]},
        {"table": 1, "slot": 0, "kind": "read_only_storage_buffer", "stages": ["compute"]},
        {"table": 1, "slot": 1, "kind": "sampler", "stages": ["compute"], "sampler": "filtering"},
        {"table": 2, "slot": 0, "kind": "sampled_texture", "stages": ["compute"],
         "sample_type": "float", "view_dimension": "2d"},
        {"table": 3, "slot": 0, "kind": "storage_texture", "stages": ["compute"],
         "access": "write_only", "format": "rgba8_unorm", "view_dimension": "2d"},
    ]}, (), True),
    "min_constants": ({"entries": [COMPUTE], "constants": [
        {"id": 0, "type": "float32", "default": 1.0},
        {"id": 1, "type": "uint32"},
        {"id": 2, "type": "bool", "default": True},
        {"id": 3, "type": "int32", "default": -1},
    ]}, (), True),
    "min_dual_source": ({"entries": [VERTEX, dict(FRAGMENT, outputs=[
        {"location": 0, "type": "float32", "components": 4},
        {"location": 0, "type": "float32", "components": 4, "blend_src": 1},
    ])]}, (), True),
    "min_heaps": ({"entries": [dict(COMPUTE, heap_uses=["sampled_textures", "samplers"])]},
                  (), False),
    "min_view_index": ({"entries": [dict(VERTEX, builtins=["view_index"]), FRAGMENT]},
                       (CAPABILITY_MULTIVIEW,), True),
    "min_int64": ({"entries": [COMPUTE]}, (CAPABILITY_INT64,), True),
    "min_heap_places": ({"entries": [dict(COMPUTE, heap_uses=["sampled_textures", "samplers"])]},
                        (), False),
    "min_metallib": ({"entries": [COMPUTE]}, (), True),
    "min_interface": ({"entries": [dict(VERTEX, variables=VARYINGS), dict(FRAGMENT, variables=VARYINGS, outputs=[
        {"location": 0, "type": "float32", "components": 4},
        {"location": 1, "type": "uint32", "components": 4},
        {"location": 2, "type": "float16", "components": 4},
    ])]}, (CAPABILITY_FLOAT16,), True),
    "min_textures": ({"entries": [COMPUTE], "bindings": [
        {"table": 0, "slot": 0, "kind": "sampled_texture", "stages": ["compute"],
         "sample_type": "sint", "view_dimension": "2d_array"},
        {"table": 0, "slot": 1, "kind": "sampled_texture", "stages": ["compute"],
         "sample_type": "uint", "view_dimension": "cube"},
        {"table": 0, "slot": 2, "kind": "sampled_texture", "stages": ["compute"],
         "sample_type": "float", "view_dimension": "cube_array"},
        {"table": 0, "slot": 3, "kind": "sampled_texture", "stages": ["compute"],
         "sample_type": "unfilterable_float", "view_dimension": "3d"},
        {"table": 0, "slot": 4, "kind": "sampled_texture", "stages": ["compute"],
         "sample_type": "depth", "view_dimension": "2d", "multisampled": True},
        {"table": 0, "slot": 5, "kind": "sampled_texture", "stages": ["compute"],
         "sample_type": "unfilterable_float", "view_dimension": "2d", "multisampled": True},
        {"table": 1, "slot": 0, "kind": "sampler", "stages": ["compute"],
         "sampler": "non_filtering"},
        {"table": 1, "slot": 1, "kind": "sampler", "stages": ["compute"],
         "sampler": "comparison"},
        {"table": 2, "slot": 0, "kind": "storage_texture", "stages": ["compute"],
         "access": "read_write", "format": "r32_float", "view_dimension": "2d_array"},
        {"table": 2, "slot": 1, "kind": "storage_texture", "stages": ["compute"],
         "access": "read_only", "format": "rgba8_unorm", "view_dimension": "3d"},
    ]}, (), True),
    "min_msl_only": ({"entries": [VERTEX, FRAGMENT]}, (), True),
    "min_dxil_only": ({"entries": [VERTEX, FRAGMENT]}, (), True),
    "min_metallib_only": ({"entries": [COMPUTE]}, (), True),
}

# Seeds that carry the code of one native API only: a container may
# leave out either.
WITHOUT = {"min_msl_only": "dxil", "min_dxil_only": "msl", "min_metallib_only": "msl"}

# The capabilities only native code may declare: a container declaring
# one carries no WGSL.
NATIVE_CAPABILITIES = {CAPABILITY_INT64, CAPABILITY_MULTIVIEW}

# Seeds whose D3D12 heap ranges are not one per class, each a (class,
# space, register): resource ranges of one class on two registers of one
# space and on one register of two spaces, which the library takes; only
# a class repeated on one place is refused.
RANGES = {
    "min_heap_places": [(PSV_SRV, 16, 0), (PSV_SRV, 16, 1), (PSV_SRV, 18, 0), (PSV_SAMPLER, 17, 0)],
}

# Seeds that carry a Metal library beside their MSL: its magic and
# version, which the library checks.
METALLIBS = {"min_metallib": b"MTLB\1\0\0\0", "min_metallib_only": b"MTLB\1\0\0\0"}


def compiled(header):
    text = header.read_text()
    body = text[text.index("{") + 1 : text.index("};")]
    data = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", body))
    declared = int(re.search(r"s_\w+Container\[(\d+)\]", text).group(1))
    if len(data) != declared:
        sys.exit(f"container_seed: read {len(data)} bytes of {header}, it declares {declared}")
    return data


def instruction(opcode, *operands):
    return [(len(operands) + 1) << 16 | opcode, *operands]


def spirv(entries, capabilities):
    words = [mrhi_container.SPIRV_MAGIC, 0x00010300, 0, 100, 0]
    for capability in (CAPABILITY_SHADER, *capabilities):
        words += instruction(mrhi_container.OP_CAPABILITY, capability)
    models = {stage: model for model, stage in mrhi_container.EXECUTION_MODELS.items()}
    for i, entry in enumerate(entries):
        name = entry["name"].encode() + b"\0"
        name += bytes(-len(name) % 4)
        words += instruction(mrhi_container.OP_ENTRY_POINT, models[entry["stage"]], 10 + i,
                             *struct.unpack(f"<{len(name) // 4}I", name))
    return struct.pack(f"<{len(words)}I", *words)


def wgsl(entries):
    lines = []
    for entry in entries:
        size = "@workgroup_size(1) " if entry["stage"] == "compute" else ""
        lines.append(f"@{entry['stage']} {size}fn {entry['name']}() {{}}\n")
    return "".join(lines).encode()


def msl(entries):
    keyword = mrhi_container.MSL_KEYWORDS
    return {e["name"]: f"{keyword[e['stage']]} void {e['name']}() {{}}\n".encode()
            for e in entries}


def dxil(entry, ranges):
    """A DXIL container of the entry's stage: a program header and a PSV0
    part declaring the heap ranges given as (class, space, register)."""
    kind = mrhi_container.DXIL_KINDS[entry["stage"]]
    program = struct.pack("<I4x", kind << 16)
    records = b"".join(struct.pack("<4I", cls, space, reg, 0xFFFFFFFF)
                       for cls, space, reg in ranges)
    psv = struct.pack("<II", 0, len(ranges)) + (struct.pack("<I", 16) if ranges else b"")
    psv += records
    parts = [(b"DXIL", program), (b"PSV0", psv)]
    head = 32 + 4 * len(parts)
    offsets, body = [], b""
    for fourcc, data in parts:
        offsets.append(head + len(body))
        body += struct.pack("<4sI", fourcc, len(data)) + data
    size = head + len(body)
    return (b"DXBC" + bytes(20) + struct.pack("<II", size, len(parts)) +
            struct.pack(f"<{len(parts)}I", *offsets) + body)


def minimal(enums):
    """Each minimal seed's name, bytes and what the library reads of it:
    entries, bindings, root block bytes, heap uses, and the SPIR-V
    capabilities it declares beyond Shader, which a device must grant."""
    uses = {"sampled_textures": 1, "storage_textures": 2, "storage_buffers": 4, "samplers": 8,
            "writes": 16}
    seeds = []
    for name, (reflection, capabilities, native) in MINIMAL.items():
        entries = reflection["entries"]
        code = spirv(entries, capabilities)
        heaps = {use for e in entries for use in e.get("heap_uses", [])}
        bare = heaps or NATIVE_CAPABILITIES.intersection(capabilities)
        ranges = []
        if "sampled_textures" in heaps:
            ranges.append((PSV_SRV, 16, 0))
        if "samplers" in heaps:
            ranges.append((PSV_SAMPLER, 17, 0))
        ranges = RANGES.get(name, ranges)
        blobs = {e["name"]: dxil(e, ranges) for e in entries}
        without = WITHOUT.get(name)
        data = mrhi_container.build(code, None if bare else wgsl(entries), reflection, enums,
                                    msl=msl(entries) if native and without != "msl" else None,
                                    metallib=METALLIBS.get(name),
                                    dxil=blobs if (native or ranges) and without != "dxil"
                                    else None)
        heap_bits = 0
        for use in heaps:
            heap_bits |= uses[use]
        seeds.append((name, data, (len(entries), len(reflection.get("bindings", [])),
                                   reflection.get("root_block_bytes", 0), heap_bits,
                                   tuple(capabilities))))
    return seeds


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: container_seed.py <corpus dir>")
    directory = pathlib.Path(sys.argv[1])
    directory.mkdir(parents=True, exist_ok=True)
    for folder in HEADERS:
        for header in sorted((ROOT / folder).glob("*_container.h")):
            name = header.name.removesuffix("_container.h")
            (directory / f"{name}.mrsc").write_bytes(compiled(header))
    with open(mrhi_container.CONTRACT, encoding="utf-8") as f:
        enums = mrhi_container.Enums(json.load(f))
    for name, data, _ in minimal(enums):
        (directory / f"{name}.mrsc").write_bytes(data)


if __name__ == "__main__":
    main()
