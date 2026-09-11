#!/usr/bin/env bash
# Render every demo headlessly and check that each one produced a picture.
#
#   bash tools/ci/render_demos.sh build/fmsui_sim ci-out/demos
#
# A demo passes when the simulator exits 0 within the timeout, the file it
# wrote is a non-empty PNG, and its output carries no sanitizer report.  Every
# demo is run even after one fails, so a single run shows all that broke.  The
# PNGs and per-demo logs stay in the output directory for the failure artifact.
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 FMSUI_SIM OUT_DIR" >&2
    exit 2
fi

sim=$1
out=$2
here=$(dirname "$0")
demos=(m0 m1 catalog pages reorder fplan)
png_signature=89504e470d0a1a0a

mkdir -p "$out"
failed=()
for demo in "${demos[@]}"; do
    png="$out/$demo.png"
    log="$out/$demo.log"
    rm -f "$png"

    status=0
    timeout 120 "$sim" --demo "$demo" --shot "$png" >"$log" 2>&1 || status=$?

    problem=""
    if [[ $status -eq 124 ]]; then
        problem="timed out after 120 s"
    elif [[ $status -ne 0 ]]; then
        problem="exit status $status"
    elif [[ ! -s "$png" ]]; then
        problem="no PNG, or an empty one"
    elif [[ $(head -c 8 "$png" | od -An -tx1 | tr -d ' \n') != "$png_signature" ]]; then
        problem="not a PNG"
    elif ! bash "$here/check_no_sanitizer_reports.sh" "$log" >/dev/null; then
        problem="sanitizer report"
    fi

    if [[ -n "$problem" ]]; then
        echo "::error::demo $demo: $problem"
        cat "$log"
        failed+=("$demo")
    else
        echo "ok   $demo  $(stat -c %s "$png") bytes"
    fi
done

if [[ ${#failed[@]} -ne 0 ]]; then
    echo "${#failed[@]} of ${#demos[@]} demos failed: ${failed[*]}"
    exit 1
fi
echo "all ${#demos[@]} demos rendered"
