#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# tools/mrhi_container.py against the library: a container it writes
# is read by test_shader with the same digest and reflection, as are one
# whose entries use heaps and so has no WGSL, and ones with Metal code,
# and D3D12 code, whose maps follow the writer's rules; code that
# disagrees with its reflection is refused.
#
# usage: test_container_writer.py TEST_SHADER

import copy
import hashlib
import json
import os
import shutil
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


# Each entry's MSL, at the indices the writer's rule gives: the root
# block at buffer 0, the uniform buffer at 1 and the storage buffer at
# 2, the textures at 0 and 1, the samplers at 0 and 1.
MSL = {
    "vs": "vertex float4 vs(constant Root& root [[buffer(0)]],\n"
          "                 constant Camera& camera [[buffer(1)]]) { return float4(0); }\n",
    "fs": "// fragment float4 hidden(texture2d<float> t [[texture(9)]]) {}\n"
          "fragment float4 fs(texture2d<float> albedo [[texture(0)]],\n"
          "                   sampler linear [[sampler(0)]], sampler shadow [[sampler(1)]])\n"
          "{ return float4(1); }\n",
    "cs": "kernel void cs(device uint* counts [[buffer(2)]],\n"
          "               texture2d<float, access::write> image [[texture(1)]],\n"
          "               constant uint* spvBufferSizeConstants [[buffer(25)]]) {}\n",
}
METAL_INDICES = bytes([1, 0, 0, 2, 1, 1])


# Each entry's DXIL resources, as (class, register, space), at the
# places the writer's D3D12 rule gives: a binding at its slot in its
# table's space, the root block, constants and vertex information at b0,
# b1 and b2 of space 5.
DXIL = {
    "vs": [("b", 0, 5), ("b", 0, 0), ("b", 2, 5), ("b", 1, 5)],
    "fs": [("t", 2, 0), ("s", 1, 0), ("s", 0, 2)],
    "cs": [("u", 0, 1), ("u", 1, 1)],
}
PSV_TYPES = {"invalid": 0, "s": 1, "b": 2, "t": 4, "u": 7}
DXIL_KINDS = {"fragment": 0, "vertex": 1, "compute": 5}


def dxbc(stage, resources, stride=24, parts=(b"PSV0", b"DXIL"), count=None):
    """A DXIL container of a stage declaring the resources, a class of
    "range" being eight shader resource views and one ending in "*" an
    unbounded array of its class, its PSV0 part claiming count of them:
    all the writer reads."""
    count = len(resources) if count is None else count
    psv = struct.pack("<I", 24) + bytes(24) + struct.pack("<I", count)
    if resources:
        psv += struct.pack("<I", stride)
        for kind, reg, space in resources:
            high = reg + 7 if kind == "range" else 0xFFFFFFFF if kind.endswith("*") else reg
            psv += struct.pack("<4I", PSV_TYPES.get(kind.rstrip("*"), 4), space, reg, high)
            psv += bytes(max(stride - 16, 0))
    blobs = {b"PSV0": psv, b"DXIL": struct.pack("<II", DXIL_KINDS[stage] << 16 | 0x60, 2),
             b"ODD!": b"\1"}
    head = 32 + 4 * len(parts)
    offsets = []
    body = b""
    for fourcc in parts:
        body += bytes(-len(body) % 4)
        offsets.append(head + len(body))
        body += fourcc + struct.pack("<I", len(blobs[fourcc])) + blobs[fourcc]
    size = head + len(body)
    return (b"DXBC" + bytes(20) + struct.pack("<II", size, len(parts)) +
            struct.pack(f"<{len(parts)}I", *offsets) + body)


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


def write(folder, code, text, reflection, options=()):
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
    result = subprocess.run([sys.executable, WRITER, *options, *paths], capture_output=True,
                            text=True)
    return result.returncode, result.stderr, paths[3]


def metal_options(folder, msl=None, metallib=None):
    """Writes MSL and a metallib as given and answers the writer's
    options for them."""
    options = []
    if msl is not None:
        directory = os.path.join(folder, "msl")
        shutil.rmtree(directory, ignore_errors=True)
        os.makedirs(directory)
        for name, text in msl.items():
            with open(os.path.join(directory, name + ".metal"), "wb") as f:
                f.write(text if isinstance(text, bytes) else text.encode("utf-8"))
        options += ["--msl", directory]
    if metallib is not None:
        path = os.path.join(folder, "a.metallib")
        with open(path, "wb") as f:
            f.write(metallib)
        options += ["--metallib", path]
    return options


def dxil_options(folder, dxil):
    """Writes each entry's DXIL as given and answers the writer's options
    for it."""
    directory = os.path.join(folder, "dxil")
    shutil.rmtree(directory, ignore_errors=True)
    os.makedirs(directory)
    for name, code in dxil.items():
        with open(os.path.join(directory, name + ".dxil"), "wb") as f:
            f.write(code)
    return ["--dxil", directory]


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

        # Metal: MSL, a metallib, both, and what the writer refuses.
        def read_back(options, what):
            status, errors, output = write(folder, code, WGSL, REFLECTION, options)
            check(status == 0, f"{what} is written: {errors}")
            if status != 0:
                return None
            with open(output, "rb") as f:
                data = f.read()
            digest = hashlib.sha256(data[48:]).hexdigest()
            args = [test_shader, output, digest, "3", str(len(REFLECTION["bindings"])), "16", "0"]
            result = subprocess.run(args, capture_output=True, text=True)
            check(result.returncode == 0, f"the library reads {what}: {result.stdout}")
            return data

        data = read_back(metal_options(folder, msl=MSL), "MSL")
        if data is not None:
            sizes = [len(MSL[n].encode("utf-8")) for n in ("vs", "fs", "cs")]
            records = (struct.pack("<IIB7x", 0, sizes[0], 255) +
                       struct.pack("<IIB7x", sizes[0], sizes[1], 255) +
                       struct.pack("<IIB7x", sizes[0] + sizes[1], sizes[2], 25))
            check(section(data, 11) == bytes(8) + records + METAL_INDICES,
                  "the map: the root block, each entry's MSL and sizes, each binding")
            check(section(data, 12) == "".join(MSL[n] for n in ("vs", "fs", "cs")).encode(),
                  "the MSL, in the entries' order")
            check(section(data, 13) is None, "no metallib")
        library = b"MTLB" + bytes(12)
        data = read_back(metal_options(folder, msl=MSL, metallib=library), "MSL and a metallib")
        check(data is None or section(data, 13) == library, "the metallib kept as it is")
        data = read_back(metal_options(folder, metallib=library), "a metallib alone")
        if data is not None:
            check(section(data, 12) is None and section(data, 11) ==
                  bytes(8) + struct.pack("<IIB7x", 0, 0, 255) * 3 + METAL_INDICES,
                  "a map without MSL ranges or sizes")
        rootless = copy.deepcopy(REFLECTION)
        rootless["root_block_bytes"] = 0
        lean = dict(MSL, vs=MSL["vs"].replace("constant Root& root [[buffer(0)]],", ""))
        status, errors, output = write(folder, code, WGSL, rootless,
                                       metal_options(folder, msl=lean))
        check(status == 0, f"MSL without a root block is written: {errors}")
        if status == 0:
            with open(output, "rb") as f:
                check(section(f.read(), 11)[:8] == b"\xff" + bytes(7), "no root block's index")

        def metal_refused(what, msl=MSL, metallib=None, reflection=REFLECTION, wgsl=WGSL,
                          module=code):
            status, _, output = write(folder, module, wgsl, reflection,
                                      metal_options(folder, msl, metallib))
            check(status == 1 and not os.path.exists(output), f"refused: {what}")

        metal_refused("an entry without MSL", msl={"vs": MSL["vs"], "fs": MSL["fs"]})
        metal_refused("MSL without its function", msl=dict(MSL, vs=MSL["vs"].replace("vs(", "vx(")))
        metal_refused("MSL with its function commented out",
                      msl=dict(MSL, vs="// " + MSL["vs"].replace("\n", " ")))
        metal_refused("MSL declaring another stage",
                      msl=dict(MSL, fs=MSL["fs"].replace("fragment float4 fs", "vertex float4 fs")))
        metal_refused("a buffer outside the map",
                      msl=dict(MSL, vs=MSL["vs"].replace("buffer(1)", "buffer(3)")))
        metal_refused("a texture outside the map",
                      msl=dict(MSL, fs=MSL["fs"].replace("texture(0)", "texture(2)")))
        metal_refused("a sampler outside the map",
                      msl=dict(MSL, fs=MSL["fs"].replace("sampler(1)", "sampler(2)")))
        metal_refused("the root block without one",
                      reflection=rootless)
        metal_refused("buffer sizes on a binding's buffer",
                      msl=dict(MSL, cs=MSL["cs"].replace("buffer(25)", "buffer(1)")))
        metal_refused("buffer sizes past the buffers",
                      msl=dict(MSL, cs=MSL["cs"].replace("buffer(25)", "buffer(31)")))
        metal_refused("buffer sizes twice",
                      msl=dict(MSL, cs=MSL["cs"] + MSL["cs"].replace("cs(", "other(")))
        metal_refused("MSL that is not UTF-8", msl=dict(MSL, vs=b"vertex \xc0 vs() {}"))
        metal_refused("MSL with NUL", msl=dict(MSL, vs=MSL["vs"] + "\0"))
        metal_refused("a metallib without its magic", msl=None, metallib=b"MTLX" + bytes(4))
        metal_refused("Metal code beside a heap", wgsl=None, reflection=heaped, module=heap_code)
        metal_refused("more samplers than Metal's",
                      reflection=dict(REFLECTION, bindings=REFLECTION["bindings"] + [
                          {"table": 3, "slot": i, "kind": "sampler", "stages": ["fragment"],
                           "sampler": "filtering"} for i in range(15)]))

        # D3D12: DXIL, its map, fixed constants, and what the writer
        # refuses.
        stages = {e["name"]: e["stage"] for e in REFLECTION["entries"]}
        blobs = {n: dxbc(stages[n], r) for n, r in DXIL.items()}
        data = read_back(dxil_options(folder, blobs), "DXIL")
        if data is not None:
            head = struct.pack("<6I", 0, 5, 1, 5, 2, 5) + bytes(8)
            records = b""
            at = 0
            for name in ("vs", "fs", "cs"):
                records += struct.pack("<III4x", at, len(blobs[name]), name == "vs")
                at += len(blobs[name])
            places = b"".join(struct.pack("<II", b["slot"], b["table"])
                              for b in REFLECTION["bindings"])
            check(section(data, 14) == head + records + places + bytes(4),
                  "the map: its buffers, each entry's DXIL, each binding, no constant fixed")
            check(section(data, 15) == blobs["vs"] + blobs["fs"] + blobs["cs"],
                  "the DXIL, in the entries' order")
        unread = dict(blobs, vs=dxbc("vertex", [("b", 0, 5), ("b", 0, 0)]))
        data = read_back(dxil_options(folder, unread), "DXIL reading no vertex information")
        if data is not None:
            check(section(data, 14)[16:24] == bytes(8) and section(data, 14)[40] == 0,
                  "no vertex information's place, and no reader")
        # Constant 0 sizes an array through another constant, constant 1
        # the workgroup, and constant 2 nothing.
        words = list(struct.unpack(f"<{len(code) // 4}I", code))
        words += instruction(71, 80, 1, 0) + instruction(71, 81, 1, 1)
        words += instruction(71, 82, 1, 2) + instruction(71, 83, 11, 25)
        words += instruction(52, 90, 84, 128, 80, 85)
        words += instruction(28, 86, 91, 84)
        words += instruction(51, 90, 83, 81, 87, 87)
        sized = struct.pack(f"<{len(words)}I", *words)
        status, errors, output = write(folder, sized, WGSL, REFLECTION,
                                       dxil_options(folder, blobs))
        check(status == 0, f"DXIL with fixed constants is written: {errors}")
        if status == 0:
            with open(output, "rb") as f:
                check(section(f.read(), 14)[-4:] == bytes([1, 1, 0, 0]),
                      "constants sizing something fixed, through others too")
        undefaulted = copy.deepcopy(REFLECTION)
        del undefaulted["constants"][1]["default"]
        status, _, _ = write(folder, sized, WGSL, undefaulted, dxil_options(folder, blobs))
        check(status == 1, "refused: a fixed constant without a default")

        odd = dict(blobs, vs=dxbc("vertex", DXIL["vs"], parts=(b"PSV0", b"DXIL", b"ODD!")))
        data = read_back(dxil_options(folder, odd), "DXIL of a length not a multiple of 4")
        if data is not None:
            check(struct.unpack_from("<I", section(data, 14), 48)[0] == len(odd["vs"]) + 3,
                  "the next DXIL at a multiple of 4")
        read_only = copy.deepcopy(REFLECTION)
        read_only["bindings"].append({"table": 1, "slot": 2, "kind": "read_only_storage_buffer",
                                      "stages": ["compute"]})
        status, errors, _ = write(folder, code, WGSL, read_only, dxil_options(
            folder, dict(blobs, cs=dxbc("compute", DXIL["cs"] + [("t", 2, 1)]))))
        check(status == 0, f"a read-only storage buffer as a shader resource view: {errors}")
        status, errors, _ = write(folder, code, WGSL, REFLECTION, dxil_options(
            folder, dict(blobs, fs=dxbc("fragment", [("t", 2, 0)], stride=0))))
        check(status == 1 and errors.startswith("mrhi_container: "),
              "refused: one resource record at stride 0")

        def d3d12_refused(what, change, reflection=REFLECTION, wgsl=WGSL, module=code):
            status, errors, output = write(folder, module, wgsl, reflection,
                                           dxil_options(folder, dict(blobs, **change)))
            check(status == 1 and errors.startswith("mrhi_container: ") and
                  not os.path.exists(output), f"refused: {what}")

        status, errors, _ = write(folder, code, WGSL, REFLECTION, dxil_options(
            folder, {"vs": blobs["vs"], "cs": blobs["cs"]}))
        check(status == 1 and errors.startswith("mrhi_container: "),
              "refused: an entry without DXIL")
        d3d12_refused("DXIL of another stage", {"fs": dxbc("vertex", DXIL["fs"])})
        d3d12_refused("a resource outside the map",
                      {"cs": dxbc("compute", DXIL["cs"] + [("u", 5, 1)])})
        d3d12_refused("a register of another class",
                      {"cs": dxbc("compute", [("t", 0, 1)])})
        d3d12_refused("vertex information outside a vertex entry",
                      {"fs": dxbc("fragment", DXIL["fs"] + [("b", 2, 5)])})
        d3d12_refused("the root block without one", {}, reflection=rootless)
        d3d12_refused("constants without any", {}, reflection=dict(REFLECTION, constants=[]))
        d3d12_refused("an unknown resource type",
                      {"fs": dxbc("fragment", [("invalid", 0, 0)])})
        d3d12_refused("a range of registers",
                      {"fs": dxbc("fragment", [("range", 2, 0)])})
        d3d12_refused("resource records too short",
                      {"fs": dxbc("fragment", DXIL["fs"], stride=12)})
        d3d12_refused("no PSV0 part", {"fs": dxbc("fragment", DXIL["fs"], parts=(b"DXIL",))})
        d3d12_refused("no DXIL part", {"fs": dxbc("fragment", DXIL["fs"], parts=(b"PSV0",))})
        d3d12_refused("DXIL without its magic", {"fs": b"DXBX" + blobs["fs"][4:]})
        d3d12_refused("DXIL whose size is not its length", {"fs": blobs["fs"] + bytes(4)})
        d3d12_refused("a PSV0 part short of its resources",
                      {"fs": dxbc("fragment", DXIL["fs"], count=40)})
        # Heaps: fs samples textures and samplers, cs writes buffers.
        heap_dxil = dict(blobs, fs=dxbc("fragment", DXIL["fs"] + [("t*", 0, 16), ("s*", 0, 17)]),
                         cs=dxbc("compute", DXIL["cs"] + [("u*", 0, 18)]))
        status, errors, output = write(folder, heap_code, None, heaped,
                                       dxil_options(folder, heap_dxil))
        check(status == 0, f"DXIL reading heaps is written: {errors}")
        if status == 0:
            with open(output, "rb") as f:
                heap_map = section(f.read(), 14)
            ranges = b"".join(struct.pack("<III4x", c, 0, s) for c, s in ((0, 16), (1, 18), (2, 17)))
            check(heap_map[24:28] == struct.pack("<I", 3) and heap_map[-16 * 3 - 4:-4] == ranges,
                  "the heap ranges by class, before the fixed flags")

        def heap_d3d12_refused(what, **change):
            d3d12_refused(what, dict(heap_dxil, **change), wgsl=None, reflection=heaped,
                          module=heap_code)

        heap_d3d12_refused("a heap read by an entry reading none",
                           vs=dxbc("vertex", DXIL["vs"] + [("t*", 0, 16)]))
        heap_d3d12_refused("samplers from the heap in an entry reading none",
                           cs=dxbc("compute", DXIL["cs"] + [("u*", 0, 18), ("s*", 0, 17)]))
        heap_d3d12_refused("heap samplers used but never read",
                           fs=dxbc("fragment", DXIL["fs"] + [("t*", 0, 16)]))
        heap_d3d12_refused("a heap in a reserved space",
                           cs=dxbc("compute", DXIL["cs"] + [("u*", 0, 0xFFFFFFF0)]))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
