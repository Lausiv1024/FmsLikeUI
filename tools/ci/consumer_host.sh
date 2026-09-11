#!/usr/bin/env bash
# The host consumer, as CI runs it.
#
#   bash tools/ci/consumer_host.sh [OUT_DIR]      (ci-out/consumer-host)
#
# Stages a tree holding only what an application using FmsLikeUI from source
# would have (OUT_DIR/tree, see stage_consumer.sh), then configures, builds and
# runs consumers/host from it into OUT_DIR/build. It is a configure of its own,
# from nothing: no build from sim/ is read or reused. Warnings in the consumer's
# own targets, public headers included, fail the build. Last, it checks that the
# library's internal headers cannot be included from the consumer
# (check_internal_headers.py).
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
out=$(realpath -m "${1:-ci-out/consumer-host}")

if [[ -e "$out" ]]; then
    echo "error: $out already exists; the consumer has to start from nothing" >&2
    exit 2
fi

echo "::group::stage the consumer's tree"
bash "$repo/tools/ci/stage_consumer.sh" "$out/tree"
echo "::endgroup::"

cmake -S "$out/tree/consumers/host" -B "$out/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DFMSUI_WERROR=ON
cmake --build "$out/build"
ctest --test-dir "$out/build" --output-on-failure --timeout 120

echo "::group::internal headers are out of the consumer's reach"
python3 "$repo/tools/ci/check_internal_headers.py" "$out/tree" "$out/build"
echo "::endgroup::"
