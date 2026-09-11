"""Reading compile_commands.json, for the checks in this directory.

Imported by check_internal_headers.py and check_consumer_components.py; not a
script of its own.
"""

import json
import pathlib
import shlex

# Longest first is not needed: none of these is a prefix of another.
_INCLUDE_FLAGS = ("-I", "-isystem", "-iquote", "-idirafter")


def load(build: pathlib.Path) -> list:
    return json.loads((build / "compile_commands.json").read_text())


def source(entry: dict) -> pathlib.Path:
    """The translation unit, as an absolute path."""
    return (pathlib.Path(entry["directory"]) / entry["file"]).resolve()


def include_dirs(entry: dict) -> list:
    """Every directory the entry's command puts on an include path, absolute."""
    args = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
    base = pathlib.Path(entry["directory"])
    dirs = []
    it = iter(args)
    for arg in it:
        for flag in _INCLUDE_FLAGS:
            if arg == flag:
                value = next(it, None)
            elif arg.startswith(flag):
                value = arg[len(flag):]
            else:
                continue
            if value:
                dirs.append((base / value).resolve())
            break
    return dirs


def under(path: pathlib.Path, root: pathlib.Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False
