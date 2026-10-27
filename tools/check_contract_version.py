#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Checks that contract_version rises with every change to
# contract/mrhi.json and with nothing else (docs/internals/contract.md,
# Versions): against a base commit, the contract in the working tree
# must either be the same, version included, or differ beyond its
# version and carry a higher one. A base that names no commit is refused,
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


def problem(before, after):
    """Why the contract after breaks the rule against the one before; None
    when it keeps it."""
    old = before.get("contract_version")
    new = after.get("contract_version")
    changed = without_version(before) != without_version(after)
    if changed and not (isinstance(new, int) and isinstance(old, int) and new > old):
        return (f"{CONTRACT} changed, but contract_version went from {old} to {new}: "
                f"raise it to {old + 1 if isinstance(old, int) else 'a higher number'}")
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
    found = problem(before, after)
    if found is not None:
        if not quiet:
            print(f"contract version: {found}", file=sys.stderr)
        return 1
    if not quiet:
        print(f"contract version: {after['contract_version']}, consistent with {base}")
    return 0


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
        # A commit whose tree is missing, as in a treeless partial clone
        # without network: git can resolve the commit but not list it.
        run(*identity, "commit", "-q", "--allow-empty", "-m", "its tree goes missing")
        tree = subprocess.run(["git", "-C", work, "rev-parse", "HEAD^{tree}"], check=True,
                              capture_output=True, text=True).stdout.strip()
        os.remove(os.path.join(work, ".git", "objects", tree[:2], tree[2:]))
        if check(work, "HEAD", quiet=True) == 0:
            print("self-test: a base whose tree cannot be read was taken", file=sys.stderr)
            failures += 1
    print(f"self-test: {len(cases) + 3 - failures} of {len(cases) + 3} cases right")
    return 1 if failures else 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    if len(sys.argv) != 2 or sys.argv[1].startswith("-"):
        sys.exit("usage: check_contract_version.py BASE | --self-test")
    return check(ROOT, sys.argv[1])


if __name__ == "__main__":
    sys.exit(main())
