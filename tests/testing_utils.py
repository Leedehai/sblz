# Copyright (c) 2020 Leedehai. All rights reserved.
# Use of this source code is governed under the LICENSE.txt file.

import sys


def print_error(s):
    sys.stderr.write("%s\n" % s)


# s may be str or bytes
def ensure_str(s):
    if type(s) == str:
        return s
    return s.decode()


def report(ok):
    assert (type(ok) == bool)
    if ok:
        message = "\x1b[32mOk.\x1b[0m"
    else:
        message = "\x1b[31mError.\x1b[0m"
    print(message)
    return 0 if ok else 1
