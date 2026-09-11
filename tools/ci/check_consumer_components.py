#!/usr/bin/env python3
"""Check which components the ESP-IDF consumer was built with.

    python3 tools/ci/check_consumer_components.py BUILD_DIR

Reads BUILD_DIR/project_description.json, which ESP-IDF writes for every build.
Fails unless fmsui and lvgl are among the build's components, and unless none
of fmsui_fonts, the Tab5 BSP or esp_lvgl_port is.  The consumer exists to show
that the framework builds without those, and a component that arrived through
some indirect requirement would not show up anywhere else.

Prints a Markdown report on stdout, for the job summary.
"""

import json
import pathlib
import sys

REQUIRED = ("fmsui", "lvgl")

# Matched against the end of a component name: managed components are called
# namespace__name, e.g. espressif__m5stack_tab5.
FORBIDDEN = ("fmsui_fonts", "m5stack_tab5", "esp_lvgl_port")


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    build = pathlib.Path(sys.argv[1])
    description = json.loads((build / "project_description.json").read_text())
    components = sorted(description["build_components"])
    info = description.get("build_component_info", {})

    failed = False
    lines = ["| Check | Result |", "|---|---|"]

    for name in REQUIRED:
        if name in components:
            where = info.get(name, {}).get("dir", "?")
            lines.append(f"| `{name}` is built | yes, from `{where}` |")
        else:
            lines.append(f"| `{name}` is built | **no** |")
            failed = True

    for name in FORBIDDEN:
        hits = [c for c in components if c == name or c.endswith("__" + name)]
        if hits:
            lines.append(f"| `{name}` is not built | **built as {', '.join(hits)}** |")
            failed = True
        else:
            lines.append(f"| `{name}` is not built | not built |")

    managed = [c for c in components if "__" in c]
    lines.append(
        "| managed components | "
        + (", ".join(f"`{c}`" for c in managed) if managed else "none")
        + " |"
    )
    lines.append("")
    lines.append(f"{len(components)} components in the build.")

    print("\n".join(lines))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
