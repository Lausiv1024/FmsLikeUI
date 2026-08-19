#!/usr/bin/env bash
# Drive the Windows-side ESP-IDF from WSL, so the device build can be checked
# without leaving this shell.
#
#   tools/idf.sh build
#   tools/idf.sh set-target esp32p4
#   tools/idf.sh fullclean
#
# Flashing and monitoring stay on the Windows side (or in the VS Code ESP-IDF
# extension): COM9 is not visible from here.
#
# Picks the newest ESP-IDF under ~/esp on the Windows side, which is how it
# starts using 5.5 the moment 5.5 is installed. Override with:
#   FMSUI_IDF=v5.5.4 tools/idf.sh build
set -euo pipefail

TOOLS_PATH_WIN='C:\Espressif'
TOOLS_PATH_WSL=/mnt/c/Espressif

# ESP-IDF lands in different places depending on how it was installed: the VS
# Code extension puts it under ~/esp, the standalone Windows installer under
# C:\esp. Look in both and take the newest.
ESP_ROOTS=(/mnt/c/esp /mnt/c/Users/lausiv1024/esp)

# --- which ESP-IDF ---
idf_path_wsl=""
if [[ -n "${FMSUI_IDF:-}" ]]; then
    for root in "${ESP_ROOTS[@]}"; do
        [[ -d "$root/$FMSUI_IDF/esp-idf" ]] && idf_path_wsl="$root/$FMSUI_IDF/esp-idf"
    done
else
    # `sort -V` orders v5.4.2 before v5.5.4, so the tail is the newest.
    best=$(for root in "${ESP_ROOTS[@]}"; do
        [[ -d "$root" ]] || continue
        find "$root" -maxdepth 1 -mindepth 1 -type d -name 'v*' 2>/dev/null |
            while read -r d; do
                [[ -d "$d/esp-idf" ]] && printf '%s\t%s\n' "$(basename "$d")" "$d/esp-idf"
            done
    done | sort -V | tail -1)
    idf_path_wsl=$(cut -f2 <<<"$best")
fi

if [[ -z "$idf_path_wsl" || ! -d "$idf_path_wsl" ]]; then
    echo "error: no ESP-IDF found under ${ESP_ROOTS[*]}" >&2
    exit 1
fi

idf_dir=$(basename "$(dirname "$idf_path_wsl")")
# /mnt/c/esp/v5.5.4/esp-idf -> C:\esp\v5.5.4\esp-idf
IDF_PATH_WIN=$(sed -e 's|^/mnt/c/|C:/|' <<<"$idf_path_wsl" | tr '/' '\\')

# --- which Python ---
# export.bat derives its virtualenv name from whichever `python` is first on
# PATH. The system default is 3.13 while the installed env is 3.11, so the
# right interpreter has to be put in front by hand or export.bat looks for an
# env that was never created.
py_dir=$(find "$TOOLS_PATH_WSL/tools/idf-python" -maxdepth 1 -mindepth 1 -type d -printf '%f\n' 2>/dev/null |
    sort -V | tail -1)
if [[ -z "$py_dir" ]]; then
    echo "error: no idf-python under $TOOLS_PATH_WSL/tools/idf-python" >&2
    exit 1
fi

IDF_PYTHON_WIN="C:\\Espressif\\tools\\idf-python\\${py_dir}"
PROJECT_WIN='C:\dev\Idf\FmsLikeUI'

echo "[idf.sh] ESP-IDF ${idf_dir} (${IDF_PATH_WIN}), python ${py_dir}" >&2

cmd.exe /c "set IDF_TOOLS_PATH=${TOOLS_PATH_WIN}&& set PATH=${IDF_PYTHON_WIN};%PATH%&& cd /d ${PROJECT_WIN} && call ${IDF_PATH_WIN}\\export.bat >nul && idf.py $*" 2>&1 |
    iconv -f CP932 -t UTF-8 2>/dev/null || true
