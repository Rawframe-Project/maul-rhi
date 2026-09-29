#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# tools/mrhi_container.py against the library: a container it writes
# is read by test_shader with the same digest and reflection, as is one
# whose entries use heaps and so has no WGSL, and code that disagrees
# with its reflection is refused.
#
# usage: test_container_writer.py TEST_SHADER

import copy
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WRITER = os.path.join(ROOT, "tools", "mrhi_container.py")

WGSL = """
// A comment naming @group(3) @binding(9) var<uniform> nothing: u32;
struct Camera { view: mat4x4f }
@group(0) @binding(0) var<uniform> camera: Camera;
@group(0) @binding(1) var linear: sampler;
@binding(2) @group(0) var albedo: texture_2d<f32>;
@group(1) @binding(0) var<storage, read_write> counts: array<u32>;
@group(1) @binding(1) var image: texture_storage_2d<rgba8unorm, write>;
override scale: f32 = 1.0;
/* nested /* comments */ @vertex fn hidden() {} */
@vertex fn vs(@location(0) position: vec3f) -> @builtin(position) vec4f {
    return camera.view * vec4f(position * scale, 1.0);
}
@fragment fn fs(@location(0) uv: vec2f) -> @location(0) vec4f {
    return textureSample(albedo, linear, uv);
}
@compute @workgroup_size(8u, 8) fn cs(@builtin(global_invocation_id) id: vec3u) {
    counts[id.x] += 1u;
    textureStore(image, id.xy, vec4f(1.0));
}
"""

REFLECTION = {
    "root_block_bytes": 16,
    "entries": [
        {"name": "vs", "stage": "vertex",
         "inputs": [{"location": 0, "type": "float32", "components": 3}],
         "variables": [{"location": 0, "type": "float32", "components": 2},
                       {"location": 1, "type": "uint32", "components": 1}]},
        {"name": "fs", "stage": "fragment", "builtins": ["front_facing"],
         "variables": [{"location": 0, "type": "float32", "components": 2,
                        "interpolation": "linear", "sampling": "centroid"}],
         "outputs": [{"location": 0, "type": "float32", "components": 4}]},
        {"name": "cs", "stage": "compute", "workgroup": [8, 8, 1],
         "workgroup_storage_bytes": 1024},
    ],
    "bindings": [
        {"table": 0, "slot": 0, "kind": "uniform_buffer", "stages": ["vertex"], "min_size": 64},
        {"table": 0, "slot": 1, "kind": "sampler", "stages": ["fragment"],
         "sampler": "filtering"},
        {"table": 0, "slot": 2, "kind": "sampled_texture", "stages": ["fragment"],
         "sample_type": "float", "view_dimension": "2d"},
        {"table": 1, "slot": 0, "kind": "storage_buffer", "stages": ["compute"]},
        {"table": 1, "slot": 1, "kind": "storage_texture", "stages": ["compute"],
         "access": "write_only", "format": "rgba8_unorm", "view_dimension": "2d"},
        {"table": 2, "slot": 0, "kind": "sampler", "stages": ["fragment"],
         "sampler": "comparison"},
    ],
    "constants": [{"id": 0, "type": "float32", "default": 1.0},
                  {"id": 1, "type": "int32", "default": -2},
                  {"id": 2, "type": "bool", "default": True},
                  {"id": 3, "type": "uint32"}],
}


def instruction(opcode, *operands):
    return [(len(operands) + 1) << 16 | opcode, *operands]


def name_words(name):
    raw = name.encode("utf-8") + b"\0"
    raw += bytes(-len(raw) % 4)
    return list(struct.unpack(f"<{len(raw) // 4}I", raw))


def spirv(entries, bindings):
    """A module header, OpEntryPoints and binding decorations: all the
    writer reads."""
    words = [0x07230203, 0x00010300, 0, 100, 0]
    models = {"vertex": 0, "fragment": 4, "compute": 5}
    for i, (name, stage) in enumerate(entries):
        words += instruction(15, models[stage], 10 + i, *name_words(name))
    for i, (group, slot) in enumerate(bindings):
        words += instruction(71, 50 + i, 34, group)
        words += instruction(71, 50 + i, 33, slot)
    return struct.pack(f"<{len(words)}I", *words)


SPIRV_ENTRIES = [("vs", "vertex"), ("fs", "fragment"), ("cs", "compute")]
SPIRV_BINDINGS = [(0, 0), (0, 2), (1, 1)]


def section(data, wanted):
    """A section's bytes, or None."""
    count = struct.unpack_from("<I", data, 48)[0]
    for i in range(count):
        kind, _, offset, size = struct.unpack_from("<IIQQ", data, 64 + 24 * i)
        if kind == wanted:
            return data[offset:offset + size]
    return None


def constants(data):
    """The constant records of a container: id, type, bits and whether
    it is required."""
    records = section(data, 7) or b""
    return [struct.unpack_from("<IB3xIB3x", records, at) for at in range(0, len(records), 16)]


def write(folder, code, text, reflection):
    """Writes a container; text None passes no WGSL."""
    paths = [os.path.join(folder, n) for n in ("a.spv", "a.wgsl", "a.json", "a.mrsc")]
    with open(paths[0], "wb") as f:
        f.write(code)
    if text is None:
        paths[1] = "-"
    else:
        with open(paths[1], "w", encoding="utf-8") as f:
            f.write(text)
    with open(paths[2], "w", encoding="utf-8") as f:
        json.dump(reflection, f)
    if os.path.exists(paths[3]):
        os.remove(paths[3])
    result = subprocess.run([sys.executable, WRITER, *paths], capture_output=True, text=True)
    return result.returncode, result.stderr, paths[3]


def main():
    test_shader = sys.argv[1]
    failures = []

    def check(condition, message):
        if not condition:
            failures.append(message)
            print(f"FAIL: {message}")

    with tempfile.TemporaryDirectory() as folder:
        code = spirv(SPIRV_ENTRIES, SPIRV_BINDINGS)
        status, errors, output = write(folder, code, WGSL, REFLECTION)
        check(status == 0, f"the container is written: {errors}")
        if status == 0:
            with open(output, "rb") as f:
                data = f.read()
            digest = hashlib.sha256(data[48:]).hexdigest()
            args = [test_shader, output, digest, "3", str(len(REFLECTION["bindings"])), "16", "0"]
            result = subprocess.run(args, capture_output=True, text=True)
            check(result.returncode == 0, f"the library reads it: {result.stdout}")
            check(constants(data) == [(0, 4, 0x3F800000, 0), (1, 2, 0xFFFFFFFE, 0),
                                      (2, 1, 1, 0), (3, 3, 0, 1)],
                  "the constants' types, default bits and required flags")
            entries = section(data, 3) or b""
            check([struct.unpack_from("<36xII", entries, at) for at in range(0, len(entries), 48)]
                  == [(0, 0), (4, 0), (0, 1024)],
                  "the entries' builtins and workgroup storage")
            check(section(data, 10) == struct.pack("<IBBBBIBBBBIBBBB", 0, 1, 2, 1, 1,
                                                   1, 4, 1, 3, 4, 0, 1, 2, 2, 2),
                  "the inter-stage variables, with WGSL's default interpolation")

        # Heaps: fs samples through both heaps, cs writes storage buffers.
        heaped = copy.deepcopy(REFLECTION)
        heaped["entries"][1]["heap_uses"] = ["sampled_textures", "samplers"]
        heaped["entries"][2]["heap_uses"] = ["storage_buffers", "writes"]
        heap_code = spirv(SPIRV_ENTRIES, SPIRV_BINDINGS + [(4, 0), (4, 1)])
        status, errors, output = write(folder, heap_code, None, heaped)
        check(status == 0, f"a container using heaps is written: {errors}")
        if status == 0:
            with open(output, "rb") as f:
                data = f.read()
            digest = hashlib.sha256(data[48:]).hexdigest()
            args = [test_shader, output, digest, "3", str(len(REFLECTION["bindings"])), "16",
                    str(1 | 4 | 8 | 16)]
            result = subprocess.run(args, capture_output=True, text=True)
            check(result.returncode == 0, f"the library reads it: {result.stdout}")
            entries = section(data, 3) or b""
            check([struct.unpack_from("<44xI", entries, at)[0]
                   for at in range(0, len(entries), 48)] == [0, 1 | 8, 4 | 16],
                  "the entries' heap uses")
            check(section(data, 9) is None, "no WGSL section")

        def heap_refused(what, code=heap_code, text=None, change=None):
            reflection = copy.deepcopy(heaped)
            if change:
                change(reflection)
            status, _, output = write(folder, code, text, reflection)
            check(status == 1 and not os.path.exists(output), f"refused: {what}")

        heap_refused("WGSL beside a heap", text=WGSL)
        heap_refused("no WGSL without a heap", code=code,
                     change=lambda r: [e.pop("heap_uses", None) for e in r["entries"]])
        heap_refused("an unknown heap use",
                     change=lambda r: r["entries"][1].update(heap_uses=["samplers",
                                                                         "uniform_buffers"]))
        heap_refused("a repeated heap use",
                     change=lambda r: r["entries"][1].update(heap_uses=["samplers", "samplers"]))
        heap_refused("heap writes without a storage kind",
                     change=lambda r: r["entries"][1].update(heap_uses=["sampled_textures",
                                                                         "samplers", "writes"]))
        heap_refused("heap writes in a vertex entry",
                     change=lambda r: r["entries"][0].update(heap_uses=["storage_textures",
                                                                         "writes"]))
        heap_refused("the sampler heap bound without samplers",
                     change=lambda r: r["entries"][1].update(heap_uses=["sampled_textures"]))
        heap_refused("the resource heap bound without resources",
                     code=spirv(SPIRV_ENTRIES, SPIRV_BINDINGS + [(4, 0), (4, 1)]),
                     change=lambda r: [r["entries"][1].update(heap_uses=["samplers"]),
                                       r["entries"][2].pop("heap_uses")])
        heap_refused("another binding of the heap set",
                     code=spirv(SPIRV_ENTRIES, SPIRV_BINDINGS + [(4, 0), (4, 2)]))

        def refused(what, code=code, text=WGSL, change=None):
            reflection = copy.deepcopy(REFLECTION)
            if change:
                change(reflection)
            status, _, output = write(folder, code, text, reflection)
            check(status == 1 and not os.path.exists(output), f"refused: {what}")

        refused("a WGSL entry the reflection lacks", text=WGSL + "@fragment fn extra() {}")
        refused("a WGSL entry of another stage", text=WGSL.replace("@fragment fn fs", "@vertex fn fs"))
        refused("a WGSL workgroup size", text=WGSL.replace("(8u, 8)", "(8u, 4)"))
        refused("a WGSL workgroup size from an override",
                text=WGSL.replace("@workgroup_size(8u, 8)", "@workgroup_size(8u, 8, side)")
                .replace("override scale", "override side: u32 = 8;\noverride scale"))
        refused("a WGSL binding the reflection lacks",
                text=WGSL + "@group(3) @binding(0) var extra: sampler;")
        refused("a WGSL binding of another kind",
                text=WGSL.replace("var<storage, read_write> counts", "var<storage> counts"))
        refused("a WGSL texture of another dimension",
                text=WGSL.replace("albedo: texture_2d<f32>", "albedo: texture_3d<f32>"))
        refused("a WGSL comparison sampler declared plain",
                text=WGSL.replace("var linear: sampler", "var linear: sampler_comparison"))
        refused("a SPIR-V entry the reflection lacks",
                code=spirv(SPIRV_ENTRIES + [("extra", "compute")], SPIRV_BINDINGS))
        refused("a SPIR-V entry of another stage",
                code=spirv([("vs", "fragment"), ("fs", "fragment"), ("cs", "compute")],
                           SPIRV_BINDINGS))
        refused("a SPIR-V binding the reflection lacks",
                code=spirv(SPIRV_ENTRIES, SPIRV_BINDINGS + [(3, 3)]))
        refused("damaged SPIR-V", code=code[:-2])
        refused("a repeated binding",
                change=lambda r: r["bindings"].append(dict(r["bindings"][0])))
        refused("a vertex stage writing storage",
                change=lambda r: r["bindings"][3]["stages"].append("vertex"))
        refused("an unknown scalar type",
                change=lambda r: r["entries"][0]["inputs"][0].update(type="float64"))
        refused("an unknown key", change=lambda r: r["entries"][0]["inputs"][0].update(format=1))
        refused("an unknown top-level key", change=lambda r: r.update(root_block=16))
        refused("an output past the color targets",
                change=lambda r: r["entries"][1]["outputs"][0].update(location=8))
        refused("a root block past 256", change=lambda r: r.update(root_block_bytes=260))
        refused("a multisampled filterable texture",
                change=lambda r: r["bindings"][2].update(multisampled=True))
        refused("a boolean default that is a number",
                change=lambda r: r["constants"][2].update(default=1))
        refused("an interpolated integer",
                change=lambda r: r["entries"][0]["variables"][1].update(interpolation="linear"))
        refused("flat at the centroid",
                change=lambda r: r["entries"][1]["variables"][0].update(interpolation="flat"))
        refused("a 16-bit float vertex input",
                change=lambda r: r["entries"][0]["inputs"][0].update(type="float16"))
        refused("variables on a compute entry",
                change=lambda r: r["entries"][2].update(variables=[r["entries"][1]["variables"][0]]))
        refused("a vertex builtin", change=lambda r: r["entries"][0].update(builtins=["frag_depth"]))
        refused("an unknown builtin",
                change=lambda r: r["entries"][1].update(builtins=["position"]))
        refused("five components",
                change=lambda r: r["entries"][1]["outputs"][0].update(components=5))
        refused("interpolated outputs",
                change=lambda r: r["entries"][1]["outputs"][0].update(interpolation="flat"))
        refused("fragment workgroup storage",
                change=lambda r: r["entries"][1].update(workgroup_storage_bytes=4))
        refused("a repeated entry name",
                change=lambda r: r["entries"][1].update(name="vs"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
