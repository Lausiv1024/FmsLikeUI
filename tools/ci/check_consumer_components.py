#!/usr/bin/env python3
"""Check which components the ESP-IDF consumer was built with.

    python3 tools/ci/check_consumer_components.py BUILD_DIR

Reads BUILD_DIR/project_description.json, which ESP-IDF writes for every build.
Fails unless fmsui and lvgl are among the build's components, and unless none
of fmsui_fonts, the Tab5 BSP or esp_lvgl_port is.  The consumer exists to show
that the framework builds without those, and a component that arrived through
some indirect requirement would not show up anywhere else.

Also reads BUILD_DIR/compile_commands.json, and fails unless fmsui's internal
headers (src/internal/fmsui/arena.h and element.h) stay private to it: not in
its public include/, on the include path of every fmsui source, and on the
include path of no source in main -- the ESP-IDF counterpart of the host
consumer's check_internal_headers.py, since the component is given that
directory through PRIV_INCLUDE_DIRS rather than through CMake directly.

Prints a Markdown report on stdout, for the job summary.
"""

import json
import pathlib
import sys

from compile_commands import include_dirs, load

REQUIRED = ("fmsui", "lvgl")

# Matched against the end of a component name: managed components are called
# namespace__name, e.g. espressif__m5stack_tab5.
FORBIDDEN = ("fmsui_fonts", "m5stack_tab5", "esp_lvgl_port")

INTERNAL = ("arena", "element")


def internal_header_rows(build: pathlib.Path, fmsui: pathlib.Path) -> tuple:
    """Rows for the report, and whether any of them failed."""
    public = fmsui / "include"
    internal = (fmsui / "src" / "internal").resolve()
    entries = load(build)
    # ESP-IDF builds each component as __idf_<name>; the object's path says
    # which one a translation unit belongs to.
    library = [e for e in entries if "/__idf_fmsui.dir/" in e.get("output", "")]
    main = [e for e in entries if "/__idf_main.dir/" in e.get("output", "")]

    rows = []
    failed = False

    exposed = [h for h in INTERNAL if (public / "fmsui" / f"{h}.h").exists()]
    rows.append("| `fmsui` has no internal header in `include/` | "
                + ("none |" if not exposed else f"**has {', '.join(exposed)}** |"))
    failed |= bool(exposed)

    without = [e["file"] for e in library if internal not in include_dirs(e)]
    ok = bool(library) and not without
    rows.append(f"| `fmsui`'s {len(library)} sources have `src/internal` | "
                + ("all |" if ok else "**not all** |"))
    failed |= not ok

    leaked = [e["file"] for e in main
              if internal in include_dirs(e)
              or any((d / "fmsui" / f"{h}.h").is_file() for d in include_dirs(e) for h in INTERNAL)]
    ok = bool(main) and not leaked
    rows.append(f"| `main`'s {len(main)} sources cannot reach them | "
                + ("none can |" if ok else "**" + (", ".join(leaked) or "no sources found") + "** |"))
    failed |= not ok

    return rows, failed


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

    if "fmsui" in info:
        rows, rows_failed = internal_header_rows(build, pathlib.Path(info["fmsui"]["dir"]))
        lines.extend(rows)
        failed |= rows_failed

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
