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

PROGRAM_UNDER_TEST = os.path.join(
    os.path.dirname(__file__), "..", "out", "example_symbolize")


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
    found_symbols = set()
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
        address = match_obj.group(1)
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
        function_symbol = match_obj.group(2)
        found_symbols.add(function_symbol)
    return len(found_symbols) >= 8  # main, f1, f2, ..., f7


def run():
    """
    Returns:
    bool: True on success
    """
    if not os.path.isfile(PROGRAM_UNDER_TEST):
        testing_utils.print_error("program not built: %s, did you run 'make'?" %
                                  PROGRAM_UNDER_TEST)
        return False
    try:
        output = subprocess.check_output([PROGRAM_UNDER_TEST])
    except subprocess.CalledProcessError as e:
        testing_utils.print_error("subprocess error: %s" % str(e))
        return False
    return validate_output(testing_utils.ensure_str(output))


if __name__ == "__main__":
    sys.exit(testing_utils.report(run()))
