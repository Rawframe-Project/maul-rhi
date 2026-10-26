#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Checks that contract_version rises with every change to
# contract/mrhi.json and with nothing else (docs/internals/contract.md,
# Versions): against a base commit, the contract in the working tree
# must either be the same, version included, or differ beyond its
# version and carry a higher one. A base without the contract checks
# nothing. --self-test plants each case in a scratch git repository and
# proves the check takes the right ones and refuses the others.
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


def at_base(root, base):
    """The contract at the base commit, parsed; None where it has none."""
    shown = subprocess.run(["git", "-C", root, "show", f"{base}:{CONTRACT}"],
                           capture_output=True, text=True)
    return json.loads(shown.stdout) if shown.returncode == 0 else None


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
    before = at_base(root, base)
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

        def git(*args):
            subprocess.run(["git", "-C", work, *args], check=True, capture_output=True)

        git("init", "-q")
        identity = ("-c", "user.name=self-test", "-c", "user.email=self-test@invalid")
        git(*identity, "commit", "-q", "--allow-empty", "-m", "before the contract")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(json.dumps(base, indent=2) + "\n")
        git("add", CONTRACT)
        git(*identity, "commit", "-qm", "base")
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
    print(f"self-test: {len(cases) + 1 - failures} of {len(cases) + 1} cases right")
    return 1 if failures else 0


def main():
    if sys.argv[1:] == ["--self-test"]:
        return self_test()
    if len(sys.argv) != 2 or sys.argv[1].startswith("-"):
        sys.exit("usage: check_contract_version.py BASE | --self-test")
    return check(ROOT, sys.argv[1])


if __name__ == "__main__":
    sys.exit(main())
