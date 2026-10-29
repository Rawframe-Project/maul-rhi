#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Checks that MRHI_SPI_VERSION rises with any change to what a driver
# sees (docs/operations/release.md): the declarations the installed SPI
# headers reach, digested. clang-20 parses src/driver.h for a fixed 64-bit
# Linux target; the digest covers each type the SPI headers declare and
# every type those reach in turn through the public headers: a typedef's
# type, a struct's members with their types, offsets, size and alignment
# (with each function pointer's signature), an enum's values (a sized
# enum's constants belong to the typedef they follow), the static inline
# functions of the SPI headers and the macros they define. The core
# functions the SPI headers declare are not part of the interface and are
# left out, as are public types no driver reaches.
#
# The digest is recorded in src/driver.h beside MRHI_SPI_VERSION. Against
# a base, the check refuses a recorded digest that is not the tree's, a
# version below the base's, and a digest that differs from the latest
# release's (the newest v* tag, by version) while the version is not
# above that release's; without such a tag, a digest that differs from
# the base's while the version is not above the base's. A version may
# rise with the digest unchanged: a change in meaning raises it by hand.
# --self-test plants each case in a scratch git repository and proves the
# check takes the right ones and refuses the others.
#
# usage: check_spi_version.py BASE | --write | --dump [REF] | --self-test

import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DRIVER = "src/driver.h"
SPI_HEADERS = ("src/driver.h", "src/command.h", "src/reflection.h", "src/container.h",
               "src/container_interface.h")
TARGET = "x86_64-unknown-linux-gnu"
VERSION = re.compile(r"^#define MRHI_SPI_VERSION (\d+)$", re.M)
RECORDED = re.compile(r"^// SPI digest: ([0-9a-f]{64})$", re.M)
DIGEST_LINES = ("// What a driver sees at this version, as tools/check_spi_version.py\n"
                "// digests it; --write records it.\n")
IDENTIFIER = re.compile(r"[A-Za-z_]\w*")
UNNAMED = re.compile(r"\((anonymous|unnamed)( \w+)? at [^)]*\)")


def git(root, *args):
    return subprocess.run(["git", "-C", root, *args], capture_output=True, text=True)


def clang(root, *args):
    """clang-20's output over src/driver.h in root, for the fixed target."""
    compiler = shutil.which("clang-20")
    if compiler is None:
        raise RuntimeError("clang-20 is not on PATH")
    run = subprocess.run([compiler, "-x", "c", "-std=c23", f"--target={TARGET}", "-Iinclude",
                          "-Isrc", *args, DRIVER], cwd=root, capture_output=True, text=True)
    if run.returncode != 0:
        raise RuntimeError(f"clang-20 failed on {DRIVER}:\n{run.stderr.strip()}")
    return run.stdout


def layouts(root):
    """Each named record's layout as clang prints it, by 'struct name'."""
    dump = clang(root, "-fsyntax-only", "-Xclang", "-fdump-record-layouts-complete")
    found = {}
    for block in dump.split("*** Dumping AST Record Layout")[1:]:
        lines = [line.rstrip() for line in block.strip().splitlines()]
        head = lines[0].split("|", 1)[1].strip()
        if UNNAMED.search(head) is None:
            found[head] = "\n".join(UNNAMED.sub(r"(\1)", line) for line in lines)
    return found


def macros(root):
    """The macros the SPI headers define, with their values, but for the
    include guards and the version itself."""
    values = {}
    for line in clang(root, "-E", "-dM").splitlines():
        parts = line.split(" ", 2)
        if len(parts) >= 2 and parts[0] == "#define":
            values[parts[1]] = parts[2] if len(parts) == 3 else ""
    found = {}
    for header in SPI_HEADERS:
        with open(os.path.join(root, header), encoding="utf-8") as f:
            for name in re.findall(r"^\s*#\s*define\s+(\w+)", f.read(), re.M):
                guard = name.startswith("MAUL_RHI_SRC_") and name.endswith("_H")
                if not guard and name != "MRHI_SPI_VERSION":
                    found[name] = f"#define {name} {values.get(name, '')}".rstrip()
    return found


class Files:
    """The file of each location clang's JSON dump prints, which names a
    file only where it differs from the location printed before."""

    def __init__(self):
        self.current = None

    def walk(self, node):
        if isinstance(node, dict):
            if "offset" in node and "file" in node:
                self.current = node["file"]
            for value in node.values():
                self.walk(value)
        elif isinstance(node, list):
            for value in node:
                self.walk(value)

    def of(self, decl):
        """The file decl is declared in; walks all of it."""
        location = decl.get("loc", {})
        self.walk(location.get("spellingLoc", {}))
        self.walk(location.get("expansionLoc", location))
        found = self.current
        self.walk({key: value for key, value in decl.items() if key != "loc"})
        return found


def evaluated(node):
    """The value clang evaluated for an initializer, under any casts."""
    if node.get("kind") == "ConstantExpr" and "value" in node:
        return int(node["value"])
    for inner in node.get("inner", []):
        value = evaluated(inner)
        if value is not None:
            return value
    return None


def constants(enum):
    """An enum's constants with their values, in order."""
    found, next_value = [], 0
    for constant in enum.get("inner", []):
        if constant.get("kind") != "EnumConstantDecl":
            continue
        value = evaluated(constant)
        value = next_value if value is None else value
        found.append((constant["name"], value))
        next_value = value + 1
    return found


def source(root, decl):
    """A declaration's source text with its whitespace collapsed."""
    begin, end = decl["range"]["begin"], decl["range"]["end"]
    with open(os.path.join(root, decl["file"]), encoding="utf-8") as f:
        text = f.read()[begin["offset"]:end["offset"] + end["tokLen"]]
    text = re.sub(r"//[^\n]*|/\*.*?\*/", " ", text, flags=re.S)
    return " ".join(text.split())


def top_level(root):
    """The top-level declarations in order, each with its file, from
    clang's JSON dump."""
    ast = json.loads(clang(root, "-fsyntax-only", "-Xclang", "-ast-dump=json"))
    files, found = Files(), []
    for decl in ast.get("inner", []):
        file = files.of(decl)
        if file is not None and not decl.get("isImplicit"):
            found.append(dict(decl, file=file))
    return found


def entry(text, file, refs=""):
    return {"text": text, "file": file, "refs": set(IDENTIFIER.findall(refs))}


def typedef_entry(decl, ours):
    kind = decl["type"]
    written, plain = kind["qualType"], kind.get("desugaredQualType", kind["qualType"])
    if UNNAMED.search(written):
        raise RuntimeError(f"{decl['name']} names an unnamed type, which the digest cannot "
                           "follow; name it")
    if not ours:
        return entry(f"typedef {decl['name']} = {plain}", decl["file"])
    return entry(f"typedef {decl['name']} = {written} [{plain}]", decl["file"],
                 written + " " + plain)


def declarations(root):
    """Every declaration a driver could reach, by kind and name: its text
    in the digest, its file and the names it refers to."""
    shapes, found, owner = layouts(root), {}, {}
    decls = top_level(root)
    for index, decl in enumerate(decls):
        kind, name, file = decl["kind"], decl.get("name"), decl["file"]
        ours = not os.path.isabs(file)
        if kind == "TypedefDecl":
            found[("type", name)] = typedef_entry(decl, ours)
        elif kind == "RecordDecl" and name and decl.get("completeDefinition"):
            shape = shapes[f"{decl['tagUsed']} {name}"]
            found[(decl["tagUsed"], name)] = entry(shape, file, shape if ours else "")
        elif kind == "EnumDecl":
            values = constants(decl)
            key = ("enum", name) if name else attached(decls, index)
            text = ", ".join(f"{constant} = {value}" for constant, value in values)
            if key in found:
                found[key]["text"] += f"; enum {{{text}}}"
            else:
                found[key] = entry(f"enum {name} {{{text}}}", file)
            for constant, _ in values:
                owner[constant] = key
        elif kind == "FunctionDecl" and file in SPI_HEADERS and decl.get("inline"):
            text = source(root, decl)
            found[("function", name)] = entry(text, file, text)
    return found, owner


def attached(decls, index):
    """The typedef an unnamed enum gives its values to: the one it
    declares, or else the one just before it, as a sized enum's are."""
    after = decls[index + 1] if index + 1 < len(decls) else {}
    if after.get("kind") == "TypedefDecl" and "enum" in after["type"]["qualType"]:
        return ("type", after["name"])
    before = decls[index - 1] if index > 0 else {}
    if before.get("kind") != "TypedefDecl":
        raise RuntimeError(f"an unnamed enum in {decls[index]['file']} follows no typedef")
    return ("type", before["name"])


def seen(root):
    """What a driver sees: the digest's lines, sorted."""
    found, owner = declarations(root)
    by_name = {}
    for key in found:
        by_name.setdefault(key[1], []).append(key)
    todo = [key for key, value in found.items() if value["file"] in SPI_HEADERS]
    reached = set()
    while todo:
        key = todo.pop()
        if key in reached:
            continue
        reached.add(key)
        for name in found[key]["refs"]:
            todo.extend(by_name.get(name, []))
            if name in owner:
                todo.append(owner[name])
    lines = [found[key]["text"] for key in reached] + list(macros(root).values())
    return sorted(lines)


def digest(root):
    return hashlib.sha256("\n\n".join(seen(root)).encode()).hexdigest()


def recorded(root):
    """The version and the digest src/driver.h records in root."""
    with open(os.path.join(root, DRIVER), encoding="utf-8") as f:
        text = f.read()
    version, written = VERSION.search(text), RECORDED.search(text)
    return (int(version.group(1)) if version else None,
            written.group(1) if written else None)


def at_commit(root, commit, work):
    """A commit's version and digest, from its headers extracted into
    work; None where the commit has no SPI version."""
    archive = subprocess.run(["git", "-C", root, "archive", commit, "include", "src"],
                             capture_output=True)
    if archive.returncode != 0:
        raise RuntimeError(f"git archive {commit}: {archive.stderr.decode().strip()}")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    subprocess.run(["tar", "-x", "-C", work], input=archive.stdout, check=True)
    if not os.path.exists(os.path.join(work, DRIVER)):
        return None
    version, _ = recorded(work)
    return None if version is None else (version, digest(work))


def released(root):
    """The newest v* tag, by version."""
    tags = git(root, "tag", "--list", "v[0-9]*", "--sort=-v:refname")
    return tags.stdout.split()[0] if tags.returncode == 0 and tags.stdout.split() else None


def problem(tree, base, release):
    """Why the tree's (version, digest) breaks the rule against the base's
    and the release's, each a (version, digest) pair or None (the release
    a (tag, pair)); None when it keeps it."""
    version, now = tree
    if base is not None and version < base[0]:
        return f"MRHI_SPI_VERSION went down from {base[0]} to {version}"
    if release is not None and release[1] is not None:
        tag, (shipped, then) = release
        if now != then and not version > shipped:
            return (f"what a driver sees changed since {tag}, but MRHI_SPI_VERSION is "
                    f"{version}, which {tag} shipped already: raise it to {shipped + 1}")
        return None
    if base is not None and now != base[1] and not version > base[0]:
        return (f"what a driver sees changed, but MRHI_SPI_VERSION stayed {version}, and no "
                f"release tag holds it: raise it to {base[0] + 1}")
    return None


def check(root, base, quiet=False):
    def say(message, error=True):
        if not quiet:
            print(f"SPI version: {message}", file=sys.stderr if error else sys.stdout)

    resolved = git(root, "rev-parse", "--verify", "--quiet", f"{base}^{{commit}}")
    if resolved.returncode != 0:
        say(f"{base} names no commit here; fetch it or fix the name")
        return 1
    try:
        version, written = recorded(root)
        now = digest(root)
        if version is None or written != now:
            say(f"{DRIVER} records digest {written}, but what a driver sees digests to "
                f"{now}: run tools/check_spi_version.py --write")
            return 1
        with tempfile.TemporaryDirectory() as work:
            before = at_commit(root, resolved.stdout.strip(), os.path.join(work, "base"))
            tag = released(root)
            shipped = at_commit(root, tag, os.path.join(work, "tag")) if tag else None
    except RuntimeError as failed:
        say(str(failed))
        return 1
    found = problem((version, now), before, (tag, shipped) if tag else None)
    if found is not None:
        say(found)
        return 1
    against = f" and {tag}" if tag else ""
    say(f"{version}, consistent with {base}{against}", error=False)
    return 0


def write(root):
    """Records the tree's digest in src/driver.h."""
    path = os.path.join(root, DRIVER)
    with open(path, encoding="utf-8") as f:
        text = f.read()
    line = f"// SPI digest: {digest(root)}"
    if RECORDED.search(text):
        text = RECORDED.sub(line, text)
    else:
        version = VERSION.search(text)
        if version is None:
            raise RuntimeError(f"{DRIVER} defines no MRHI_SPI_VERSION")
        text = text[:version.end()] + "\n" + DIGEST_LINES + line + text[version.end():]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(line)


def dump(root, ref):
    """Prints what a driver sees, at ref or in the tree."""
    if ref is None:
        print("\n\n".join(seen(root)))
        return
    with tempfile.TemporaryDirectory() as work:
        if at_commit(root, ref, work) is None:
            raise RuntimeError(f"{ref} has no {DRIVER} with an SPI version")
        print("\n\n".join(seen(work)))


# Each case: its name, the edits made to the tree (file, old, new, or a
# version edit), and whether the check takes it. The scratch repository
# holds the headers as a release, v0.9.0, with its digest recorded. Each
# change a driver sees is refused, and taken once the version is raised
# with it, so a refusal is the rule's and not a failed parse.
SELF_CASES = [
    ("unchanged", [], True),
    ("a comment changed", [("src/driver.h", "// The SPI version a driver",
                            "// The SPI version that a driver")], True),
    ("a sized enum's value renumbered",
     [("include/maul-rhi/surface.h", "mrhi_presentFifo = 0,", "mrhi_presentFifo = 7,")], False),
    ("a member widened", [("include/maul-rhi/instance.h", "uint32_t vendorId;",
                           "uint64_t vendorId;")], False),
    ("a member renamed", [("include/maul-rhi/instance.h", "uint32_t vendorId;",
                           "uint32_t vendor;")], False),
    ("a function pointer given an argument",
     [("src/driver.h", "void (*lossReport)(void* self, mrhiDeviceLossReport* reportOut);",
       "void (*lossReport)(void* self, mrhiDeviceLossReport* reportOut, int extra);")], False),
    ("an SPI macro changed", [("src/container.h", "#define MRHI_METAL_NONE 255",
                               "#define MRHI_METAL_NONE 254")], False),
    ("an SPI inline function changed",
     [("src/driver.h", "MRHI_STATISTICS_COUNTERS : 1u)", "MRHI_STATISTICS_COUNTERS : 2u)")],
     False),
    ("a type no driver reaches changed",
     [("include/maul-rhi/frame.h", "uint32_t frameIndirectDraws;",
       "uint64_t frameIndirectDraws;")], True),
    ("only the version raised", ["raise"], True),
]


def edit(work, change, version):
    """Applies one edit of a case; a version edit rewrites the version."""
    if change in ("raise", "lower"):
        path, old = os.path.join(work, DRIVER), f"#define MRHI_SPI_VERSION {version}"
        new = f"#define MRHI_SPI_VERSION {version + (1 if change == 'raise' else -1)}"
    else:
        name, old, new = change
        path = os.path.join(work, name)
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if text.count(old) != 1:
        raise RuntimeError(f"self-test: the edit of {path} no longer applies: {old}")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text.replace(old, new))


def self_case(work, run, version, case, base="v0.9.0"):
    """Runs one case over the base; True when the check decided right."""
    name, changes, takes = case
    run("checkout", "-q", base, "--", ".")
    for change in changes:
        edit(work, change, version)
    write_quietly(work)
    took = check(work, base, quiet=True) == 0
    if took != takes:
        print(f"self-test: {name}: {'taken' if took else 'refused'}, expected "
              f"{'taken' if takes else 'refused'}", file=sys.stderr)
    return took == takes


def all_cases():
    """The cases, each refused one beside its twin with the version
    raised, and the version lowered."""
    cases = []
    for name, changes, takes in SELF_CASES:
        cases.append((name, changes, takes))
        if not takes:
            cases.append((f"{name}, the version raised", [*changes, "raise"], True))
    cases.append(("the version lowered", ["lower"], False))
    return cases


def write_quietly(root):
    with open(os.devnull, "w", encoding="utf-8") as sink:
        stdout, sys.stdout = sys.stdout, sink
        try:
            write(root)
        finally:
            sys.stdout = stdout


def self_test():
    with tempfile.TemporaryDirectory() as work:
        def run(*args):
            subprocess.run(["git", "-C", work, *args], check=True, capture_output=True)

        shutil.copytree(os.path.join(ROOT, "include"), os.path.join(work, "include"))
        os.makedirs(os.path.join(work, "src"))
        for header in SPI_HEADERS:
            shutil.copy(os.path.join(ROOT, header), os.path.join(work, header))
        write_quietly(work)
        version, _ = recorded(work)
        run("init", "-q")
        run("add", ".")
        run("-c", "user.name=self-test", "-c", "user.email=self-test@invalid", "commit", "-qm",
            "release")
        run("tag", "v0.9.0")
        cases = all_cases()
        right = sum(self_case(work, run, version, case) for case in cases)
        run("checkout", "-q", "v0.9.0", "--", ".")
        edit(work, ("src/driver.h", "// SPI digest: ", "// SPI digest: 0"), version)
        stale = check(work, "v0.9.0", quiet=True) != 0
        unknown = check(work, "no-such-commit", quiet=True) != 0
        # Without a release tag the base stands in for it.
        run("tag", "base", "v0.9.0")
        run("tag", "-d", "v0.9.0")
        name, changes, _ = SELF_CASES[2]
        untagged = [(f"without a release, {name}", changes, False),
                    (f"without a release, {name}, the version raised", [*changes, "raise"],
                     True)]
        right += sum(self_case(work, run, version, case, "base") for case in untagged)
    total = len(cases) + 2 + len(untagged)
    right += stale + unknown
    if not stale:
        print("self-test: a stale recorded digest was taken", file=sys.stderr)
    if not unknown:
        print("self-test: a base that names no commit was taken", file=sys.stderr)
    print(f"self-test: {right} of {total} cases right")
    return 0 if right == total else 1


def main():
    args = sys.argv[1:]
    try:
        if args == ["--self-test"]:
            return self_test()
        if args == ["--write"]:
            write(ROOT)
            return 0
        if args[:1] == ["--dump"] and len(args) <= 2:
            dump(ROOT, args[1] if len(args) == 2 else None)
            return 0
    except RuntimeError as failed:
        print(f"SPI version: {failed}", file=sys.stderr)
        return 1
    if len(args) != 1 or args[0].startswith("-"):
        sys.exit("usage: check_spi_version.py BASE | --write | --dump [REF] | --self-test")
    return check(ROOT, args[0])


if __name__ == "__main__":
    sys.exit(main())
