#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Checks that contract_version rises once per release with the changes to
# contract/mrhi.json, and with nothing else (docs/internals/contract.md,
# Versions): against a base commit, the contract in the working tree
# must either be the same, version included, or differ beyond its
# version and carry one above the latest release's, never below the
# base's. The latest release is the newest v* tag, by version; its
# contract's version is the release's. Without such a tag, or one
# holding the contract, the version must rise above the base's.
# A base that names no commit is refused,
# so a typo or an unfetched commit cannot pass, and so is one whose tree
# git cannot read; a commit without the contract checks nothing.
# --self-test plants each case in a scratch git repository and proves the
# check takes the right ones and refuses the others.
#
# usage: check_contract_version.py BASE | --self-test

import copy
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTRACT = "contract/mrhi.json"


def git(root, *args):
    return subprocess.run(["git", "-C", root, *args], capture_output=True, text=True)


def at_base(root, commit):
    """The contract at a commit, parsed; None where the commit has none."""
    listed = git(root, "ls-tree", "--name-only", commit, "--", CONTRACT)
    if listed.returncode != 0:
        raise RuntimeError(f"git ls-tree {commit}: {listed.stderr.strip()}")
    if not listed.stdout.strip():
        return None
    shown = git(root, "show", f"{commit}:{CONTRACT}")
    if shown.returncode != 0:
        raise RuntimeError(f"git show {commit}:{CONTRACT}: {shown.stderr.strip()}")
    return json.loads(shown.stdout)


def without_version(contract):
    rest = dict(contract)
    rest.pop("contract_version", None)
    return rest


def released(root):
    """The newest v* tag and its contract's version; None without a tag
    that holds the contract."""
    tags = git(root, "tag", "--list", "v[0-9]*", "--sort=-v:refname")
    if tags.returncode != 0 or not tags.stdout.split():
        return None
    tag = tags.stdout.split()[0]
    contract = at_base(root, tag)
    if contract is None or not isinstance(contract.get("contract_version"), int):
        return None
    return tag, contract["contract_version"]


def problem(before, after, release=None):
    """Why the contract after breaks the rule against the one before and
    the release, a (tag, version) pair or None; None when it keeps it."""
    old = before.get("contract_version")
    new = after.get("contract_version")
    changed = without_version(before) != without_version(after)
    if changed and not (isinstance(new, int) and isinstance(old, int)):
        return f"{CONTRACT} changed, but contract_version is {new}, not a number"
    if changed and release is not None and not new > release[1]:
        return (f"{CONTRACT} changed, but contract_version is {new}, which {release[0]} "
                f"shipped already: raise it to {release[1] + 1}")
    if changed and release is not None and new < old:
        return f"{CONTRACT} changed, but contract_version went down from {old} to {new}"
    if changed and release is None and not new > old:
        return (f"{CONTRACT} changed, but contract_version went from {old} to {new}, and "
                f"no release tag holds the contract: raise it to {old + 1}")
    if not changed and new != old:
        return (f"contract_version went from {old} to {new}, but nothing else in "
                f"{CONTRACT} changed")
    return None


def check(root, base, quiet=False):
    resolved = git(root, "rev-parse", "--verify", "--quiet", f"{base}^{{commit}}")
    if resolved.returncode != 0:
        if not quiet:
            print(f"contract version: {base} names no commit here; fetch it or fix the name",
                  file=sys.stderr)
        return 1
    try:
        before = at_base(root, resolved.stdout.strip())
    except RuntimeError as failed:
        if not quiet:
            print(f"contract version: {failed}", file=sys.stderr)
        return 1
    if before is None:
        if not quiet:
            print(f"contract version: {base} has no {CONTRACT}; nothing to compare")
        return 0
    with open(os.path.join(root, CONTRACT), encoding="utf-8") as f:
        after = json.load(f)
    try:
        release = released(root)
    except RuntimeError as failed:
        if not quiet:
            print(f"contract version: {failed}", file=sys.stderr)
        return 1
    found = problem(before, after, release)
    if found is not None:
        if not quiet:
            print(f"contract version: {found}", file=sys.stderr)
        return 1
    if not quiet:
        against = f" and {release[0]}" if release is not None else ""
        print(f"contract version: {after['contract_version']}, consistent with {base}{against}")
    return 0


# Each release case: its name, the base, the contract version written
# over it with another edit, and whether the check takes it. The scratch
# repository holds v0.9.0 at the base's version, then "main" one above it
# with an edit, then "ahead" two above it with another.
RELEASE_CASES = [
    ("a change while the base is above the release", "main", 1, True),
    ("a change while the base equals the release", "v0.9.0", 0, False),
    ("a change raised past the release", "v0.9.0", 1, True),
    ("a change lowered below the base", "ahead", 1, False),
    ("no change while the base is above the release", "main", None, True),
]


def release_cases(work, variant, edited, run, identity):
    """Plants the release cases' history, runs them, and returns how many
    went wrong. Tags compare by version: v0.10.0, on main, is newer than
    v0.9.0."""
    path = os.path.join(work, CONTRACT)

    def commit(contract, message):
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(json.dumps(contract, indent=2) + "\n")
        run("add", CONTRACT)
        run(*identity, "commit", "-q", "--allow-empty", "-m", message)

    commit(variant(), "the release")
    run("tag", "v0.9.0")
    commit(variant(edited + " Main.", 1), "main")
    run("branch", "main-base")
    commit(variant(edited + " Ahead.", 2), "ahead")
    run("branch", "ahead-base")
    refs = {"main": "main-base", "ahead": "ahead-base", "v0.9.0": "v0.9.0"}
    failures = 0
    for name, base, raise_by, takes in RELEASE_CASES:
        if raise_by is None:
            contract = json.loads(subprocess.run(
                ["git", "-C", work, "show", f"{refs[base]}:{CONTRACT}"], check=True,
                capture_output=True, text=True).stdout)
        else:
            contract = variant(edited + " Again.", raise_by)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(json.dumps(contract, indent=2) + "\n")
        took = check(work, refs[base], quiet=True) == 0
        if took != takes:
            print(f"self-test: {name}: {'taken' if took else 'refused'}, expected "
                  f"{'taken' if takes else 'refused'}", file=sys.stderr)
            failures += 1
    # v0.10.0 on main makes main's version the release's: keeping it fails,
    # where v0.9.0, newer by name, would let it pass.
    run("tag", "v0.10.0", "main-base")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(variant(edited + " Again.", 1), indent=2) + "\n")
    if check(work, "main-base", quiet=True) == 0:
        print("self-test: the newest tag by version was not the release", file=sys.stderr)
        failures += 1
    run("tag", "-d", "v0.9.0")
    run("tag", "-d", "v0.10.0")
    run("checkout", "-q", "HEAD", "--", CONTRACT)
    return failures


def self_test():
    with open(os.path.join(ROOT, CONTRACT), encoding="utf-8") as f:
        base = json.load(f)
    version = base["contract_version"]

    def variant(doc=None, raise_by=0):
        made = copy.deepcopy(base)
        if doc is not None:
            made["headers"][0]["doc"] = doc
        made["contract_version"] = version + raise_by
        return made

    edited = base["headers"][0].get("doc", "") + " Edited."
    # Each case: its name, the contract written over the base, and whether
    # the check takes it.
    cases = [
        ("unchanged", variant(), True),
        ("changed and raised", variant(edited, 1), True),
        ("changed and raised by two", variant(edited, 2), True),
        ("changed, the version kept", variant(edited), False),
        ("changed, the version lowered", variant(edited, -1), False),
        ("only the version raised", variant(raise_by=1), False),
        ("only the version lowered", variant(raise_by=-1), False),
    ]
    failures = 0
    with tempfile.TemporaryDirectory() as work:
        os.makedirs(os.path.join(work, "contract"))
        path = os.path.join(work, CONTRACT)

        def run(*args):
            subprocess.run(["git", "-C", work, *args], check=True, capture_output=True)

        run("init", "-q")
        identity = ("-c", "user.name=self-test", "-c", "user.email=self-test@invalid")
        run(*identity, "commit", "-q", "--allow-empty", "-m", "before the contract")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(json.dumps(base, indent=2) + "\n")
        run("add", CONTRACT)
        run(*identity, "commit", "-qm", "base")
        for name, contract, takes in cases:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(json.dumps(contract, indent=2) + "\n")
            took = check(work, "HEAD", quiet=True) == 0
            if took != takes:
                print(f"self-test: {name}: {'taken' if took else 'refused'}, expected "
                      f"{'taken' if takes else 'refused'}", file=sys.stderr)
                failures += 1
        if check(work, "HEAD~1", quiet=True) != 0:
            print("self-test: a base without the contract was refused", file=sys.stderr)
            failures += 1
        if check(work, "no-such-commit", quiet=True) == 0:
            print("self-test: a base that names no commit was taken", file=sys.stderr)
            failures += 1
        failures += release_cases(work, variant, edited, run, identity)
        # A commit whose tree is missing, as in a treeless partial clone
        # without network: git can resolve the commit but not list it.
        run(*identity, "commit", "-q", "--allow-empty", "-m", "its tree goes missing")
        tree = subprocess.run(["git", "-C", work, "rev-parse", "HEAD^{tree}"], check=True,
                              capture_output=True, text=True).stdout.strip()
        os.remove(os.path.join(work, ".git", "objects", tree[:2], tree[2:]))
        if check(work, "HEAD", quiet=True) == 0:
            print("self-test: a base whose tree cannot be read was taken", file=sys.stderr)
            failures += 1
    total = len(cases) + 4 + len(RELEASE_CASES)
    print(f"self-test: {total - failures} of {total} cases right")
    return 1 if failures else 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    if len(sys.argv) != 2 or sys.argv[1].startswith("-"):
        sys.exit("usage: check_contract_version.py BASE | --self-test")
    return check(ROOT, sys.argv[1])


if __name__ == "__main__":
    sys.exit(main())
