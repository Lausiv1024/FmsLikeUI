#!/usr/bin/env bash
# Copy the part of this repository an application using FmsLikeUI from source
# would have, and nothing else.
#
#   bash tools/ci/stage_consumer.sh DEST
#
# Copied:     components/fmsui, third_party/lv_conf.h, third_party/lvgl, consumers/
# Not copied: the top-level CMakeLists.txt, sdkconfig.defaults, dependencies.lock,
#             main/, demo/, sim/, tests/, tools/, components/fmsui_fonts
#
# Building the consumers from this copy is what shows they do not depend on the
# rest. A CMakeLists that reached for sim/, or a component requirement that
# pulled in fmsui_fonts, cannot configure here, however it got there. Build
# output a local run may have left under consumers/ is not copied either.
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 DEST" >&2
    exit 2
fi

repo=$(cd "$(dirname "$0")/../.." && pwd)
dest=$(realpath -m "$1")

if [[ -e "$dest" ]]; then
    echo "error: $dest already exists" >&2
    exit 2
fi
if [[ ! -f "$repo/third_party/lvgl/lvgl.h" ]]; then
    echo "error: third_party/lvgl is empty; check out the submodule" >&2
    exit 2
fi

mkdir -p "$dest"
tar -C "$repo" -cf - \
    --exclude=.git \
    --exclude='consumers/*/build*' \
    --exclude=consumers/esp-idf/sdkconfig \
    --exclude=consumers/esp-idf/sdkconfig.old \
    --exclude=consumers/esp-idf/dependencies.lock \
    --exclude=consumers/esp-idf/managed_components \
    components/fmsui third_party/lv_conf.h third_party/lvgl consumers |
    tar -C "$dest" -xf -

echo "staged $dest:"
(cd "$dest" && find . -mindepth 1 -maxdepth 3 -not -path './third_party/lvgl/*' | sort)
