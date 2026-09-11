#!/usr/bin/env bash
# The ESP-IDF consumer, as CI runs it.
#
#   . "$IDF_PATH/export.sh"
#   bash tools/ci/consumer_idf.sh [OUT_DIR]       (ci-out/consumer-idf)
#
# Stages a tree holding only what an ESP-IDF project using FmsLikeUI as a local
# component would have (OUT_DIR/tree, see stage_consumer.sh) and builds
# consumers/esp-idf from it for the ESP32-P4, into OUT_DIR/build. The tree has
# no main/, no demo/, no top-level project and no dependencies.lock, so neither
# the Tab5 BSP nor fmsui_fonts can be reached; check_consumer_components.py then
# confirms it from the build's own description. Nothing is flashed.
#
# Warnings in fmsui and in the consumer's main fail the build (FMSUI_WERROR=ON).
# The image size, the room left in the app partition and the component check go
# to the summary: $GITHUB_STEP_SUMMARY when set, and OUT_DIR/summary.md always.
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
out=$(realpath -m "${1:-ci-out/consumer-idf}")

if ! command -v idf.py >/dev/null; then
    echo "error: idf.py is not on PATH; source \$IDF_PATH/export.sh first" >&2
    exit 2
fi
if [[ -e "$out" ]]; then
    echo "error: $out already exists; the consumer has to start from nothing" >&2
    exit 2
fi

echo "::group::stage the consumer's tree"
bash "$repo/tools/ci/stage_consumer.sh" "$out/tree"
echo "::endgroup::"

project="$out/tree/consumers/esp-idf"
build="$out/build"
: >"$out/summary.md"

summary() {
    tee -a "$out/summary.md" >>"${GITHUB_STEP_SUMMARY:-/dev/null}"
}

{
    echo "## ESP-IDF consumer ($(idf.py --version), esp32p4)"
    echo
} | summary

echo "::group::build consumers/esp-idf"
rc=0
idf.py -C "$project" -B "$build" \
    -DSDKCONFIG="$build/sdkconfig" \
    -DIDF_TARGET=esp32p4 \
    -DFMSUI_WERROR=ON \
    build 2>&1 | tee "$out/build.log" || rc=$?
echo "::endgroup::"
if [[ $rc -ne 0 ]]; then
    echo "::error::ESP-IDF consumer build failed (log: $out/build.log)"
    echo "Build failed." | summary
    exit 1
fi

status=0

# As in device_build.sh: the partition check the build runs, invoked directly so
# the numbers can be put in the summary.
offset=$(sed -n 's/^CONFIG_PARTITION_TABLE_OFFSET=//p' "$build/sdkconfig")
sizes=$(python "$IDF_PATH/components/partition_table/check_sizes.py" \
    --offset "$offset" partition --type app \
    "$build/partition_table/partition-table.bin" "$build/fmsui_consumer.bin")
echo "$sizes"
re='binary size (0x[0-9a-fA-F]+) bytes\. Smallest app partition is (0x[0-9a-fA-F]+) bytes\. (0x[0-9a-fA-F]+) bytes \(([0-9]+)%\) free'
if [[ $sizes =~ $re ]]; then
    {
        echo "| fmsui_consumer.bin | Smallest app partition | Free |"
        echo "|---:|---:|---:|"
        echo "| $((BASH_REMATCH[1])) bytes | $((BASH_REMATCH[2])) bytes | $((BASH_REMATCH[3])) bytes (${BASH_REMATCH[4]}%) |"
        echo
    } | summary
else
    echo "::error::could not read the app partition check"
    status=1
fi

components_rc=0
components=$(python "$repo/tools/ci/check_consumer_components.py" "$build") || components_rc=$?
echo "$components" | summary
if [[ $components_rc -ne 0 ]]; then
    echo "::error::the ESP-IDF consumer built with components it must not need"
    status=1
fi

exit $status
