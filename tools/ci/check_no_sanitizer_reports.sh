#!/usr/bin/env bash
# Fail if any of the given logs contains an ASan, UBSan or LeakSanitizer report.
#
#   bash tools/ci/check_no_sanitizer_reports.sh build/Testing/Temporary/LastTest.log
#
# CI sets the sanitizers to halt, so a report is already a non-zero exit.  This
# is the second line: it keeps a report a failure even when a run's own
# ASAN_OPTIONS / UBSAN_OPTIONS let the process carry on and exit 0.
set -euo pipefail

if [[ $# -eq 0 ]]; then
    echo "usage: $0 LOG..." >&2
    exit 2
fi

pattern='runtime error:|==[0-9]+==ERROR: [A-Za-z]+Sanitizer|SUMMARY: [A-Za-z]+Sanitizer'

status=0
for log in "$@"; do
    if [[ ! -f "$log" ]]; then
        echo "::error::$log: no such log"
        status=1
    elif grep -Eq "$pattern" "$log"; then
        echo "::error::$log: sanitizer report"
        grep -En "$pattern" "$log" | head -20
        status=1
    fi
done
exit $status
