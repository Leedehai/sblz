#!/usr/bin/env python3
# Copyright (c) 2020 Leedehai. All rights reserved.
# Use of this source code is governed under the LICENSE.txt file.
# -----
# Test demangler.cc.
# How to test: see README.md.

import os, sys
import subprocess
# My own package
import testing_utils

THIS_DIR = os.path.dirname(__file__)

PROGRAM_UNDER_TEST = os.path.relpath(
    os.path.join(THIS_DIR, "..", "out", "example_demangle"))

# The demangler just implements part of the demangling procedure: it doesn't
# extract the argument types.
MANGLED_SYMBOLS_MAP = {
    "_Z2f2f8MyStruct": "f2()",
    "_Z2f3iiiiid": "f3()",
}
with open(os.path.join(THIS_DIR, "demangler_cases.txt"), 'r') as f:
    line_iter = (l.strip() for l in f.readlines())
    for line in line_iter:
        if len(line) == 0 or line.startswith('#'):
            continue
        (mangled, demangled) = line.split(" | ")
        MANGLED_SYMBOLS_MAP[mangled] = demangled


def run_one(demangled: str) -> str:
    try:
        out = subprocess.check_output([PROGRAM_UNDER_TEST, demangled])
    except subprocess.CalledProcessError:
        out = "(exit 1)"
    return testing_utils.ensure_str(out).rstrip('\n')


def run() -> bool:
    """
    Returns:
    bool: True on success
    """
    all_ok = True
    for (mangled, expected_demangled) in MANGLED_SYMBOLS_MAP.items():
        actual_demangled = run_one(mangled)
        if actual_demangled != expected_demangled:
            testing_utils.print_error(
                "in: %s, expected out: %s, actual out: %s" %
                (mangled, expected_demangled, actual_demangled))
            all_ok = False
    return all_ok


if __name__ == "__main__":
    sys.exit(testing_utils.report(run()))
