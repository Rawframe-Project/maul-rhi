#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# The stamp of a committed generated shader header, which
# tools/gen_test_shaders.py and tools/gen_kernels.py write and check.
# CI has none of the shader tools, so a header could go stale unseen;
# the stamp lets a check with no tools tell. Three lines in the header's
# comment:
#   // inputs sha256:HEX   every input it was made from: its sources, the
#                          generator and the writer scripts, each named
#                          by its path, with CRLF read as LF
#   // tool NAME VERSION   each pinned tool that made it, a line each, and
#                          for DXC the flags it compiled with
#   // bytes sha256:HEX    the bytes its array holds
# A check recomputes the inputs' digest from the tree and the bytes'
# digest from the array, and compares the tools with the generator's
# pins, so it fails on a changed input, a moved pin or a hand-edited
# header. It also refuses DXIL whose DXC flags lack -Gis, which keeps
# DXC from marking the float math fast, even when the pins agree. The self-test proves each of those fails on a copy of the
# tree, and that a copy with CRLF line ends passes.
#
# Standard library only.

import contextlib
import hashlib
import importlib.util
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

STAMP = re.compile(r"^// (inputs|tool|bytes) (.*)$", re.MULTILINE)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mrhi_dxil import DXC_FLAGS  # noqa: E402
# The family's DXC, which both generators pin: what dxc --version
# reports of it, and the stamp's lines naming the release asset of
# microsoft/DirectXShaderCompiler with its SHA-256.
DXC_IDENTITY = "libdxcompiler 1.9.0.1, 0d3ee6b5"
DXC = (
    "dxc linux_dxc_2026_07_29 (v1.9.2607, libdxcompiler 1.9.0.1, 0d3ee6b5)",
    "dxc sha256:55665c87824051ed4774ff3280a79ccbbb7d39243b9736ca5e98222134112d54",
    "dxc flags " + " ".join(DXC_FLAGS),
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


def refuse_unpinned(found, pinned):
    """Exits naming each installed tool whose identity is not exactly its
    pin; both map a tool's name to its identity."""
    wrong = [f"{name}: installed {found.get(name)}, pinned {pin}"
             for name, pin in pinned.items() if found.get(name) != pin]
    if wrong:
        sys.exit("the installed tools are not the pinned ones:\n  " + "\n  ".join(wrong))


# An input that is not a file's bytes: the contract's values the container
# writer reads (mrhi_container.Enums.canonical), so that changing one
# stales every container while an edit to the contract's prose does not.
CONTRACT_VALUES = "contract/mrhi.json values"


def contract_values(root):
    """The contract's values the root's container writer reads, as bytes."""
    spec = importlib.util.spec_from_file_location(
        "stamped_writer", os.path.join(root, "tools", "mrhi_container.py"))
    writer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(writer)
    with open(os.path.join(root, "contract", "mrhi.json"), encoding="utf-8") as f:
        return writer.Enums(json.load(f)).canonical()


def inputs_digest(root, paths):
    """The SHA-256 of the inputs, each its path and its bytes with CRLF as
    LF, in path order."""
    digest = hashlib.sha256()
    for path in sorted(paths):
        if path == CONTRACT_VALUES:
            digest.update(path.encode() + b"\0" + contract_values(root) + b"\0")
            continue
        with open(os.path.join(root, path), "rb") as f:
            data = f.read().replace(b"\r\n", b"\n")
        digest.update(path.encode() + b"\0" + str(len(data)).encode() + b"\0" + data)
    return digest.hexdigest()


def stamp_lines(root, inputs, tools, data):
    """The stamp's comment lines for a header made from the inputs by the
    tools, a list of pins."""
    return ([f"// inputs sha256:{inputs_digest(root, inputs)}"] +
            [f"// tool {tool}" for tool in tools] +
            [f"// bytes sha256:{hashlib.sha256(data).hexdigest()}"])


def array_bytes(text):
    """The bytes a header's array holds."""
    body = text[text.index("] = {") + 5:text.index("};")]
    return bytes(int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]{2})", body))


def problems(root, units, tools, compare_inputs=True):
    """What is wrong with each unit's header, as (header, kind, detail):
    kind is "missing", "inputs", "tools", "flags" or "bytes". A unit is a header's
    path and its inputs' paths, relative to the root."""
    found = []
    for header, inputs in units:
        try:
            with open(os.path.join(root, header), "rb") as f:
                text = f.read().replace(b"\r\n", b"\n").decode("utf-8")
            lines = STAMP.findall(text)
            data = array_bytes(text)
        except (OSError, UnicodeDecodeError, ValueError) as error:
            found.append((header, "missing", str(error)))
            continue
        stamp = {key: value for key, value in lines if key != "tool"}
        made_by = [value for key, value in lines if key == "tool"]
        if len(stamp) != 2:
            found.append((header, "missing", "no stamp"))
            continue
        if compare_inputs and stamp["inputs"] != f"sha256:{inputs_digest(root, inputs)}":
            found.append((header, "inputs", "an input changed since it was made"))
        if made_by != list(tools):
            found.append((header, "tools", f"made by {'; '.join(made_by)}, the pins are "
                          f"{'; '.join(tools)}"))
        flags = [line.split()[2:] for line in made_by if line.startswith("dxc flags")]
        if any(line.startswith("dxc ") for line in made_by) and not any(
                "-Gis" in line for line in flags):
            found.append((header, "flags", "its DXIL was compiled without -Gis, so DXC marked "
                          "its float math fast"))
        if stamp["bytes"] != f"sha256:{hashlib.sha256(data).hexdigest()}":
            found.append((header, "bytes", "its bytes were edited"))
    return found


def check(root, units, tools, script, compare_inputs=True):
    """Prints each problem and answers the exit status."""
    found = problems(root, units, tools, compare_inputs)
    for header, _, detail in found:
        print(f"{header}: {detail}; run {script} where its tools are")
    if not found:
        print(f"{script}: {len(units)} headers match their inputs and pins")
    return 1 if found else 0


def parse_failures(parse):
    """What is wrong with a script's flags: parse, given its arguments,
    must refuse --skip-inputs without --self-test, where it would quietly
    turn off the input comparison of --check, and accept the rest."""
    def refused(argv):
        try:
            with contextlib.redirect_stderr(io.StringIO()):
                parse(argv)
        except SystemExit as stop:
            return stop.code not in (0, None)
        return False

    wanted = ((["--check", "--skip-inputs"], True), (["--skip-inputs"], True),
              (["--check"], False), (["--self-test", "--skip-inputs"], False))
    return [f"{' '.join(argv)}: {'accepted' if want else 'refused'}"
            for argv, want in wanted if refused(argv) != want]


def self_test(root, copied, units_of, tools, parse, compare_inputs=True, cases=()):
    """Proves the check on copies of the tree: it passes as committed and
    with CRLF inputs, and fails on a changed input, a changed pin and an
    edited header, each for its own reason, and as each of the cases says
    (what, a change of a copy's root, the problem kinds wanted). copied
    names the files and folders to copy; units_of answers the units for a
    root; parse is the script's parser, which parse_failures proves.
    Answers the exit status."""
    failures = parse_failures(parse)

    def copy():
        work = tempfile.mkdtemp()
        for path in copied:
            source = os.path.join(root, path)
            target = os.path.join(work, path)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            if os.path.isdir(source):
                shutil.copytree(source, target)
            else:
                shutil.copy2(source, target)
        return work

    def kinds(work):
        return {kind for _, kind, _ in problems(work, units_of(work), tools, compare_inputs)}

    def expect(what, change, wanted):
        work = copy()
        try:
            header, inputs = units_of(work)[0]
            change(work, header, inputs)
            found = kinds(work)
        finally:
            shutil.rmtree(work)
        if found != wanted:
            failures.append(f"{what}: found {sorted(found) or 'nothing'}, wanted "
                            f"{sorted(wanted) or 'nothing'}")

    def edit(work, path, old, new):
        full = os.path.join(work, path)
        with open(full, "rb") as f:
            data = f.read()
        with open(full, "wb") as f:
            f.write(data.replace(old, new, 1) if old else data + new)

    def crlf(work, header, inputs):
        for path in [p for p in inputs if p != CONTRACT_VALUES] + [header]:
            full = os.path.join(work, path)
            with open(full, "rb") as f:
                data = f.read().replace(b"\r\n", b"\n")
            with open(full, "wb") as f:
                f.write(data.replace(b"\n", b"\r\n"))

    def edited_byte(work, header, inputs):
        full = os.path.join(work, header)
        with open(full, encoding="utf-8") as f:
            text = f.read()
        at = text.index("0x", text.index("] = {"))
        value = int(text[at + 2:at + 4], 16) ^ 1
        with open(full, "w", encoding="utf-8", newline="") as f:
            f.write(text[:at + 2] + f"{value:02X}" + text[at + 4:])

    expect("the tree as committed", lambda work, header, inputs: None, set())
    expect("a changed input", lambda work, header, inputs: edit(work, inputs[0], b"", b"\n"),
           {"inputs"})
    expect("a changed pin", lambda work, header, inputs: edit(work, header, b"// tool ",
                                                              b"// tool moved "), {"tools"})
    expect("an edited header", edited_byte, {"bytes"})
    expect("CRLF line ends", crlf, set())
    if any(tool.startswith("dxc flags") for tool in tools):
        expect("DXIL without -Gis",
               lambda work, header, inputs: edit(work, header, b" -Gis", b""), {"tools", "flags"})
    for what, change, wanted in cases:
        expect(what, lambda work, header, inputs, change=change: change(work), wanted)
    for failure in failures:
        print(f"self-test: {failure}")
    if not failures:
        print("self-test: the check passes as committed and with CRLF, and fails on a changed "
              "input, pin or header, or DXIL without -Gis; --skip-inputs is refused "
              "without --self-test" + "".join(f"; {what}: as wanted" for what, _, _ in cases))
    return 1 if failures else 0
