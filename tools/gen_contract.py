#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Writes the public headers and the thread safety table from the
# contract, docs/contract/mrhi.json, which is their source of truth. The
# contract's names are snake_case; this generator turns them into the
# family's C names (docs/conventions.md, section 4). The output is in
# the family's style, so the format, documentation and source checks
# apply to it as to hand-written code.
#
# usage: gen_contract.py [--check]
#   --check  writes nothing; fails naming each file that differs from
#            what the contract generates.

import json
import os
import re
import sys
import textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTRACT = os.path.join("docs", "contract", "mrhi.json")
WIDTH = 80
NAME = re.compile(r"^[a-z][a-z0-9]*(_[a-z0-9]+)*$")
KINDS = ("result", "struct", "function")
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
}
# The openings of a thread safety paragraph (docs/conventions.md,
# section 10), by class.
THREAD_SAFETY = {
    "any": "Safe from any thread.",
    "any_exclusive": "Safe from any thread; {object} is used by one thread at a time.",
    "main": "Main thread only.",
    "realtime": "Real-time safe: no allocation, lock or wait.",
}


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


def check_name(errors, where, name):
    if not isinstance(name, str) or not NAME.match(name):
        errors.append(f"{where}: '{name}' is not a snake_case name")


def check_type(errors, where, kind_of, type_name):
    """Checks that a type reference names a primitive or a declaration."""
    if type_name in PRIMITIVES:
        return
    if type_name == "result" and "result" in kind_of.values():
        return
    kind, _, name = type_name.partition(".")
    if kind_of.get(name) != kind:
        errors.append(f"{where}: unknown type '{type_name}'")


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


def check_function(errors, where, item, kind_of):
    returns = item.get("returns")
    if returns is not None:
        check_type(errors, where, kind_of, returns.get("type"))
        if not returns.get("doc"):
            errors.append(f"{where}: the return value needs a doc")
    for arg in item.get("args", []):
        check_name(errors, where, arg.get("name"))
        check_type(errors, where, kind_of, arg.get("type"))
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
    for key in ("library", "title", "prefix", "macro", "guard", "version", "headers"):
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
    for item in items:
        where = f"{item['kind']} '{item['name']}'"
        if item["kind"] == "result":
            check_values(errors, where, item)
        elif item["kind"] == "struct":
            for member in item.get("members", []):
                check_name(errors, where, member.get("name"))
                check_type(errors, where, kind_of, member.get("type"))
        elif item["kind"] == "function":
            check_function(errors, where, item, kind_of)
    return errors


def comment(text, indent, marker):
    """Text wrapped into comment lines of the given marker."""
    lead = " " * indent + marker + " "
    return [lead + line for line in textwrap.wrap(text, WIDTH - len(lead))]


def c_type(names, type_name):
    if type_name in PRIMITIVES:
        return PRIMITIVES[type_name]
    return names.type(type_name.partition(".")[2] or type_name)


def preamble_version(contract, names):
    version = contract["version"]
    lines = ["// The library version. CMake reads it from here."]
    for part in ("major", "minor", "patch"):
        lines.append(f"#define {names.macro}_VERSION_{part.upper()} {version[part]}")
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


def emit_result(names, item):
    lines = comment(item["doc"], 4, "//")
    lines.append(f"    typedef int32_t {names.type(item['name'])};")
    lines += ["", "    enum", "    {"]
    for value in item["values"]:
        lines += comment(value["doc"], 8, "//")
        lines.append(f"        {names.value(value['name'])} = {value['value']},")
    lines.append("    };")
    return lines


def emit_struct(names, item):
    c_name = names.type(item["name"])
    lines = comment(item["doc"], 4, "//")
    lines += [f"    typedef struct {c_name}", "    {"]
    for member in item["members"]:
        if member.get("doc"):
            lines += comment(member["doc"], 8, "//")
        lines.append(f"        {c_type(names, member['type'])} {camel(member['name'])};")
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
    opening = THREAD_SAFETY[safety["class"]].format(object=safety.get("object", ""))
    lines.append("    /// @par Thread safety")
    lines += comment(" ".join([opening] + ([safety["note"]] if safety.get("note") else [])), 4, "///")
    returns = item.get("returns", {}).get("type")
    result = c_type(names, returns) if returns else "void"
    params = ", ".join(f"{c_type(names, a['type'])} {camel(a['name'])}" for a in args) or "void"
    nodiscard = f"{names.macro}_NODISCARD " if returns == "result" else ""
    lines.append(
        f"    {names.macro}_API {nodiscard}{result} {names.function(item['name'])}({params});")
    return lines


EMITTERS = {"result": emit_result, "struct": emit_struct, "function": emit_function}


def emit_header(contract, header):
    names = Names(contract)
    guard = f"{contract['guard']}_{header['name'].upper()}_H"
    lines = ["// SPDX-License-Identifier: MIT", "// Copyright (c) 2026 Sirac Ozmen", "//"]
    lines += comment(header["doc"], 0, "//")
    lines += ["//"]
    lines += comment(f"Generated by tools/gen_contract.py from {CONTRACT}: edit the contract, "
                     "not this file.", 0, "//")
    lines += ["", f"#ifndef {guard}", f"#define {guard}", ""]
    lines += ["#include <stddef.h>", "#include <stdint.h>", ""]
    lines += ["#ifdef __cplusplus", 'extern "C"', "{", "#endif", ""]
    for preamble in header.get("preamble", []):
        lines += PREAMBLES[preamble](contract, names) + [""]
    for item in header["items"]:
        lines += EMITTERS[item["kind"]](names, item) + [""]
    lines += ["#ifdef __cplusplus", "}", "#endif", "", f"#endif // {guard}"]
    return "\n".join(lines) + "\n"


def emit_thread_table(contract):
    names = Names(contract)
    lines = [f"# {contract['title']} thread safety", ""]
    lines += textwrap.wrap(f"Generated by `tools/gen_contract.py` from `{CONTRACT}`: edit the "
                           "contract, not this file.", 72)
    lines += [
        "",
        "| Function | Thread safety |",
        "| --- | --- |",
    ]
    for header in contract["headers"]:
        for item in header["items"]:
            if item["kind"] != "function":
                continue
            safety = item["thread_safety"]
            text = THREAD_SAFETY[safety["class"]].format(object=safety.get("object", ""))
            lines.append(f"| `{names.function(item['name'])}` | {text} |")
    return "\n".join(lines) + "\n"


def outputs(contract):
    """Each generated file's path relative to the root, with its text."""
    files = {
        os.path.join("include", contract["library"], header["name"] + ".h"):
        emit_header(contract, header)
        for header in contract["headers"]
    }
    files[os.path.join("docs", "contract", "thread-safety.md")] = emit_thread_table(contract)
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
