#!/usr/bin/env bash
# Device build for CI: every FMSUI_DEMO configuration, for the ESP32-P4.
#
#   . "$IDF_PATH/export.sh"
#   bash tools/ci/device_build.sh [BUILD_DIR [LOG_DIR]]    (build-ci, ci-out/device)
#
# The first configuration is the default demo, built from a build directory that
# does not exist yet.  Its sdkconfig is generated inside that directory from
# sdkconfig.defaults, so an sdkconfig left in the project root is never read.
# The other five reconfigure the same directory, which recompiles what
# FMSUI_DEMO changes and links a new image.
#
# Every configuration builds with -DFMSUI_WERROR=ON, so a warning in main,
# demo/ or components/ fails it; ESP-IDF, the managed components and LVGL keep
# ESP-IDF's own policy.  After the default build, the compile database must
# show no LVGL examples or demos.  Each image's size and the room left in the
# smallest app partition go to the summary: $GITHUB_STEP_SUMMARY when set, and
# LOG_DIR/summary.md always.
#
# The managed components come from the committed dependencies.lock.  The
# component manager leaves that file alone while it agrees with
# main/idf_component.yml and rewrites it when it does not; a rewrite means these
# builds used versions nobody committed, so it fails the run.
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
build=$(realpath -m "${1:-build-ci}")
logs=$(realpath -m "${2:-ci-out/device}")
cd "$repo"

if ! command -v idf.py >/dev/null; then
    echo "error: idf.py is not on PATH; source \$IDF_PATH/export.sh first" >&2
    exit 2
fi
if [[ -e "$build" ]]; then
    echo "error: $build already exists; the first build has to start from nothing" >&2
    exit 2
fi
if [[ ! -f "$repo/dependencies.lock" ]]; then
    echo "error: no dependencies.lock; the device build is pinned by the committed one" >&2
    exit 2
fi
mkdir -p "$logs"
: >"$logs/summary.md"
cp "$repo/dependencies.lock" "$logs/dependencies.committed.lock"

summary() {
    tee -a "$logs/summary.md" >>"${GITHUB_STEP_SUMMARY:-/dev/null}"
}

# name, then the FMSUI_DEMO value (empty for the default demo)
configs=(
    "default:"
    "m0:m0"
    "m1:m1"
    "catalog:catalog"
    "fplan:fplan"
    "reorder:reorder"
)

{
    echo "## Device build ($(idf.py --version), esp32p4)"
    echo
    echo "| Configuration | FMSUI_DEMO | fmslikeui.bin | Smallest app partition | Free |"
    echo "|---|---|---:|---:|---:|"
} | summary

status=0
for config in "${configs[@]}"; do
    name=${config%%:*}
    demo=${config#*:}
    log="$logs/$name.log"

    echo "::group::build $name (FMSUI_DEMO=${demo:-<default>})"
    rc=0
    idf.py -B "$build" \
        -DSDKCONFIG="$build/sdkconfig" \
        -DIDF_TARGET=esp32p4 \
        -DFMSUI_WERROR=ON \
        -DFMSUI_DEMO="$demo" \
        build 2>&1 | tee "$log" || rc=$?
    echo "::endgroup::"
    if [[ $rc -ne 0 ]]; then
        echo "::error::device build failed: $name (log: $log)"
        echo "| $name | \`${demo:-(default)}\` | build failed | | |" | summary
        status=1
        break
    fi

    # The same check the build runs, invoked directly: an incremental build that
    # did not relink would not print it, and the summary wants every image.
    offset=$(sed -n 's/^CONFIG_PARTITION_TABLE_OFFSET=//p' "$build/sdkconfig")
    sizes=$(python "$IDF_PATH/components/partition_table/check_sizes.py" \
        --offset "$offset" partition --type app \
        "$build/partition_table/partition-table.bin" "$build/fmslikeui.bin")
    echo "$sizes"
    re='binary size (0x[0-9a-fA-F]+) bytes\. Smallest app partition is (0x[0-9a-fA-F]+) bytes\. (0x[0-9a-fA-F]+) bytes \(([0-9]+)%\) free'
    if [[ ! $sizes =~ $re ]]; then
        echo "::error::could not read the app partition check for $name"
        status=1
        break
    fi
    echo "| $name | \`${demo:-(default)}\` | $((BASH_REMATCH[1])) bytes | $((BASH_REMATCH[2])) bytes | $((BASH_REMATCH[3])) bytes (${BASH_REMATCH[4]}%) |" | summary

    if [[ $name == default ]]; then
        cp "$build/compile_commands.json" "$logs/compile_commands.default.json"
        # Printed after the size table rather than inside it.
        lvgl_report=0
        GITHUB_STEP_SUMMARY="$logs/lvgl_sources.md" \
            python "$repo/tools/ci/check_lvgl_sources.py" "$build/compile_commands.json" ||
            lvgl_report=$?
        if [[ $lvgl_report -ne 0 ]]; then
            echo "::error::LVGL examples or demos were compiled"
            status=1
        fi
    fi
done

lock_note="dependencies.lock: unchanged by the build"
if ! cmp -s "$logs/dependencies.committed.lock" "$repo/dependencies.lock"; then
    lock_note="dependencies.lock: REWRITTEN by the build -- it disagrees with main/idf_component.yml"
    echo "::error::dependencies.lock was rewritten during the build, so it disagrees with main/idf_component.yml. Run idf.py update-dependencies, check the result on the device, and commit the new lock."
    diff "$logs/dependencies.committed.lock" "$repo/dependencies.lock" || true
    status=1
fi

{
    echo
    [[ -f "$logs/lvgl_sources.md" ]] && cat "$logs/lvgl_sources.md"
    echo "$lock_note"
    echo
    if [[ -f "$repo/dependencies.lock" ]]; then
        cp "$repo/dependencies.lock" "$logs/dependencies.lock"
        echo "<details><summary>Component manager resolution (dependencies.lock after the build)</summary>"
        echo
        echo '```yaml'
        cat "$repo/dependencies.lock"
        echo '```'
        echo
        echo "</details>"
    fi
} | summary

exit $status
