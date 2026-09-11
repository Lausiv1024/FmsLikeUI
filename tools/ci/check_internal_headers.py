#!/usr/bin/env python3
"""Check that the library's internal headers are out of the host consumer's reach.

    python3 tools/ci/check_internal_headers.py TREE BUILD_DIR

TREE is the tree stage_consumer.sh made, and BUILD_DIR the consumers/host build
configured from it.  Fails unless all of these hold:

1. components/fmsui/include/ has no fmsui/arena.h or fmsui/element.h, and
   components/fmsui/src/internal/ has both.
2. Every fmsui source is compiled with src/internal on its include path.  This
   is what makes the next check look for something that is really there.
3. No consumer translation unit -- its sources, the generated header checks,
   the probes -- has src/internal on its include path, and none of the include
   directories it does have holds either header.
4. Building each probe target, a translation unit that includes one of the
   headers and is linked the way the consumer is, fails -- and fails because
   the header was not found, not for some other reason.

Prints what it found as a Markdown table.
"""

import pathlib
import re
import subprocess
import sys

from compile_commands import include_dirs, load, source, under

INTERNAL = ("arena", "element")


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    tree = pathlib.Path(sys.argv[1]).resolve()
    build = pathlib.Path(sys.argv[2]).resolve()
    fmsui = tree / "components" / "fmsui"
    public = fmsui / "include"
    internal = fmsui / "src" / "internal"

    failed = False
    lines = ["| Check | Result |", "|---|---|"]

    def row(check: str, ok: bool, detail: str) -> None:
        nonlocal failed
        failed |= not ok
        lines.append(f"| {check} | {detail if ok else '**' + detail + '**'} |")

    # 1. Where the headers are.
    exposed = [h for h in INTERNAL if (public / "fmsui" / f"{h}.h").exists()]
    row("`include/` has no internal header", not exposed,
        "none" if not exposed else "has " + ", ".join(f"fmsui/{h}.h" for h in exposed))
    missing = [h for h in INTERNAL if not (internal / "fmsui" / f"{h}.h").is_file()]
    row("`src/internal/` has both", not missing,
        "yes" if not missing else "missing " + ", ".join(f"fmsui/{h}.h" for h in missing))

    entries = load(build)
    library = [e for e in entries if under(source(e), fmsui / "src")]
    consumer = [
        e for e in entries
        if any(under(source(e), root) for root in
               (tree / "consumers", build / "header_check", build / "internal_header_probe"))
    ]

    # 2. The library is built with them.
    without = [source(e).name for e in library if internal not in include_dirs(e)]
    row(f"fmsui's {len(library)} sources have `src/internal`", bool(library) and not without,
        "all" if library and not without else "not: " + (", ".join(without) or "no sources found"))

    # 3. The consumer is not.
    leaked = [source(e).name for e in consumer if internal in include_dirs(e)]
    row(f"the consumer's {len(consumer)} translation units do not", bool(consumer) and not leaked,
        "none has it" if consumer and not leaked
        else "has it: " + (", ".join(leaked) or "no translation units found"))

    reachable = sorted({
        f"{d}/fmsui/{h}.h"
        for e in consumer for d in include_dirs(e) for h in INTERNAL
        if (d / "fmsui" / f"{h}.h").is_file()
    })
    row("no consumer include directory holds either header", not reachable,
        "none" if not reachable else ", ".join(reachable))

    # 4. And including one does not compile.
    for h in INTERNAL:
        target = f"fmsui_internal_probe_{h}"
        result = subprocess.run(["cmake", "--build", str(build), "--target", target],
                                capture_output=True, text=True)
        output = result.stdout + result.stderr
        not_found = re.search(
            rf"fmsui/{h}\.h: No such file or directory|'fmsui/{h}\.h' file not found", output)
        if result.returncode == 0:
            detail = "it compiled"
        elif not_found:
            detail = f"fails: {not_found.group(0)}"
        else:
            detail = "fails, but not because the header is missing"
        row(f"`#include <fmsui/{h}.h>` in the consumer", result.returncode != 0 and bool(not_found),
            detail)
        if result.returncode == 0 or not not_found:
            print(output, file=sys.stderr)

    print("\n".join(lines))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
