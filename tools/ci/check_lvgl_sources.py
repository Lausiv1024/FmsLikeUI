#!/usr/bin/env python3
"""Fail if a device build compiled LVGL's examples or demos.

    python3 tools/ci/check_lvgl_sources.py build-ci/compile_commands.json

Counts the build's compile commands by where each source lives.  Nothing in this
project uses lvgl/examples or lvgl/demos, and sdkconfig.defaults turns both off;
this is what notices if that setting stops taking effect.  The table goes to
stdout, and also to $GITHUB_STEP_SUMMARY when that is set.
"""

import json
import os
import pathlib
import sys

# First match wins, so the LVGL subdirectories come before LVGL as a whole.
CATEGORIES = (
    ("third_party/lvgl/examples/", "LVGL examples"),
    ("third_party/lvgl/demos/", "LVGL demos"),
    ("third_party/lvgl/", "LVGL (everything else)"),
    ("components/fmsui/", "fmsui"),
    ("components/fmsui_fonts/", "fmsui_fonts"),
    ("main/", "main"),
    ("demo/", "demo"),
)
MUST_BE_ZERO = ("LVGL examples", "LVGL demos")
MUST_BE_PRESENT = ("fmsui", "main", "demo")


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    repo = os.path.normcase(str(pathlib.Path(__file__).resolve().parents[2])) + os.sep
    commands = json.loads(pathlib.Path(sys.argv[1]).read_text())

    counts = {label: 0 for _, label in CATEGORIES}
    other = 0
    for entry in commands:
        path = os.path.normcase(os.path.normpath(entry["file"]))
        rel = path[len(repo):].replace("\\", "/") if path.startswith(repo) else None
        label = next((l for prefix, l in CATEGORIES if rel and rel.startswith(prefix)), None)
        if label is None:
            other += 1
        else:
            counts[label] += 1

    problems = [f"{label}: {counts[label]} sources, expected 0" for label in MUST_BE_ZERO
                if counts[label] != 0]
    problems += [f"{label}: no sources -- is this the compile database of this project?"
                 for label in MUST_BE_PRESENT if counts[label] == 0]

    lines = ["| Sources | Compile commands |", "|---|---:|"]
    lines += [f"| {label} | {counts[label]} |" for _, label in CATEGORIES]
    lines += [f"| Outside the project (ESP-IDF, managed components) | {other} |",
              f"| **Total** | **{len(commands)}** |"]
    verdict = ("LVGL examples / demos: FAIL -- " + "; ".join(problems) if problems
               else "LVGL examples / demos: 0 / 0 compiled")
    report = "\n".join(["### Compiled sources", "", *lines, "", verdict, ""])

    print(report)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as f:
            f.write(report + "\n")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
