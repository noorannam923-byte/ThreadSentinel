#!/usr/bin/env bash
# Guided tour of Thread Sentinel's features (make showcase).
#   bash tools/showcase.sh            run straight through
#   bash tools/showcase.sh --pause    wait for Enter between steps (live demos)
set -u
cd "$(dirname "$0")/.."

PAUSE=0
[ "${1:-}" = "--pause" ] && PAUSE=1
OUT=results/showcase
mkdir -p "$OUT"
PY=".venv/bin/python"
[ -x "$PY" ] || PY=python3
export PYTHONPATH=tools

B='\033[1m'; C='\033[96m'; D='\033[2m'; R='\033[0m'
step() {
    [ $PAUSE -eq 1 ] && { printf "${D}(press Enter)${R}"; read -r _; }
    printf "\n${B}${C}━━━ %s ━━━${R}\n" "$1"
    printf "${D}%s${R}\n\n" "$2"
}
run() { printf "${B}\$ %s${R}\n" "$*"; "$@"; echo; }
summary() { "$PY" - "$1" <<'EOF'
import json, sys
s = json.load(open(sys.argv[1]))["summary"]
print(f"  -> report: confirmed={s['confirmed_races']} potential={s['potential_races']} "
      f"deadlocks={s.get('potential_deadlocks', 0)}")
EOF
}

[ -x demos/bin/01_counter_race ] && [ -x demos/bin/12_linked_list_race_plain ] && [ -f libsentinel_preload.so ] \
    || { echo "Build first: make all baseline"; exit 1; }

step "1. A real data race (CONFIRMED)" \
"Four threads increment 'counter' with no lock. Sentinel reports the two unordered, unprotected writes."
TS_REPORT=$OUT/01.json run demos/bin/01_counter_race
summary $OUT/01.json

step "2. A race hidden by luck (POTENTIAL): hybrid vs. pure happens-before" \
"T1 and T2 write 'x' unlocked; an unrelated mutex happened to order them on this run.
Pure HB (the ThreadSanitizer approach) sees nothing. The hybrid's structural clock flags it."
TS_QUIET=1 TS_DETECTOR=hb TS_REPORT=$OUT/07_hb.json demos/bin/07_lucky_lock_order >/dev/null
printf "${B}pure HB:${R} "; summary $OUT/07_hb.json
TS_REPORT=$OUT/07.json run demos/bin/07_lucky_lock_order
printf "${B}hybrid:${R}  "; summary $OUT/07.json

step "3. No false alarm on lock-free synchronisation: hybrid vs. pure lockset" \
"Workers write results that main zeroed before fork; fork/join orders it, no locks needed.
Pure lockset (Eraser) cries race; the hybrid detector correctly stays quiet."
TS_QUIET=1 TS_DETECTOR=lockset TS_REPORT=$OUT/06_ls.json demos/bin/06_fork_join_safe >/dev/null
printf "${B}pure lockset:${R} "; summary $OUT/06_ls.json
TS_QUIET=1 TS_REPORT=$OUT/06.json demos/bin/06_fork_join_safe >/dev/null
printf "${B}hybrid:${R}       "; summary $OUT/06.json

step "4. Prevention: detect once, then run with the races serialised" \
"Four threads push onto a shared linked list without a lock and lose nodes.
Step A: the uninstrumented program is broken. Step B: detect and save the report.
Step C: run with TS_MODE=prevent and that report: every node survives."
run demos/bin/12_linked_list_race_plain
TS_QUIET=1 TS_REPORT=$OUT/12_detect.json demos/bin/12_linked_list_race >/dev/null
printf "${B}detected:${R} "; summary $OUT/12_detect.json; echo
run env TS_QUIET=1 TS_MODE=prevent TS_PROTECT_FILE=$OUT/12_detect.json TS_REPORT=$OUT/12_prevent.json \
    demos/bin/12_linked_list_race

step "5. Potential deadlock from a run that did not deadlock" \
"T1 locks lock_a then lock_b; T2 (10 ms later) locks lock_b then lock_a. This run finishes fine,
but the opposite timing would deadlock. Sentinel's lock-order graph reports the cycle."
TS_REPORT=$OUT/13.json run demos/bin/13_lock_order_inversion
summary $OUT/13.json

step "6. Same check on an UNMODIFIED binary via LD_PRELOAD" \
"13_lock_order_inversion_plain was compiled without Sentinel. Preloading the shim intercepts its
pthread calls; mutexes and call sites are shown by address."
TS_REPORT=$OUT/13_preload.json run env LD_PRELOAD=./libsentinel_preload.so demos/bin/13_lock_order_inversion_plain
summary $OUT/13_preload.json

step "7. Independent verification: offline replay of the event trace" \
"The runtime writes a JSONL trace; a separate Python implementation re-classifies every access
and must find exactly the races the C runtime reported."
TS_QUIET=1 TS_TRACE=$OUT/07.jsonl TS_REPORT=$OUT/07_trace.json demos/bin/07_lucky_lock_order >/dev/null
run "$PY" -m sentinel replay $OUT/07.jsonl --report $OUT/07_trace.json

step "8. Reports: terminal and standalone HTML" \
"The same JSON report pretty-printed, plus an offline HTML page (Plotly inlined)."
run "$PY" -m sentinel report $OUT/01.json --html $OUT/report_01.html
"$PY" -m sentinel report $OUT/13.json --html $OUT/report_13.html >/dev/null

printf "${B}Done.${R} Artefacts in ${C}%s/${R}  (HTML: report_01.html, report_13.html)\n" "$OUT"
