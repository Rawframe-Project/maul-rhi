#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# tools/shader_tools.py's pins, with each installed tool's answer faked:
# the pinned spirv-cross and DXC pass; another DXC build, a spirv-cross
# naming a revision of its own and a missing one are each named with its
# pin; tools/mrhi_dxil.py refuses another DXC or spirv-cross before it
# reads anything, and tools/mrhi_msl.py another spirv-cross but not
# another DXC, which it does not run; `tools/mrhi_container.py --tools`
# prints the pins as the committed headers' stamps name them.
#
# usage: test_shader_tools.py

import contextlib
import glob
import importlib.util
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "tools")
sys.path.insert(0, TOOLS)

import shader_tools  # noqa: E402

failures = 0


def expect(condition, what):
    global failures
    if not condition:
        failures += 1
        print(f"FAIL: {what}")


# What each probe prints: the family's DXC, and the distribution's
# spirv-cross, which names no revision but its package's version.
PINNED_ANSWERS = {
    ("dxc", "--version"): "dxcompiler.so: 1.9(1-0d3ee6b5)(1.9.0.1)\n",
    ("spirv-cross", "--revision"): "(Debian package)\n",
    ("dpkg-query", "-W", "-f=${Version}", "spirv-cross"): "2021.01.15+1.3.239.0-1build1",
}


def answering(answers):
    """A stand-in for shader_tools.output answering from a table."""
    return lambda *command: answers.get(command, "")


def with_answers(answers, call):
    """call's result with the tools answering from answers."""
    real = shader_tools.output
    shader_tools.output = answering(answers)
    try:
        return call()
    finally:
        shader_tools.output = real


def refusal(call):
    """The message call exits with, or None when it does not exit."""
    try:
        call()
    except SystemExit as stop:
        return str(stop.code)
    return None


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(TOOLS, name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_main(module, answers):
    """A crossing script's main on files that do not exist, with the
    tools answering from answers: its refusal, or its status."""
    argv = sys.argv
    sys.argv = [module.__name__, "missing.spv", "missing.json", "out"]
    try:
        with contextlib.redirect_stderr(io.StringIO()):
            stopped = with_answers(answers, lambda: refusal(module.main))
    finally:
        sys.argv = argv
    return stopped


found = with_answers(PINNED_ANSWERS, lambda: shader_tools.identities(shader_tools.CROSSING))
expect(shader_tools.unpinned(found, shader_tools.CROSSING) == [],
       f"the pinned spirv-cross and DXC pass: {found}")

other_dxc = dict(PINNED_ANSWERS)
other_dxc[("dxc", "--version")] = "dxcompiler.so: 1.8(2-1a2b3c4d)(1.8.0.2)\n"
found = with_answers(other_dxc, lambda: shader_tools.identities(shader_tools.CROSSING))
expect(shader_tools.unpinned(found, shader_tools.CROSSING) ==
       ["dxc: installed libdxcompiler 1.8.0.2, 1a2b3c4d, pinned libdxcompiler 1.9.0.1, 0d3ee6b5"],
       "another DXC named with its pin")

own_revision = dict(PINNED_ANSWERS)
own_revision[("spirv-cross", "--revision")] = "2024-06-11\n"
found = with_answers(own_revision, lambda: shader_tools.identities(("spirv-cross",)))
expect(found == {"spirv-cross": "spirv-cross 2024-06-11"},
       "a spirv-cross naming its own revision read as it")
expect(len(shader_tools.unpinned(found, ("spirv-cross",))) == 1, "and refused")

found = with_answers({}, lambda: shader_tools.identities(shader_tools.CROSSING))
expect(found == {"spirv-cross": "spirv-cross: not found", "dxc": "dxc: not found"},
       f"missing tools named as missing: {found}")

stopped = with_answers(other_dxc, lambda: refusal(
    lambda: shader_tools.refuse_unpinned("a cook", shader_tools.CROSSING)))
expect(stopped is not None and stopped.startswith("a cook: ") and "dxc: installed" in stopped,
       "a refusal names the program and the tool")
expect(with_answers(PINNED_ANSWERS, lambda: refusal(
    lambda: shader_tools.refuse_unpinned("a cook", shader_tools.CROSSING))) is None,
    "no refusal of the pinned tools")

dxil = load("mrhi_dxil")
msl = load("mrhi_msl")
for name, answers in (("DXC", other_dxc), ("spirv-cross", own_revision)):
    stopped = run_main(dxil, answers)
    expect(stopped is not None and stopped.startswith("mrhi_dxil: "),
           f"mrhi_dxil.py refuses another {name} before reading its inputs")
expect(run_main(dxil, PINNED_ANSWERS) is None,
       "mrhi_dxil.py runs with the pinned tools, to fail on its missing inputs")
stopped = run_main(msl, own_revision)
expect(stopped is not None and stopped.startswith("mrhi_msl: "),
       "mrhi_msl.py refuses another spirv-cross")
expect(run_main(msl, other_dxc) is None, "mrhi_msl.py runs no DXC, so takes any")

printed = io.StringIO()
with contextlib.redirect_stdout(printed):
    status = load("mrhi_container").main(["--tools"])
lines = printed.getvalue().splitlines()
expect(status == 0 and lines == shader_tools.pin_lines(dxil.DXC_FLAGS),
       "mrhi_container.py --tools prints the pins")
stamped = set()
for header in glob.glob(os.path.join(ROOT, "test", "shaders", "*_container.h")):
    with open(header, encoding="utf-8") as f:
        stamped.update(line[3:].rstrip("\n") for line in f
                       if line.startswith("// tool spirv-cross") or line.startswith("// tool dxc"))
expect(stamped == set(lines), f"the committed headers stamp the same tools: {sorted(stamped)}")

print(f"test_shader_tools: {failures} failures")
sys.exit(1 if failures else 0)
