#!/usr/bin/env python3
# Copyright (c) 2020 Leedehai. All rights reserved.
# Use of this source code is governed under the LICENSE.txt file.
# -----
# Test symbolize.cc.
# How to test: see README.md.

import os, sys
import subprocess
import re
# My own package
import testing_utils

PROGRAMS_UNDER_TEST = [
    os.path.relpath(os.path.join(os.path.dirname(__file__), "..", "out", e))
    for e in [
        "example_symbolize",  # symbolizer as an object
        "example_symbolize_with_so",  # symbolizer as a shared library
    ]
]


def find_overlapped_symbol(mangled_symbol, function_names):
    for name in function_names:
        if name in mangled_symbol:
            return name
    return None


def validate_output(output):
    """
    Params:
    output: str

    Returns:
    bool: True on success
    """
    line_pattern = re.compile(r"\[(\d\d)\] (0x[0-9A-Fa-f]{16,16}) (.+)")
    trace_lines = [e for e in output.split('\n') if len(e)]
    found_addresses = {}  # key: address (hex string), val: stack index
    remaining_expected_symbols = [
        "main", "f1", "f2", "f3", "f4", "f5", "f6", "f7"
    ]
    for i, line in enumerate(trace_lines):
        match_obj = line_pattern.match(line)
        if not match_obj:
            testing_utils.print_error("line mismatch: %s" % line)
            return False
        # Index
        actual_index = int(match_obj.group(1))
        expected_index = len(trace_lines) - 1 - i
        if expected_index != actual_index:
            testing_utils.print_error("index: expected %d, found %d" %
                                      (expected_index, actual_index))
            return False
        # Address
        # NOTE addresses may not be decreasing because some functions
        # are system-provided.
        address = match_obj.group(2)
        if not address.startswith("0x"):
            testing_utils.print_error("not an address hexadecimal string: %s" %
                                      address)
        if address in found_addresses:
            testing_utils.print_error(
                "address string %s at position %d already appeared at a higher stack position %d"
                % (address, actual_index, found_addresses[address]))
            return False
        found_addresses[address] = actual_index
        # Function name
        # NOTE the function names are not guaranteed to be unique or
        # even non-empty (e.g. on macOS, the very first call before
        # our main() is not given a name).
        function_symbol = match_obj.group(3)
        overlapped_symbol = find_overlapped_symbol(function_symbol,
                                                   remaining_expected_symbols)
        if overlapped_symbol:
            remaining_expected_symbols.remove(overlapped_symbol)

    if len(remaining_expected_symbols):
        testing_utils.print_error(
            "these symbols are not found in the stack trace: %s" %
            ", ".join(remaining_expected_symbols))
        return False
    return True


def run_one(program):
    """
    Returns:
    bool: True on success
    """
    if not os.path.isfile(program):
        testing_utils.print_error("program not built: %s, did you run 'make'?" %
                                  program)
        return False
    try:
        print("run: %s" % program)
        output = subprocess.check_output([program])
    except subprocess.CalledProcessError as e:
        testing_utils.print_error("subprocess error: %s" % str(e))
        return False
    except OSError as e:
        testing_utils.print_error(str(e))
        return False
    return validate_output(testing_utils.ensure_str(output))


def run():
    """
    Returns:
    bool: True on success
    """
    all_ok = True
    for e in PROGRAMS_UNDER_TEST:
        if False == run_one(e):
            all_ok = False
    return all_ok


if __name__ == "__main__":
    sys.exit(testing_utils.report(run()))
