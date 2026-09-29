#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes the public headers, the result names and the thread safety
# table from the contract, docs/contract/mrhi.json, which is their
# source of truth (docs/contract/README.md describes it). The contract's
# names are snake_case; this generator turns them into the family's C
# names (docs/conventions.md, section 4). Headers go through the pinned
# clang-format, so the format, documentation and source checks apply to
# them as to hand-written code.
#
# usage: gen_contract.py [--check]
#   --check  writes nothing; fails naming each file that differs from
#            what the contract generates.

import json
import os
import re
import shutil
import subprocess
import sys
import textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTRACT = os.path.join("docs", "contract", "mrhi.json")
WIDTH = 80
NAME = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")
KINDS = ("constant", "result", "enum", "bitflags", "opaque", "id", "struct", "function")
# The kinds a type reference may name, as kind.name.
REFERABLE = ("enum", "bitflags", "opaque", "id", "struct")
WIDTHS = ("uint8", "uint16", "uint32", "int32")
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
# The four APIs every concept maps onto (the requirements' four-API
# rule), and the classes a mapping row may have.
APIS = (("vulkan", "Vulkan"), ("d3d12", "D3D12"), ("metal", "Metal"), ("web_gpu", "WebGPU"))
CLASSES = ("direct", "emulated", "restricted", "absent_rejected")
# The openings of a thread safety paragraph (docs/conventions.md,
# section 10), by class.
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

    def function(self, name):
        return self.prefix + pascal(name)

    def value(self, name):
        return self.prefix + "_" + camel(name)

    def constant(self, name):
        return self.macro + "_" + name.upper()


# Validation: every message names where the contract is wrong.


def check_name(errors, where, name):
    if not isinstance(name, str) or not NAME.match(name):
        errors.append(f"{where}: '{name}' is not a snake_case name")


def check_type(errors, where, kind_of, spec):
    """Checks a type spec: a type and, for members and arguments, a pointer."""
    type_name, pointer = spec.get("type"), spec.get("pointer")
    if pointer not in POINTERS:
        errors.append(f"{where}: unknown pointer '{pointer}'")
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


def check_signature(errors, where, kind_of, spec):
    """Checks a function's or a function pointer's return and arguments."""
    returns = spec.get("returns")
    if returns is not None:
        check_type(errors, where, kind_of, returns)
    for arg in spec.get("args", []):
        check_name(errors, where, arg.get("name"))
        check_type(errors, where, kind_of, arg)


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
    if item["kind"] in ("enum", "bitflags") and item.get("width") not in WIDTHS:
        errors.append(f"{where}: needs a width, one of {', '.join(WIDTHS)}")
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
    """A concept's four rows: each with a class and a note on how."""
    if not isinstance(mapping, dict):
        errors.append(f"{where}: needs a mapping row for each API")
        return
    for api, _ in APIS:
        row = mapping.get(api)
        if not isinstance(row, dict) or row.get("class") not in CLASSES or not row.get("note"):
            errors.append(f"{where}: the {api} row needs a class ({', '.join(CLASSES)}) and a note")
    if set(mapping) - {api for api, _ in APIS}:
        errors.append(f"{where}: a mapping row for an unknown API")


def check_function(errors, where, item, kind_of):
    check_signature(errors, where, kind_of, item)
    if item.get("returns") is not None and not item["returns"].get("doc"):
        errors.append(f"{where}: the return value needs a doc")
    for arg in item.get("args", []):
        if not arg.get("doc"):
            errors.append(f"{where}: argument '{arg.get('name')}' needs a doc")
    safety = item.get("thread_safety", {})
    if safety.get("class") not in THREAD_SAFETY:
        errors.append(f"{where}: unknown thread safety class '{safety.get('class')}'")
    elif safety["class"] == "any_exclusive" and not safety.get("object"):
        errors.append(f"{where}: an exclusive thread safety class names its object")


def validate(contract):
    """The contract's structural errors, as messages; empty when valid."""
    errors, kind_of = [], {}
    for key in ("library", "title", "prefix", "macro", "guard", "version", "contract_version",
                "headers"):
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
        where = f"{item['kind']} '{item['name']}'"
        if item["kind"] in checks:
            checks[item["kind"]](errors, where, item)
        elif item["kind"] == "struct":
            check_struct(errors, where, item, kind_of)
        elif item["kind"] == "function":
            check_function(errors, where, item, kind_of)
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


def preamble_version(contract, names):
    version, macro = contract["version"], names.macro
    lines = ["// The library version. CMake reads it from here."]
    for part in ("major", "minor", "patch"):
        lines.append(f"#define {macro}_VERSION_{part.upper()} {version[part]}")
    lines += [""]
    lines += comment("The contract version a program is built against. An instance refuses any "
                     "other before 1.0.", 0, "//")
    lines.append(f"#define {macro}_CONTRACT_VERSION {contract['contract_version']}")
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
        "// C++17 and left out for older dialects.",
        "#if defined(__cplusplus) && __cplusplus >= 201703L",
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
    width = PRIMITIVES[item["width"]] if item["kind"] != "result" else "int32_t"
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
                                            f"{item['value']}"]


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
    nodiscard = f"{names.macro}_NODISCARD " if returns and returns["type"] == "result" else ""
    return lines + [f"    {nodiscard}{names.macro}_API {result} "
                    f"{names.function(item['name'])}({c_signature(names, item)});"]


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
                cells = [f"{row['class'].replace('_', '-')}: {row['note']}"
                         for row in (entry["mapping"][api] for api, _ in APIS)]
                label = names.value(entry["name"]) if is_enum else camel(entry["name"])
                lines.append(f"| `{label}` | " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


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


def emit_thread_table(contract):
    names = Names(contract)
    lines = [f"# {contract['title']} thread safety", ""]
    lines += textwrap.wrap(f"Generated by `tools/gen_contract.py` from `{CONTRACT}`: edit the "
                           "contract, not this file.", 72)
    lines += ["", "| Function | Thread safety |", "| --- | --- |"]
    for header in contract["headers"]:
        for item in header["items"]:
            if item["kind"] != "function":
                continue
            safety = item["thread_safety"]
            text = opening_of(safety)
            lines.append(f"| `{names.function(item['name'])}` | {text} |")
    return "\n".join(lines) + "\n"


def emit_result_names(contract):
    """The source of the ResultName function, one case per result value."""
    names = Names(contract)
    result = next(item for header in contract["headers"] for item in header["items"]
                  if item["kind"] == "result")
    function = names.function(result["name"] + "_name")
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
    files[os.path.join("docs", "contract", "thread-safety.md")] = emit_thread_table(contract)
    files[os.path.join("src", "generated", "result_names.c")] = emit_result_names(contract)
    files[os.path.join("src", "generated", "defaults.c")] = emit_defaults(contract)
    files[os.path.join("src", "generated", "capabilities.c")] = emit_capability_checks(contract)
    files[os.path.join("docs", "contract", "mappings.md")] = emit_mappings(contract)
    # The contract itself, in one canonical layout, so that its diffs
    # show only what changed.
    files[CONTRACT] = json.dumps(contract, indent=2, ensure_ascii=False) + "\n"
    return files


def main(argv):
    if argv not in ([], ["--check"]):
        raise SystemExit("usage: gen_contract.py [--check]")
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
        if not argv:
            with open(full, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
    if argv and stale:
        raise SystemExit("stale, run tools/gen_contract.py: " + ", ".join(stale))
    print(f"contract: {len(outputs(contract))} files, {len(stale)} "
          + ("stale" if argv else "written"))


if __name__ == "__main__":
    main(sys.argv[1:])
