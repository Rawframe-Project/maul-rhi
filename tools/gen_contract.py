#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes the public headers and the generated sources from the contract,
# contract/mrhi.json, which is their source of truth
# (docs/internals/contract.md describes it). The contract's names are
# snake_case; this generator turns them into the family's C names: get_version
# becomes mrhiGetVersion, pass_begin on the object pass mrhiPass_Begin,
# error_invalid mrhi_errorInvalid, view_count viewCount. Headers go through
# the pinned clang-format, so the format, documentation and source checks
# apply to them as to hand-written code.
#
# The library version is not in the contract: it lives between
# release-please's markers in base.h, which release-please moves, and the
# generator keeps it as it finds it.
#
# usage: gen_contract.py [--check] [--mappings OUTPUT.md] | --self-test
#   --check     writes nothing; fails naming each file that differs from
#               what the contract generates.
#   --self-test plants each case of the limits' diagnostic codes, and of a
#               function's object, in a copy of the contract and proves the
#               check takes the right ones.
#   --mappings  also writes the table of how each concept maps onto
#               Vulkan, D3D12, Metal and WebGPU, for the documentation
#               site; it is not committed.

import copy
import json
import os
import re
import shutil
import subprocess
import sys
import textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTRACT = os.path.join("contract", "mrhi.json")
WIDTH = 80
NAME = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")
# The members of mrhiLimits that need no diagnostic code of their own,
# each with why: every other member has diagnostic_limit_<member>, which
# a call refused past it records (mrhiLimitDiagnostic).
LIMITS_WITHOUT_CODE = {
    "color_attachments": "the floor's 8 equals MRHI_MAX_COLOR_TARGETS, a fixed bound refused "
                         "first, so no call is refused at a device's value",
    "frames_in_flight": "a frame begun past it is a frame budget's refusal, recorded under "
                        "diagnostic_frames_in_flight",
}
KINDS = ("constant", "result", "enum", "bitflags", "opaque", "id", "struct", "function")
# The kinds a type reference may name, as kind.name.
REFERABLE = ("enum", "bitflags", "opaque", "id", "struct")
POINTERS = (None, "const", "mutable", "out")
PRIMITIVES = {
    "bool": "bool",
    "int8": "int8_t",
    "int16": "int16_t",
    "int32": "int32_t",
    "int64": "int64_t",
    "uint8": "uint8_t",
    "uint16": "uint16_t",
    "uint32": "uint32_t",
    "uint64": "uint64_t",
    "size": "size_t",
    "float32": "float",
    "float64": "double",
    "static_cstring": "const char*",
    "char": "char",
}
# The scalar classes of render targets and vertex formats, as the
# library's scalar types.
SCALARS = {"float": "mrhi_scalarFloat32", "sint": "mrhi_scalarSint32", "uint": "mrhi_scalarUint32"}
# The four APIs every concept maps onto (the requirements' four-API
# rule), and the classes a mapping row may have.
APIS = (("vulkan", "Vulkan"), ("d3d12", "D3D12"), ("metal", "Metal"), ("web_gpu", "WebGPU"))
CLASSES = ("direct", "emulated", "restricted", "absent_rejected")
# The openings of a thread safety paragraph, by class, as
# tools/check_docs.py accepts them.
THREAD_SAFETY = {
    "any": "Safe from any thread.",
    "any_exclusive": "Safe from any thread; {object} {verb} used by one thread at a time.",
    "main": "Main thread only.",
    "realtime": "Real-time safe: no allocation, lock or wait.",
}


def opening_of(safety):
    """The opening sentence of a thread safety paragraph."""
    verb = "are" if safety.get("plural") else "is"
    return THREAD_SAFETY[safety["class"]].format(object=safety.get("object", ""), verb=verb)


def pascal(name):
    """A snake_case name in PascalCase."""
    return "".join(word.capitalize() for word in name.split("_"))


def camel(name):
    """A snake_case name in camelCase."""
    words = name.split("_")
    return words[0] + "".join(word.capitalize() for word in words[1:])


class Names:
    """The C names of a contract's declarations."""

    def __init__(self, contract):
        self.prefix = contract["prefix"]
        self.macro = contract["macro"]

    def type(self, name):
        return self.prefix + pascal(name)

    def function(self, name, on=None):
        if on is None:
            return self.prefix + pascal(name)
        return self.prefix + pascal(on) + "_" + pascal(name[len(on) + 1:])

    def call(self, item):
        """A function item's name, PObject_Verb when it names an object."""
        return self.function(item["name"], item.get("object"))

    def value(self, name):
        return self.prefix + "_" + camel(name)

    def constant(self, name):
        return self.macro + "_" + name.upper()


# Validation: every message names where the contract is wrong.


def check_name(errors, where, name):
    if not isinstance(name, str) or not NAME.match(name):
        errors.append(f"{where}: '{name}' is not a snake_case name")


def check_type(errors, where, kind_of, spec, markable=False):
    """Checks a type spec: a type and, for members and arguments, a pointer.
    Only a function's own argument is markable input_only; check_function
    checks the mark there."""
    type_name, pointer = spec.get("type"), spec.get("pointer")
    if pointer not in POINTERS:
        errors.append(f"{where}: unknown pointer '{pointer}'")
    if "input_only" in spec and not markable:
        errors.append(f"{where}: '{spec.get('name', 'the return value')}' is input_only, which "
                      "only a function's argument may be")
    if type_name == "function":
        check_signature(errors, where, kind_of, spec)
        return
    if type_name in PRIMITIVES or (type_name == "result" and "result" in kind_of.values()):
        return
    if type_name == "void" or kind_of.get(str(type_name).partition(".")[2]) == "opaque":
        if pointer is None:
            errors.append(f"{where}: '{type_name}' is only used through a pointer")
        if type_name == "void":
            return
    kind, _, name = str(type_name).partition(".")
    if kind not in REFERABLE or kind_of.get(name) != kind:
        errors.append(f"{where}: unknown type '{type_name}'")


def check_signature(errors, where, kind_of, spec, markable=False):
    """Checks a function's or a function pointer's return and arguments;
    markable for a function's, whose arguments may be input_only."""
    returns = spec.get("returns")
    if returns is not None:
        check_type(errors, where, kind_of, returns)
    for arg in spec.get("args", []):
        check_name(errors, where, arg.get("name"))
        check_type(errors, where, kind_of, arg, markable)


def check_values(errors, where, item):
    seen_names, seen_values = set(), set()
    for value in item.get("values", []):
        check_name(errors, where, value.get("name"))
        if value.get("name") in seen_names or value.get("value") in seen_values:
            errors.append(f"{where}: '{value.get('name')}' repeats a name or a value")
        seen_names.add(value.get("name"))
        seen_values.add(value.get("value"))
        if not isinstance(value.get("value"), int) or not value.get("doc"):
            errors.append(f"{where}: '{value.get('name')}' needs an integer value and a doc")
    if item["kind"] == "result" and 0 not in seen_values:
        errors.append(f"{where}: a result needs success as zero")
    # An enum is int32_t, a packed enum (values that carry bits, as a
    # chained struct's type does) and bitflags uint32_t, as the family's
    # rule sets, so none names a width.
    if item["kind"] in ("enum", "bitflags") and "width" in item:
        errors.append(f"{where}: takes no width: an enum is int32_t, bitflags uint32_t")
    if "packed" in item and (item["kind"] != "enum" or item["packed"] is not True):
        errors.append(f"{where}: only an enum is packed, with packed true")
    if item["kind"] == "bitflags":
        for value in item.get("values", []):
            bits = value.get("value")
            if not isinstance(bits, int) or bits <= 0 or bits & (bits - 1):
                errors.append(f"{where}: '{value.get('name')}' is not a single bit")
    if item.get("mapped"):
        for value in item.get("values", []):
            if not value.get("unmapped"):
                check_mapping(errors, f"{where} '{value.get('name')}'", value.get("mapping"))
    for value in item.get("values", []):
        if "floor" in value and not isinstance(value["floor"], dict):
            errors.append(f"{where}: '{value.get('name')}' has a floor that is not an object")
        if "floor" in value and not (isinstance(value.get("block"), list) and
                                     len(value["block"]) == 2):
            errors.append(f"{where}: '{value.get('name')}' needs a block of two sizes")
        if "floor" in value and not is_copy(value.get("copy")):
            errors.append(f"{where}: '{value.get('name')}' needs copy facts per aspect")
        if not set(value.get("aspects", [])) <= {"depth", "stencil"}:
            errors.append(f"{where}: '{value.get('name')}' has aspects other than depth and stencil")
        target = value.get("target")
        if target is not None and not (
                isinstance(target, dict) and set(target) == {"channels", "scalar", "bytes", "alignment"}
                and target["channels"] in (1, 2, 3, 4) and target["scalar"] in SCALARS
                and target["alignment"] in (1, 2, 4) and isinstance(target["bytes"], int)
                and target["bytes"] > 0):
            errors.append(f"{where}: '{value.get('name')}' has a malformed target")
        layout = value.get("layout")
        if layout is not None and not (
                isinstance(layout, dict) and set(layout) == {"scalar", "components", "bytes"}
                and layout["scalar"] in SCALARS and layout["components"] in (1, 2, 3, 4)
                and isinstance(layout["bytes"], int) and layout["bytes"] > 0):
            errors.append(f"{where}: '{value.get('name')}' has a malformed layout")
        pair = value.get("srgb_pair")
        if pair is not None and pair not in {other["name"] for other in item["values"]}:
            errors.append(f"{where}: '{value.get('name')}' pairs with an unknown format")


def is_copy(copy):
    """Whether a format's copy facts are well formed: per aspect (color,
    depth or stencil) the bytes of a block and the directions it copies."""
    return isinstance(copy, dict) and set(copy) <= {"color", "depth", "stencil"} and all(
        isinstance(facts, dict) and set(facts) == {"bytes", "source", "destination"}
        and isinstance(facts["bytes"], int) and 0 < facts["bytes"] <= 16
        and isinstance(facts["source"], bool) and isinstance(facts["destination"], bool)
        for facts in copy.values())


def check_constant(errors, where, item):
    if not isinstance(item.get("value"), int) or item["value"] < 0:
        errors.append(f"{where}: a constant needs a non-negative integer value")


def check_struct(errors, where, item, kind_of):
    if (item.get("def") or item.get("chained")) and kind_of.get("chain") != "struct":
        errors.append(f"{where}: a def or chained struct needs the struct 'chain'")
    if item.get("def") and item.get("chained"):
        errors.append(f"{where}: a struct is a def or chained, not both")
    for member in item.get("members", []):
        check_name(errors, where, member.get("name"))
        check_type(errors, where, kind_of, member)
        if "array" in member and kind_of.get(member["array"]) != "constant":
            errors.append(f"{where}: '{member['name']}' needs a constant for its array length")
        if item.get("mapped"):
            check_mapping(errors, f"{where} '{member.get('name')}'", member.get("mapping"))
        if item.get("defaults") and not isinstance(member.get("default"), (int, bool)):
            errors.append(f"{where}: '{member.get('name')}' needs a default value")
        if member.get("better", "higher") not in ("higher", "lower"):
            errors.append(f"{where}: '{member.get('name')}' is better higher or lower")


def check_mapping(errors, where, mapping):
    """A concept's four rows: each with a class and a note on how, and an
    emulated one with its cost."""
    if not isinstance(mapping, dict):
        errors.append(f"{where}: needs a mapping row for each API")
        return
    for api, _ in APIS:
        row = mapping.get(api)
        if not isinstance(row, dict) or row.get("class") not in CLASSES or not row.get("note"):
            errors.append(f"{where}: the {api} row needs a class ({', '.join(CLASSES)}) and a note")
        elif row["class"] == "emulated" and not row.get("cost"):
            errors.append(f"{where}: the {api} row is emulated and needs its cost")
    if set(mapping) - {api for api, _ in APIS}:
        errors.append(f"{where}: a mapping row for an unknown API")


# The kinds of concept that are mapped onto the APIs or declared the
# library's own.
CLASSED_KINDS = ("function", "struct", "enum", "bitflags")


def check_classed(errors, where, item):
    """A concept mapped by value or member, taken whole with one row per
    API, or declared the library's own with the reason: exactly one."""
    ways = [bool(item.get("mapped")), "mapping" in item, "library" in item]
    if sum(ways) > 1:
        errors.append(f"{where}: mapped, mapped whole or the library's own, only one")
    if "library" in item and not (isinstance(item["library"], str) and item["library"]):
        errors.append(f"{where}: the library's own needs the reason")
    if "mapping" in item:
        check_mapping(errors, where, item["mapping"])
    if not any(ways):
        errors.append(f"{where}: needs mapping rows or the library's own reason")


# The verbs of a call that makes or ends an object, which the family's
# rule names PVerbNoun whatever its first id.
CREATION_VERBS = ("create", "destroy", "declare", "import", "add", "acquire")


def object_of(item):
    """The object a function acts on by the family's rule: the kind of its
    first id that is neither an output nor marked input_only, else its
    root's type; None for a call that takes no root, or makes or ends an
    object."""
    args = item.get("args", [])
    if (not args or not args[0].get("type", "").startswith("opaque.")
            or item["name"].partition("_")[0] in CREATION_VERBS):
        return None
    for arg in args:
        kind, _, name = arg.get("type", "").partition(".")
        if kind == "id" and arg.get("pointer") is None and not arg.get("input_only"):
            return name.removesuffix("_id")
    return args[0]["type"].partition(".")[2]


def check_function(errors, where, item, kind_of):
    check_signature(errors, where, kind_of, item, markable=True)
    if item.get("returns") is not None and not item["returns"].get("doc"):
        errors.append(f"{where}: the return value needs a doc")
    for arg in item.get("args", []):
        if not arg.get("doc"):
            errors.append(f"{where}: argument '{arg.get('name')}' needs a doc")
        by_value_id = arg.get("type", "").startswith("id.") and arg.get("pointer") is None
        if "input_only" in arg and (arg["input_only"] is not True or not by_value_id):
            errors.append(f"{where}: argument '{arg.get('name')}' is input_only, which is true "
                          "on an id taken by value or absent")
    body = item.get("inline_body")
    if body is not None and (not isinstance(body, list) or not body or
                             not all(isinstance(line, str) for line in body)):
        errors.append(f"{where}: an inline body is a list of C lines")
    safety = item.get("thread_safety", {})
    if safety.get("class") not in THREAD_SAFETY:
        errors.append(f"{where}: unknown thread safety class '{safety.get('class')}'")
    elif safety["class"] == "any_exclusive" and not safety.get("object"):
        errors.append(f"{where}: an exclusive thread safety class names its object")
    on, wanted = item.get("object"), object_of(item)
    if on != wanted:
        errors.append(f"{where}: its object '{on}' is not '{wanted}', the kind of its first id "
                      "or else its root")
    elif on is not None and not item["name"].startswith(on + "_"):
        errors.append(f"{where}: its object '{on}' does not start its name")


def check_limit_codes(errors, items):
    """Each limit has its diagnostic code, or a reason not to in
    LIMITS_WITHOUT_CODE, and each code names a limit."""
    by_name = {item.get("name"): item for item in items}
    limits, codes = by_name.get("limits"), by_name.get("diagnostic_code")
    if limits is None or codes is None:
        return
    members = {member.get("name") for member in limits.get("members", [])}
    prefix = "diagnostic_limit_"
    coded = {value["name"][len(prefix):] for value in codes.get("values", [])
             if value.get("name", "").startswith(prefix)}
    excused = set(LIMITS_WITHOUT_CODE)
    for name in sorted(members - coded - excused):
        errors.append(f"struct 'limits': '{name}' has no {prefix}{name} code, nor a reason "
                      "in LIMITS_WITHOUT_CODE")
    for name in sorted(coded - members):
        errors.append(f"enum 'diagnostic_code': '{prefix}{name}' names no member of limits")
    for name in sorted(coded & excused):
        errors.append(f"struct 'limits': '{name}' has a code and a reason in LIMITS_WITHOUT_CODE")
    for name in sorted(excused - members):
        errors.append(f"LIMITS_WITHOUT_CODE gives a reason for '{name}', which limits lacks")


def validate(contract):
    """The contract's structural errors, as messages; empty when valid."""
    errors, kind_of = [], {}
    for key in ("library", "title", "prefix", "macro", "guard", "contract_version", "headers"):
        if key not in contract:
            errors.append(f"the contract has no '{key}'")
    if errors:
        return errors
    items = [item for header in contract["headers"] for item in header["items"]]
    for item in items:
        where = f"{item.get('kind')} '{item.get('name')}'"
        check_name(errors, where, item.get("name"))
        if item.get("kind") not in KINDS or not item.get("doc"):
            errors.append(f"{where}: needs a known kind and a doc")
        if item.get("name") in kind_of:
            errors.append(f"{where}: the name is declared twice")
        kind_of[item.get("name")] = item.get("kind")
    checks = {"result": check_values, "enum": check_values, "bitflags": check_values,
              "constant": check_constant}
    for item in items:
        if item["kind"] in CLASSED_KINDS:
            check_classed(errors, f"{item['kind']} '{item['name']}'", item)
    for item in items:
        where = f"{item['kind']} '{item['name']}'"
        if item["kind"] in checks:
            checks[item["kind"]](errors, where, item)
        elif item["kind"] == "struct":
            check_struct(errors, where, item, kind_of)
        elif item["kind"] == "function":
            check_function(errors, where, item, kind_of)
    check_limit_codes(errors, items)
    return errors


# Emission: C text in the family's style.


def comment(text, indent, marker):
    """Text wrapped into comment lines of the given marker."""
    lead = " " * indent + marker + " "
    return [lead + line for line in textwrap.wrap(text, WIDTH - len(lead))]


def c_type(names, spec, own=None):
    """The C type of a type spec; own is the struct being declared, which
    refers to itself by its struct tag."""
    type_name = spec["type"]
    if type_name in PRIMITIVES:
        base = PRIMITIVES[type_name]
    elif type_name in ("void", "result"):
        base = names.type("result") if type_name == "result" else "void"
    else:
        name = type_name.partition(".")[2]
        base = ("struct " if name == own else "") + names.type(name)
    pointer = spec.get("pointer")
    if pointer == "const":
        return "const " + base + "*"
    return base + {"mutable": "*", "out": "**"}.get(pointer, "")


def c_signature(names, spec):
    """A parameter list in C."""
    args = spec.get("args", [])
    return ", ".join(c_declaration(names, arg) for arg in args) or "void"


def c_declaration(names, spec, own=None):
    """A member or parameter: its type and name, or a function pointer."""
    name = camel(spec["name"])
    if spec["type"] == "function":
        returns = c_type(names, spec["returns"]) if spec.get("returns") else "void"
        return f"{returns} (*{name})({c_signature(names, spec)})"
    if "array" in spec:
        return f"{c_type(names, spec, own)} {name}[{names.constant(spec['array'])}]"
    return f"{c_type(names, spec, own)} {name}"


def library_version(contract):
    """The (major, minor, patch) base.h carries between release-please's markers."""
    path = os.path.join(ROOT, "include", contract["library"], "base.h")
    text = open(path, encoding="utf-8").read() if os.path.exists(path) else ""
    parts = []
    for part in ("MAJOR", "MINOR", "PATCH"):
        match = re.search(r"^#define " + contract["macro"] + "_VERSION_" + part + r" (\d+)$", text,
                          re.M)
        if match is None:
            raise SystemExit(f"{path}: no {contract['macro']}_VERSION_{part}")
        parts.append(int(match.group(1)))
    return parts


def preamble_version(contract, names):
    macro = names.macro
    lines = ["// The library version. CMake reads it from here, and release-please",
             "// moves it between the markers."]
    for part, value in zip(("major", "minor", "patch"), library_version(contract)):
        lines += [f"// x-release-please-start-{part}",
                  f"#define {macro}_VERSION_{part.upper()} {value}",
                  "// x-release-please-end"]
    lines += [""]
    lines += comment("The binary interface the headers describe, which the root def carries: the "
                     "whole version before 1.0, where any release may break it, and the major and "
                     "minor version from 1.0 on. The top bit is always set, so a zeroed value, "
                     "or a small one written by hand, never matches.", 0, "//")
    lines += [
        f"#define {macro}_ABI_VERSION                                                           \\",
        f"    (UINT32_C(0x80000000) | ((uint32_t){macro}_VERSION_MAJOR << 16) |                 \\",
        f"     ({macro}_VERSION_MAJOR == 0                                                      \\",
        f"          ? (((uint32_t){macro}_VERSION_MINOR << 8) | (uint32_t){macro}_VERSION_PATCH)    \\",
        f"          : (uint32_t){macro}_VERSION_MINOR))",
        f"#if {macro}_VERSION_MAJOR > 0x7FFF || {macro}_VERSION_MINOR > 0xFFFF",
        f'#error "{macro}_VERSION_MAJOR or {macro}_VERSION_MINOR does not fit {macro}_ABI_VERSION"',
        "#endif",
        f"#if {macro}_VERSION_MAJOR == 0 && ({macro}_VERSION_MINOR > 0xFF || {macro}_VERSION_PATCH > 0xFF)",
        f'#error "before 1.0, {macro}_VERSION_MINOR and {macro}_VERSION_PATCH must fit 8 bits each"',
        "#endif",
    ]
    return lines


def preamble_api(contract, names):
    macro, guard, target = names.macro, contract["guard"], contract["library"].replace("-", "_")
    return [
        f"// {macro}_API marks the public functions: dllexport or dllimport in a",
        f"// shared Windows build ({target}_EXPORTS is defined while building",
        "// the library), default visibility in a shared build elsewhere.",
        f"#if defined({guard}_SHARED) && defined(_WIN32)",
        f"#if defined({target}_EXPORTS)",
        f"#define {macro}_API __declspec(dllexport) extern",
        "#else",
        f"#define {macro}_API __declspec(dllimport) extern",
        "#endif",
        f"#elif defined({guard}_SHARED) && (defined(__GNUC__) || defined(__clang__))",
        f'#define {macro}_API __attribute__((visibility("default"))) extern',
        "#else",
        f"#define {macro}_API extern",
        "#endif",
    ]


def preamble_nodiscard(contract, names):
    macro = names.macro
    return [
        f"// {macro}_NODISCARD marks a function whose result must be read: every",
        "// function that returns a status. The attribute is standard in C23 and",
        "// C++17 and left out for older dialects. MSVC keeps __cplusplus at",
        "// 199711L unless /Zc:__cplusplus is given, so _MSVC_LANG is read too.",
        "#if (defined(__cplusplus) && __cplusplus >= 201703L) || \\",
        "    (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)",
        f"#define {macro}_NODISCARD [[nodiscard]]",
        "#elif !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L",
        f"#define {macro}_NODISCARD [[nodiscard]]",
        "#else",
        f"#define {macro}_NODISCARD",
        "#endif",
    ]


PREAMBLES = {
    "version": preamble_version,
    "api": preamble_api,
    "nodiscard": preamble_nodiscard,
}


def emit_values(names, item):
    width = "uint32_t" if item["kind"] == "bitflags" or item.get("packed") else "int32_t"
    lines = comment(item["doc"], 4, "//")
    lines.append(f"    typedef {width} {names.type(item['name'])};")
    lines += ["", "    enum", "    {"]
    for value in item["values"]:
        lines += comment(value["doc"], 8, "//")
        bits = item["kind"] == "bitflags"
        lines.append(f"        {names.value(value['name'])} = {c_value(value['value'], bits)},")
    lines.append("    };")
    return lines


def c_value(value, bits=False):
    """An enum value; bits and large values in hexadecimal."""
    if bits:
        return f"0x{value:X}u"
    return f"0x{value:08X}u" if value > 0xFFFF else str(value)


def emit_constant(names, item):
    return comment(item["doc"], 0, "//") + [f"#define {names.constant(item['name'])} "
                                            f"{c_value(item['value'])}"]


def emit_id(names, item):
    c_name = names.type(item["name"])
    lines = comment(item["doc"], 4, "//")
    lines += [f"    typedef struct {c_name}", "    {", "        uint32_t index1;",
              "        uint32_t generation;", f"    }} {c_name};"]
    return lines


def emit_opaque(names, item):
    c_name = names.type(item["name"])
    return comment(item["doc"], 4, "//") + [f"    typedef struct {c_name} {c_name};"]


def emit_struct(names, item):
    c_name = names.type(item["name"])
    chain = names.type("chain")
    lines = comment(item["doc"], 4, "//")
    lines += [f"    typedef struct {c_name}", "    {"]
    if item.get("def"):
        lines += ["        uint32_t cookie;", "        // Extensions, or NULL.",
                  f"        const {chain}* next;"]
    if item.get("chained"):
        lines.append(f"        {chain} chain;")
    for member in item["members"]:
        if member.get("doc"):
            lines += comment(member["doc"], 8, "//")
        lines.append(f"        {c_declaration(names, member, item['name'])};")
    lines.append(f"    }} {c_name};")
    return lines


def emit_function(names, item):
    lines = comment(item["doc"], 4, "///")
    args = item.get("args", [])
    if args or item.get("returns"):
        lines.append("    ///")
    width = max((len(camel(arg["name"])) for arg in args), default=0)
    for arg in args:
        lead = f"@param {camel(arg['name']).ljust(width)}  "
        wrapped = textwrap.wrap(arg["doc"], WIDTH - 8 - len(lead))
        lines.append("    /// " + lead + wrapped[0])
        lines += ["    /// " + " " * len(lead) + line for line in wrapped[1:]]
    if item.get("returns"):
        lines += comment("@return " + item["returns"]["doc"], 4, "///")
    safety = item["thread_safety"]
    opening = opening_of(safety)
    lines.append("    /// @par Thread safety")
    lines += comment(" ".join([opening] + ([safety["note"]] if safety.get("note") else [])), 4,
                     "///")
    returns = item.get("returns")
    result = c_type(names, returns) if returns else "void"
    if item.get("inline_body"):
        # Built by the program from its own headers, so that a default
        # stamps the version the program was built with.
        return lines + [f"    static inline {result} "
                        f"{names.call(item)}({c_signature(names, item)})", "    {"] + [
            "        " + line for line in item["inline_body"]] + ["    }"]
    nodiscard = f"{names.macro}_NODISCARD " if returns and returns["type"] == "result" else ""
    return lines + [f"    {nodiscard}{names.macro}_API {result} "
                    f"{names.call(item)}({c_signature(names, item)});"]


EMITTERS = {
    "bitflags": emit_values,
    "constant": emit_constant,
    "id": emit_id,
    "result": emit_values,
    "enum": emit_values,
    "opaque": emit_opaque,
    "struct": emit_struct,
    "function": emit_function,
}


def emit_header(contract, header):
    names = Names(contract)
    guard = f"{contract['guard']}_{header['name'].upper()}_H"
    lines = ["// SPDX-License-Identifier: MIT", "// Copyright (c) 2026 Sirac Ozmen", "//"]
    lines += comment(header["doc"], 0, "//")
    lines += ["//"]
    lines += comment(f"Generated by tools/gen_contract.py from {CONTRACT}: edit the contract, "
                     "not this file.", 0, "//")
    lines += ["", f"#ifndef {guard}", f"#define {guard}", ""]
    includes = [f'#include "{contract["library"]}/{name}.h"' for name in header.get("includes", [])]
    lines += includes + ([""] if includes else [])
    lines += ["#include <stdbool.h>", "#include <stddef.h>", "#include <stdint.h>", ""]
    lines += ["#ifdef __cplusplus", 'extern "C"', "{", "#endif", ""]
    for preamble in header.get("preamble", []):
        lines += PREAMBLES[preamble](contract, names) + [""]
    for item in header["items"]:
        lines += EMITTERS[item["kind"]](names, item) + [""]
    lines += ["#ifdef __cplusplus", "}", "#endif", "", f"#endif // {guard}"]
    return "\n".join(lines) + "\n"


def emit_mappings(contract):
    """The mapping appendix: the four rows of every mapped concept."""
    lines = [f"# {contract['title']} mappings", ""]
    lines += textwrap.wrap(f"Generated by `tools/gen_contract.py` from `{CONTRACT}`: edit the "
                           "contract, not this file. Each row is classed direct, emulated, "
                           "restricted or absent-rejected, with how.", 72)
    names = Names(contract)
    for header in contract["headers"]:
        for item in header["items"]:
            if not item.get("mapped"):
                continue
            is_enum = item["kind"] in ("enum", "bitflags")
            lines += ["", f"## {names.type(item['name'])}", ""]
            lines.append(f"| {'Value' if is_enum else 'Member'} | "
                         + " | ".join(title for _, title in APIS) + " |")
            lines.append("| --- " * (len(APIS) + 1) + "|")
            for entry in item["values"] if is_enum else item["members"]:
                if entry.get("unmapped"):
                    continue
                cells = [cell(entry["mapping"][api]) for api, _ in APIS]
                label = names.value(entry["name"]) if is_enum else camel(entry["name"])
                lines.append(f"| `{label}` | " + " | ".join(cells) + " |")
    for header in contract["headers"]:
        whole = [item for item in header["items"] if "mapping" in item]
        own = [item for item in header["items"] if "library" in item]
        if whole:
            lines += ["", f"## {header['name']}: operations and structures", ""]
            lines.append("| Concept | " + " | ".join(title for _, title in APIS) + " |")
            lines.append("| --- " * (len(APIS) + 1) + "|")
            for item in whole:
                label = names.call(item) if item["kind"] == "function" \
                    else names.type(item["name"])
                cells = [cell(item["mapping"][api]) for api, _ in APIS]
                lines.append(f"| `{label}` | " + " | ".join(cells) + " |")
        if own:
            lines += ["", f"## {header['name']}: the library's own", ""]
            lines += ["| Concept | Why no API maps it |", "| --- | --- |"]
            for item in own:
                label = names.call(item) if item["kind"] == "function" \
                    else names.type(item["name"])
                lines.append(f"| `{label}` | {item['library']} |")
    return "\n".join(lines) + "\n"


def cell(row):
    """A mapping row as an appendix cell: its class, how, and an emulated
    row's cost."""
    text = f"{row['class'].replace('_', '-')}: {row['note']}"
    return text + (f" (cost: {row['cost']})" if row.get("cost") else "")


def emit_defaults(contract):
    """The Default functions of structs whose members carry defaults."""
    names = Names(contract)
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Copyright (c) 2026 Sirac Ozmen",
        "//",
        f"// Generated by tools/gen_contract.py from {CONTRACT}: the default",
        "// values of the structs whose members the contract gives defaults.",
        "",
        "// clang-format off",
        "",
    ]
    includes, bodies = set(), []
    for header in contract["headers"]:
        for item in header["items"]:
            if not item.get("defaults"):
                continue
            includes.add(f'#include "{contract["library"]}/{header["name"]}.h"')
            c_name = names.type(item["name"])
            bodies += ["", f"{c_name} {names.function('default_' + item['name'])}(void)", "{",
                       f"    return ({c_name}){{"]
            for member in item["members"]:
                value = member["default"]
                text = ("true" if value else "false") if isinstance(value, bool) else str(value)
                bodies.append(f"        .{camel(member['name'])} = {text},")
            bodies += ["    };", "}"]
    return "\n".join(lines + sorted(includes) + bodies) + "\n"


def out_structs(contract):
    """The structs a public function fills through a mutable pointer, and
    the structs they hold, each after those it holds."""
    items = {item["name"]: item for header in contract["headers"] for item in header["items"]}
    order = []

    def visit(name):
        if name in order:
            return
        for member in items[name]["members"]:
            if member["type"].startswith("struct."):
                visit(member["type"].partition(".")[2])
        order.append(name)

    for item in items.values():
        for arg in item.get("args", []) if item["kind"] == "function" else []:
            if arg["type"].startswith("struct.") and arg.get("pointer") == "mutable":
                visit(arg["type"].partition(".")[2])
    return order


def emit_out_copies(contract):
    """The functions the core fills a caller's struct with: the source read
    whole first, so an output that overlaps it still gets its value, then
    the output zeroed and filled member by member, so its padding reads 0
    whatever the source or the caller's storage held."""
    names = Names(contract)
    items = {item["name"]: (header["name"], item)
             for header in contract["headers"] for item in header["items"]}
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Copyright (c) 2026 Sirac Ozmen",
        "//",
        f"// Generated by tools/gen_contract.py from {CONTRACT}: the copies the",
        "// core fills a caller's struct with. Each reads the source whole first,",
        "// so an output that overlaps it still gets its value, then zeroes the",
        "// output and fills it member by member, so that its padding reads 0",
        "// whatever the source or the caller's storage held.",
        "",
        "// clang-format off",
        "",
        "#ifndef MAUL_RHI_SRC_GENERATED_OUT_COPIES_H",
        "#define MAUL_RHI_SRC_GENERATED_OUT_COPIES_H",
        "",
    ]
    structs = out_structs(contract)
    includes = sorted({f'#include "{contract["library"]}/{items[name][0]}.h"' for name in structs})
    lines += includes + ["", "#include <string.h>"]
    for name in structs:
        item = items[name][1]
        c_name = names.type(name)
        lines += ["", f"static inline void {names.function('out_' + name)}(",
                  f"    {c_name}* out, const {c_name}* from)", "{",
                  f"    const {c_name} value = *from;", "    memset(out, 0, sizeof(*out));"]
        for member in item["members"]:
            field = camel(member["name"])
            nested = member["type"].startswith("struct.")
            copy = names.function("out_" + member["type"].partition(".")[2]) if nested else None
            if "array" in member:
                at = "[i]"
                body = (f"{copy}(&out->{field}{at}, &value.{field}{at});" if nested
                        else f"out->{field}{at} = value.{field}{at};")
                lines += [f"    for (size_t i = 0; i < {names.constant(member['array'])}; ++i)",
                          "    {", f"        {body}", "    }"]
            elif nested:
                lines.append(f"    {copy}(&out->{field}, &value.{field});")
            else:
                lines.append(f"    out->{field} = value.{field};")
        lines.append("}")
    lines += ["", "#endif"]
    return "\n".join(lines) + "\n"


def find_item(contract, name):
    return next(item for header in contract["headers"] for item in header["items"]
                if item["name"] == name)


def emit_capability_checks(contract):
    """The core's checks of features and limits: a request within a
    grant, and the features a driver's API cannot grant (its
    absent-rejected rows) masked off."""
    names = Names(contract)
    features, limits = find_item(contract, "features"), find_item(contract, "limits")
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Copyright (c) 2026 Sirac Ozmen",
        "//",
        f"// Generated by tools/gen_contract.py from {CONTRACT}: requests",
        "// checked against grants, and the features each API cannot grant.",
        "",
        "// clang-format off",
        "",
        '#include "capabilities_core.h"',
        "",
        "#include <stddef.h>",
        "",
        "bool mrhiFeaturesWithin(const mrhiFeatures* asked, const mrhiFeatures* granted)",
        "{",
        "    return true",
    ]
    for member in features["members"]:
        name = camel(member["name"])
        lines.append(f"        && (!asked->{name} || granted->{name})")
    lines[-1] += ";"
    lines += ["}", "", "bool mrhiLimitsWithin(const mrhiLimits* asked, const mrhiLimits* granted)",
              "{", "    return true"]
    for member in limits["members"]:
        name = camel(member["name"])
        sign = ">=" if member.get("better") == "lower" else "<="
        lines.append(f"        && asked->{name} {sign} granted->{name}")
    lines[-1] += ";"
    lines += ["}", "", "bool mrhiAlignmentsValid(const mrhiLimits* limits)", "{", "    return true"]
    for member in limits["members"]:
        if member.get("better") == "lower":
            name = camel(member["name"])
            lines.append(f"        && limits->{name} != 0"
                         f" && (limits->{name} & (limits->{name} - 1)) == 0")
    lines[-1] += ";"
    lines += ["}", "", "mrhiDiagnosticCode mrhiLimitDiagnostic(size_t field)", "{",
              "    switch (field)", "    {"]
    codes = {value["name"] for value in find_item(contract, "diagnostic_code")["values"]}
    for member in limits["members"]:
        code = "diagnostic_limit_" + member["name"]
        if code in codes:
            lines += [f"    case offsetof(mrhiLimits, {camel(member['name'])}):",
                      f"        return {names.value(code)};"]
    lines += ["    default:", "        return 0;", "    }"]
    lines += ["}", "", "void mrhiMaskFeatures(mrhiFeatures* features, mrhiDriverKind driver)", "{",
              "    switch (driver)", "    {"]
    for api, _ in APIS:
        lines.append(f"    case {names.value('driver_' + api)}:")
        for member in features["members"]:
            if member["mapping"][api]["class"] == "absent_rejected":
                lines.append(f"        features->{camel(member['name'])} = false;")
        lines.append("        break;")
    lines += ["    default:", "        break;", "    }", "}"]
    return "\n".join(lines + format_checks(contract) + known_bits(contract)) + "\n"


def known_bits(contract):
    """For every bitflags item, the mask of the bits the contract lists."""
    names = Names(contract)
    lines = []
    for header in contract["headers"]:
        for item in header["items"]:
            if item["kind"] == "bitflags":
                mask = 0
                for value in item["values"]:
                    mask |= value["value"]
                c_name = names.type(item["name"])
                lines += ["", f"const {c_name} {c_name}Known = 0x{mask:X}u;"]
    return lines


def format_checks(contract):
    """The format functions: the floor capabilities of each format, the
    feature its family needs, whether a value is listed, and a
    capability set within another."""
    names = Names(contract)
    formats, caps = find_item(contract, "format"), find_item(contract, "format_caps")
    listed = [value for value in formats["values"] if "floor" in value]
    lines = ["", f"const mrhiFormat mrhiKnownFormats[{len(listed)}] = {{"]
    lines += [f"    {names.value(value['name'])}," for value in listed]
    lines += ["};", "", "bool mrhiIsFormatKnown(mrhiFormat format)", "{", "    switch (format)",
              "    {"]
    lines += [f"    case {names.value(value['name'])}:" for value in listed]
    lines += ["        return true;", "    default:", "        return false;", "    }", "}"]
    lines += ["", "mrhiFormatCaps mrhiFloorFormatCaps(mrhiFormat format)", "{",
              "    switch (format)", "    {"]
    for value in listed:
        fields = ", ".join(f".{camel(key)} = {str(v).lower() if isinstance(v, bool) else v}"
                           for key, v in value["floor"].items())
        lines += [f"    case {names.value(value['name'])}:",
                  f"        return (mrhiFormatCaps){{{fields}}};"]
    lines += ["    default:", "        return (mrhiFormatCaps){0};", "    }", "}"]
    lines += ["", "bool mrhiFormatFamilyGranted(mrhiFormat format, const mrhiFeatures* features)",
              "{", "    switch (format)", "    {"]
    for value in listed:
        if value.get("family"):
            lines += [f"    case {names.value(value['name'])}:",
                      f"        return features->{camel(value['family'])};"]
    lines += ["    default:", "        return true;", "    }", "}"]
    lines += ["", "mrhiFormatBlock mrhiGetFormatBlock(mrhiFormat format)", "{", "    switch (format)",
              "    {"]
    for value in listed:
        if value["block"] != [1, 1]:
            lines += [f"    case {names.value(value['name'])}:",
                      f"        return (mrhiFormatBlock){{{value['block'][0]}, {value['block'][1]}}};"]
    lines += ["    default:", "        return (mrhiFormatBlock){1, 1};", "    }", "}"]
    aspects = {"color": "mrhi_aspectAll", "depth": "mrhi_aspectDepthOnly",
               "stencil": "mrhi_aspectStencilOnly"}
    lines += ["", "mrhiFormatCopy mrhiGetFormatCopy(mrhiFormat format, mrhiTextureAspect aspect)",
              "{", "    switch (format)", "    {"]
    for value in listed:
        lines += [f"    case {names.value(value['name'])}:"]
        for aspect, facts in value["copy"].items():
            lines += [f"        if (aspect == {aspects[aspect]})", "        {",
                      f"            return (mrhiFormatCopy){{{facts['bytes']}, "
                      f"{str(facts['source']).lower()}, {str(facts['destination']).lower()}}};",
                      "        }"]
        lines += ["        break;"]
    lines += ["    default:", "        break;", "    }", "    return (mrhiFormatCopy){0};", "}"]
    lines += ["", "mrhiFormat mrhiFormatSrgbPair(mrhiFormat format)", "{", "    switch (format)",
              "    {"]
    for value in listed:
        if value.get("srgb_pair"):
            lines += [f"    case {names.value(value['name'])}:",
                      f"        return {names.value(value['srgb_pair'])};"]
    lines += ["    default:", f"        return {names.value('format_none')};", "    }", "}"]
    for aspect in ("depth", "stencil"):
        lines += ["", f"bool mrhiFormatHas{aspect.capitalize()}(mrhiFormat format)", "{",
                  "    switch (format)", "    {"]
        lines += [f"    case {names.value(value['name'])}:" for value in listed
                  if aspect in value.get("aspects", [])]
        lines += ["        return true;", "    default:", "        return false;", "    }", "}"]
    lines += ["", "mrhiFormatTarget mrhiGetFormatTarget(mrhiFormat format)", "{",
              "    switch (format)", "    {"]
    for value in listed:
        target = value.get("target")
        if target:
            lines += [f"    case {names.value(value['name'])}:",
                      f"        return (mrhiFormatTarget){{{target['channels']}, "
                      f"{SCALARS[target['scalar']]}, {target['bytes']}, {target['alignment']}}};"]
    lines += ["    default:", "        return (mrhiFormatTarget){0};", "    }", "}"]
    vertex = find_item(contract, "vertex_format")
    lines += ["", "mrhiVertexLayout mrhiGetVertexLayout(mrhiVertexFormat format)", "{",
              "    switch (format)", "    {"]
    for value in vertex["values"]:
        layout = value.get("layout")
        if layout:
            lines += [f"    case {names.value(value['name'])}:",
                      f"        return (mrhiVertexLayout){{{SCALARS[layout['scalar']]}, "
                      f"{layout['components']}, {layout['bytes']}}};"]
    lines += ["    default:", "        return (mrhiVertexLayout){0};", "    }", "}"]
    lines += ["", "uint32_t mrhiFormatIndex(mrhiFormat format)", "{", "    switch (format)", "    {"]
    for position, value in enumerate(listed):
        lines += [f"    case {names.value(value['name'])}:", f"        return {position};"]
    lines += ["    default:", f"        return {len(listed)};", "    }", "}"]
    lines += ["", "bool mrhiFormatCapsWithin(const mrhiFormatCaps* asked, "
              "const mrhiFormatCaps* granted)", "{", "    return true"]
    for member in caps["members"]:
        name = camel(member["name"])
        if member["type"] == "bool":
            lines.append(f"        && (!asked->{name} || granted->{name})")
        else:
            lines.append(f"        && (asked->{name} & ~granted->{name}) == 0")
    lines[-1] += ";"
    return lines + ["}"]


def emit_result_names(contract):
    """The source of the GetResultName function, one case per result value."""
    names = Names(contract)
    result = next(item for header in contract["headers"] for item in header["items"]
                  if item["kind"] == "result")
    function = names.function("get_" + result["name"] + "_name")
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Copyright (c) 2026 Sirac Ozmen",
        "//",
        f"// Generated by tools/gen_contract.py from {CONTRACT}: the names of",
        "// the result values.",
        "",
        "// clang-format off",
        "",
        f'#include "{contract["library"]}/base.h"',
        "",
        f"const char* {function}({names.type(result['name'])} result)",
        "{",
        "    switch (result)",
        "    {",
    ]
    for value in result["values"]:
        lines += [f"    case {names.value(value['name'])}:",
                  f'        return "{names.value(value["name"])}";']
    lines += ["    default:", '        return "unknown result";', "    }", "}"]
    return "\n".join(lines) + "\n"


def emit_diagnostic_texts(contract):
    """The source of the GetDiagnosticText function: each diagnostic code's
    doc, which is its text."""
    names = Names(contract)
    codes = find_item(contract, "diagnostic_code")
    function = names.function("get_diagnostic_text")
    lines = [
        "// SPDX-License-Identifier: MIT",
        "// Copyright (c) 2026 Sirac Ozmen",
        "//",
        f"// Generated by tools/gen_contract.py from {CONTRACT}: the texts of",
        "// the diagnostic codes.",
        "",
        "// clang-format off",
        "",
        f'#include "{contract["library"]}/base.h"',
        "",
        f"const char* {function}({names.type(codes['name'])} code)",
        "{",
        "    switch (code)",
        "    {",
    ]
    for value in codes["values"]:
        text = value["doc"].replace("\\", "\\\\").replace('"', '\\"')
        lines += [f"    case {names.value(value['name'])}:", f'        return "{text}";']
    lines += ["    default:", '        return "An unknown diagnostic code.";', "    }", "}"]
    return "\n".join(lines) + "\n"


def formatted(path, text):
    """C text through the project's clang-format, the pinned version CI
    installs, so that the output needs no reformatting."""
    tool = shutil.which("clang-format")
    if tool is None:
        raise SystemExit("gen_contract.py needs clang-format (pip install clang-format==22.1.5)")
    style = "file:" + os.path.join(ROOT, ".clang-format")
    result = subprocess.run([tool, "--style=" + style, "--assume-filename=" + path], input=text,
                            capture_output=True, text=True, check=True)
    return result.stdout


def outputs(contract):
    """Each generated file's path relative to the root, with its text."""
    files = {}
    for header in contract["headers"]:
        path = os.path.join("include", contract["library"], header["name"] + ".h")
        files[path] = formatted(path, emit_header(contract, header))
    files[os.path.join("src", "generated", "result_names.c")] = emit_result_names(contract)
    files[os.path.join("src", "generated", "diagnostic_texts.c")] = emit_diagnostic_texts(contract)
    files[os.path.join("src", "generated", "defaults.c")] = emit_defaults(contract)
    files[os.path.join("src", "generated", "capabilities.c")] = emit_capability_checks(contract)
    files[os.path.join("src", "generated", "out_copies.h")] = emit_out_copies(contract)
    # The contract itself, in one canonical layout, so that its diffs
    # show only what changed.
    files[CONTRACT] = json.dumps(contract, indent=2, ensure_ascii=False) + "\n"
    return files


def self_test(contract):
    """Plants each case of the limits' codes, of a function's object and of
    an input-only id, in a copy of the contract: whether validate refuses
    it, naming the member, the object or the argument."""

    def planted(change):
        made = copy.deepcopy(contract)
        items = {item["name"]: item for header in made["headers"] for item in header["items"]}
        change(items["limits"], items["diagnostic_code"], items)
        return made

    def member(name):
        return {"name": name, "type": "uint32", "default": 1, "doc": "A limit.",
                "mapping": copy.deepcopy(find_item(contract, "limits")["members"][0]["mapping"])}

    def code(name, value):
        return {"name": name, "value": value, "doc": "A limit's refusal."}

    def only_input(item, name, value=True, members="args"):
        next(arg for arg in item[members] if arg["name"] == name)["input_only"] = value

    top = max(value["value"] for value in find_item(contract, "diagnostic_code")["values"])
    cases = [
        ("as committed", lambda limits, codes, items: None, None),
        ("a new limit without a code",
         lambda limits, codes, items: limits["members"].append(member("made_up")), "'made_up' has no"),
        ("a new limit with its code",
         lambda limits, codes, items: (limits["members"].append(member("made_up")),
                                codes["values"].append(code("diagnostic_limit_made_up", top + 1))),
         None),
        ("a code for no limit",
         lambda limits, codes, items: codes["values"].append(code("diagnostic_limit_made_up", top + 1)),
         "'diagnostic_limit_made_up' names no member"),
        ("a code beside a reason",
         lambda limits, codes, items: codes["values"].append(
             code("diagnostic_limit_color_attachments", top + 1)),
         "'color_attachments' has a code and a reason"),
        ("a reason for a limit gone",
         lambda limits, codes, items: limits["members"].remove(
             next(m for m in limits["members"] if m["name"] == "frames_in_flight")),
         "'frames_in_flight', which limits lacks"),
        ("an object of no id or opaque kind",
         lambda limits, codes, items: items["pass_begin"].update(object="draw"),
         "its object 'draw' is not 'pass'"),
        ("the root for an object on an id",
         lambda limits, codes, items: items["pass_begin"].update(object="device"),
         "its object 'device' is not 'pass'"),
        ("no object on an id",
         lambda limits, codes, items: items["pass_begin"].pop("object"),
         "its object 'None' is not 'pass'"),
        ("an id for an object on the root",
         lambda limits, codes, items: items["device_get_state"].update(object="pass"),
         "its object 'pass' is not 'device'"),
        ("an object on a creation",
         lambda limits, codes, items: items["create_buffer"].update(object="device"),
         "its object 'device' is not 'None'"),
        ("a name that starts with the object's letters only",
         lambda limits, codes, items: items["pass_begin"].update(name="passes_begin"),
         "its object 'pass' does not start"),
        ("an input-only first id, the next id named",
         lambda limits, codes, items: (
             only_input(items["pass_set_compute_pipeline"], "pass"),
             items["pass_set_compute_pipeline"].update(object="compute_pipeline",
                                                       name="compute_pipeline_set")),
         None),
        ("an input-only first id named",
         lambda limits, codes, items: only_input(items["pass_set_compute_pipeline"], "pass"),
         "its object 'pass' is not 'compute_pipeline'"),
        ("an input-only only id, the root named",
         lambda limits, codes, items: (only_input(items["pass_begin"], "pass"),
                                       items["pass_begin"].update(object="device",
                                                                  name="device_begin")),
         None),
        ("an input-only only id named",
         lambda limits, codes, items: only_input(items["pass_begin"], "pass"),
         "its object 'pass' is not 'device'"),
        ("input_only on the root",
         lambda limits, codes, items: only_input(items["pass_begin"], "device"),
         "'device' is input_only"),
        ("input_only false",
         lambda limits, codes, items: only_input(items["pass_begin"], "pass", False),
         "'pass' is input_only"),
        ("input_only on an output id",
         lambda limits, codes, items: only_input(items["pass_read_buffer"], "request_out"),
         "'request_out' is input_only"),
        ("input_only on a return value",
         lambda limits, codes, items: items["pass_begin"]["returns"].update(input_only=True),
         "'the return value' is input_only"),
        ("input_only on a struct member",
         lambda limits, codes, items: only_input(items["buffer_def"], "size", members="members"),
         "'size' is input_only"),
        ("input_only on a function pointer's argument",
         lambda limits, codes, items: only_input(
             next(m for m in items["allocator"]["members"] if m["name"] == "alloc"), "size"),
         "'size' is input_only"),
    ]
    wrong = 0
    for name, change, expected in cases:
        errors = [error for error in validate(planted(change))
                  if "limits" in error or "diagnostic_limit_" in error or "its object" in error
                  or "input_only" in error]
        right = not errors if expected is None else any(expected in e for e in errors)
        if not right:
            print(f"self-test: {name}: {errors or 'taken'}", file=sys.stderr)
            wrong += 1
    print(f"self-test: {len(cases) - wrong} of {len(cases)} cases right")
    return 1 if wrong else 0


def main(argv):
    if argv == ["--self-test"]:
        with open(os.path.join(ROOT, CONTRACT), encoding="utf-8") as f:
            sys.exit(self_test(json.load(f)))
    check = "--check" in argv
    rest = [arg for arg in argv if arg != "--check"]
    mappings = None
    if rest[:1] == ["--mappings"] and len(rest) == 2:
        mappings = rest[1]
    elif rest:
        raise SystemExit("usage: gen_contract.py [--check] [--mappings OUTPUT.md] | --self-test")
    with open(os.path.join(ROOT, CONTRACT), encoding="utf-8") as f:
        contract = json.load(f)
    errors = validate(contract)
    if errors:
        raise SystemExit("\n".join(f"{CONTRACT}: {error}" for error in errors))
    stale = []
    for path, text in outputs(contract).items():
        full = os.path.join(ROOT, path)
        current = open(full, encoding="utf-8").read() if os.path.exists(full) else None
        if current == text:
            continue
        stale.append(path)
        if not check:
            with open(full, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
    if mappings is not None:
        with open(mappings, "w", encoding="utf-8", newline="\n") as f:
            f.write(emit_mappings(contract))
    if check and stale:
        raise SystemExit("stale, run tools/gen_contract.py: " + ", ".join(stale))
    print(f"contract: {len(outputs(contract))} files, {len(stale)} "
          + ("stale" if check else "written"))


if __name__ == "__main__":
    main(sys.argv[1:])
