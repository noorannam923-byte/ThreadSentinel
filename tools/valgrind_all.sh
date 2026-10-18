#!/usr/bin/env bash
# Run every instrumented demo under Valgrind memcheck.
#
# Pass 1 (prevent mode + protect file): the races are serialised, so the demos' own
#   race-induced leaks (e.g. 12_linked_list_race orphaning nodes) cannot happen.
#   Any definite/indirect leak here is the runtime's, and fails the run.
# Pass 2 (detect mode): invalid reads/writes etc. fail the run; leaks are not counted,
#   because a racy demo may legitimately lose memory when its race manifests.
set -u
cd "$(dirname "$0")/.."

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
vg=(valgrind --quiet --error-exitcode=99)
failed=0

for bin in demos/bin/[0-9][0-9]_*; do
    case "$bin" in *_plain|*_tsan) continue ;; esac
    name=$(basename "$bin")
    TS_QUIET=1 TS_MODE=detect TS_REPORT="$tmp/$name.protect.json" "$bin" >/dev/null

    TS_QUIET=1 TS_MODE=prevent TS_PROTECT_FILE="$tmp/$name.protect.json" TS_REPORT="$tmp/r.json" \
        "${vg[@]}" --leak-check=full --errors-for-leak-kinds=definite,indirect "$bin" >/dev/null 2>"$tmp/p.log"
    p=$?
    TS_QUIET=1 TS_MODE=detect TS_REPORT="$tmp/r.json" \
        "${vg[@]}" --leak-check=no "$bin" >/dev/null 2>"$tmp/d.log"
    d=$?

    if [ $p -eq 0 ] && [ $d -eq 0 ]; then
        echo "  ok    $name"
    else
        echo "  FAIL  $name (prevent+leaks exit=$p, detect exit=$d)"
        cat "$tmp/p.log" "$tmp/d.log"
        failed=1
    fi
done

[ $failed -eq 0 ] && echo "valgrind: all demos clean" || echo "valgrind: failures above"
exit $failed
