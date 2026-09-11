#!/usr/bin/env python3
"""Check that the framework itself, not only the executables, is sanitized.

    python3 tools/ci/check_sanitized.py build-asan

Reads the build's compile_commands.json and fails unless every source under
components/fmsui/src was compiled with ASan/UBSan and frame pointers.  Tests
still pass when only the test executables are instrumented -- that mistake has
been made here once, and nothing but the compile lines shows it.
"""

import json
import pathlib
import shlex
import sys

REQUIRED = ("-fsanitize=address,undefined", "-fno-omit-frame-pointer")
SRC_DIR = "components/fmsui/src/"


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    build = pathlib.Path(sys.argv[1])
    repo = pathlib.Path(__file__).resolve().parents[2]
    expected = sorted(p.name for p in (repo / SRC_DIR).glob("*.cpp"))

    commands = json.loads((build / "compile_commands.json").read_text())
    found = {}
    for entry in commands:
        path = entry["file"].replace("\\", "/")
        if SRC_DIR not in path:
            continue
        args = entry.get("arguments") or shlex.split(entry["command"])
        found[path.rsplit("/", 1)[1]] = [flag for flag in REQUIRED if flag not in args]

    failed = False
    for name in expected:
        if name not in found:
            print(f"FAIL {SRC_DIR}{name}: not in compile_commands.json")
            failed = True
        elif found[name]:
            print(f"FAIL {SRC_DIR}{name}: missing {' '.join(found[name])}")
            failed = True
        else:
            print(f"ok   {SRC_DIR}{name}")
    for name in sorted(set(found) - set(expected)):
        print(f"FAIL {SRC_DIR}{name}: compiled, but no such file in the source tree")
        failed = True

    print(f"{len(expected)} fmsui sources, {'NOT all' if failed else 'all'} instrumented "
          f"with {' '.join(REQUIRED)}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
