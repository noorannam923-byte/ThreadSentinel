# Thread Sentinel — Implementation Progress

## Phases Checklist

- [x] **Phase 1: Skeleton & build**
  - [x] Repository layout (`include/`, `src/`, `demos/`, `tests/`, `tools/`, `docs/`, `results/`)
  - [x] Top-level `Makefile` with targets (`all`, `demos`, `baseline`, `test`, `setup`, `clean`)
  - [x] Zero-warning build (`-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -O2 -g -pthread -Iinclude`)
  - [x] `include/sentinel.h` public API with `#ifndef TS_DISABLE` and `-DTS_DISABLE` passthrough
  - [x] Initial stubs for runtime and demo 01 compiling both instrumented and baseline
  - [x] `requirements.txt` with jinja2, plotly, pytest

- [x] **Phase 2: Vector clocks & thread registry**
  - [x] `src/vector_clock.c` & `src/vector_clock.h` (init, copy, inc, join/max, happens-before compare, format)
  - [x] Dual vector clocks per thread: `HB_t` (happens-before) and `ST_t` (structural)
  - [x] `src/threads.c` thread registry with `__thread` tid, trampoline, `pthread_create`, `pthread_join`
  - [x] C unit tests in `tests/unit/test_vector_clock.c` and `tests/unit/test_threads.c`

- [x] **Phase 3: Sync wrappers & lockset**
  - [x] `src/sync.c` wrappers for mutex (`lock`, `trylock`, `unlock`), condvar (`wait`, `signal`, `broadcast`), barrier (`wait`)
  - [x] Lock registry assigning IDs 0..63 and tracking `locks_held(t)` bitmask
  - [x] `src/lockset.c` & `src/lockset.h` bitsets and Eraser state machine (`VIRGIN`, `EXCLUSIVE`, `SHARED`, `SHARED_MODIFIED`)
  - [x] C unit tests for lockset operations and Eraser state transitions in `tests/unit/test_lockset.c`

- [x] **Phase 4: Shadow memory, hybrid detector & reporting**
  - [x] `src/shadow.c` & `src/shadow.h` hash table (addr -> access history, last write, reads since last write, candidate lockset)
  - [x] `src/detector.c` & `src/detector.h` hybrid classification (`CONFIRMED`, `POTENTIAL`, `SAFE`) + `TS_DETECTOR` modes (`hybrid`, `lockset`, `hb`)
  - [x] Memory access macros implementation: `TS_READ`, `TS_WRITE`, `TS_UPDATE`, `TS_FREE`
  - [x] `src/report.c` & `src/report.h` race reporting (colored stderr + JSON report `TS_REPORT`, race de-duplication)
  - [x] Unit tests in `tests/unit/test_shadow.c` and `tests/unit/test_detector.c`
  - [x] Validation with early demos (01, 02, 06, 07)

- [x] **Phase 5: All 12 demo programs & integration tests**
  - [x] Demo 01: `counter_race` (CONFIRMED)
  - [x] Demo 02: `counter_locked` (SAFE)
  - [x] Demo 03: `bank_transfer` (CONFIRMED)
  - [x] Demo 04: `check_then_act` (CONFIRMED)
  - [x] Demo 05: `producer_consumer_bug` (CONFIRMED)
  - [x] Demo 06: `fork_join_safe` (SAFE - showcase vs lockset FP)
  - [x] Demo 07: `lucky_lock_order` (POTENTIAL - showcase vs pure HB miss)
  - [x] Demo 08: `condvar_handoff_safe` (SAFE)
  - [x] Demo 09: `read_only_shared` (SAFE)
  - [x] Demo 10: `mixed_locks` (CONFIRMED)
  - [x] Demo 11: `barrier_phases_safe` (SAFE)
  - [x] Demo 12: `linked_list_race` (CONFIRMED)
  - [x] Ground truth JSON files in `demos/expected/`
  - [x] Pytest suite in `tests/integration/test_demos.py`

- [x] **Phase 6: Prevention runtime**
  - [x] `src/prevention.c` & `src/prevention.h` (striped guard locks, protected address/symbol set)
  - [x] Dynamic live protection on first race detection
  - [x] Pre-execution protection via `TS_PROTECT_FILE`
  - [x] `TS_REGION_BEGIN(n, ...)` and `TS_REGION_END()` with ascending stripe ordering to prevent deadlocks
  - [x] Integration test: 20/20 runs correct output with 0 CONFIRMED races for all racy demos (`tests/integration/test_prevention.py`)

- [x] **Phase 7: Trace logging, Python CLI, replay & HTML report**
  - [x] `src/trace.c` & `src/trace.h` JSONL event trace writer (`TS_TRACE`), with sound trace compaction
  - [x] Python CLI: `tools/sentinel/__main__.py` (`run`, `report`, `replay`, `demo`, `bench`) — `bench` body is Phase 8
  - [x] Python offline replay engine: `tools/sentinel/replay.py` verifying against C detector results
  - [x] Standalone HTML report generator with inlined Plotly.js in `tools/sentinel/report_html.py`
  - [x] Verify `replay` outputs `MATCH` for all demo traces (`tests/integration/test_replay.py`, hybrid + hb, exact race-set equality)

- [x] **Phase 8: Evaluation & benchmarks**
  - [x] Benchmark runner `tools/sentinel/bench_cmd.py` and `make bench` (`--only`, `--quick`, `--runs`, `--trials`)
  - [x] Accuracy comparison: Sentinel (Hybrid, Lockset, HB) vs ThreadSanitizer (TSan) across 12 demos
  - [x] Performance overhead: uninstrumented vs Sentinel detect vs Sentinel prevent vs TSan
  - [x] Scalability benchmark: thread count scaling (2, 4, 8, 16 threads) via `bench/scale_counter.c`
  - [x] Prevention effectiveness: 20-run trials measuring bug elimination rate
  - [x] Populate `docs/EVALUATION.md` with real benchmark tables and charts (`docs/img/*.svg`)

- [x] **Phase 9: Documentation & viva preparation**
  - [x] `README.md` (overview, architecture diagram, quick start, usage)
  - [x] `docs/ARCHITECTURE.md` (subsystems, data flows, invariants)
  - [x] `docs/ALGORITHMS.md` (formal rules, step-by-step worked trace example)
  - [x] `docs/VIVA_QA.md` (25+ viva questions with concise answers) — 30 questions
  - [x] `docs/REPORT_OUTLINE.md` (academic project report outline)
  - [x] Valgrind memory leak verification (`make valgrind` → `tools/valgrind_all.sh`, all 12 demos clean)
  - [x] Optional GitHub Actions CI workflow (`.github/workflows/ci.yml`; not yet run, as there is no remote)

- [ ] **Phase 10: Stretch features**
  - [ ] Potential deadlock detection (lock-order acquisition graph cycle detection)
  - [ ] `LD_PRELOAD` synchronization interception shim

---

## Deviations from Spec
- **Atomicity regions in demos 04 and 12.** Per-access guard locks serialise individual
  reads/writes but cannot make a read-then-dependent-write atomic, so check-then-act (04) and
  list push (12) still lost updates in prevent mode. Both demos now wrap the compound operation in
  `TS_REGION_BEGIN/END`, which is a no-op in detect mode (detect verdicts unchanged).
- **Re-entrant guard stripes.** Each thread keeps a per-stripe hold depth, so an access inside a
  region whose stripe is already held does not self-deadlock, and an unlock with no matching lock
  (address became protected between the pre and post hooks via live protection) is a no-op.
- **Region name resolution.** `TS_REGION_BEGIN` passes its stringified arguments; the runtime strips
  `&` to recover variable names so protect-file entries match before their address is bound.
- **Known limitation:** a region must list every protected variable it touches; accessing another
  protected variable inside a region takes that stripe out of ascending order.
- **Replay uses recorded clocks.** Each access event carries the runtime's HB/ST clocks and held
  locks; `replay.py` independently re-implements shadow memory, the hybrid/HB classification and the
  report de-duplication over them, rather than re-deriving clocks from sync events. It compares the
  exact set of (address, location pair, verdict) against the C report, not just counts. Lockset-mode
  and prevent-mode runs are not replayable (rejected with a clear error).
- **Trace compaction.** A full trace was ~350 MB / 2.3M events for demo 01. In detect mode the runtime
  omits accesses that provably cannot produce a race (same-thread run after that thread's write,
  flushing the final records when another thread arrives / on free / at exit; identical re-reads),
  and lock/unlock pairs re-acquired by the previous holder. Demo 01 is now ~44 MB / 300k events.
- **Demos 06 and 11 strengthened for the lockset showcase.** Their expected files said pure lockset
  reports a false positive, but it never did (untested: test_demos only checked hybrid). Eraser's
  EXCLUSIVE→SHARED rule correctly tolerates "write once, then others only read". 06 now has main
  zero `output_data` before fork, and 11 has each thread clear its neighbour's slot after the
  barrier, giving a real lock-free cross-thread write handoff. Lockset now flags both, and hybrid/HB
  still report SAFE. `test_demos.py` now checks the lockset and HB verdicts too.
- **Demo 05 `count` is `volatile`.** In the `-DTS_DISABLE` builds `-O2` could hoist the consumer's
  unlocked read out of its spin loop and hang forever (seen under TSan). `volatile` prevents the
  hoist without synchronising, so the race is still real and still reported.
- **TSan runs under `setarch -R`.** gcc 13 TSan aborts on this kernel's 32-bit mmap randomisation.
- **`make bench`/`demo-all` set `PYTHONPATH=tools`**; previously `python -m sentinel` could not be found.
- **`make valgrind` checks all demos in two passes.** A single detect-mode run of 12 reports the
  demo's own race-induced leak (the race orphans a chain of nodes). Pass 1 runs prevent mode with a
  protect file and full leak checking, so any leak is the runtime's. Pass 2 runs detect mode with
  memory errors fatal and leaks not counted.
- **`sentinel run` arguments.** Binary arguments go after `--` (previously every flag after the
  binary path, including `--report`, was silently passed to the binary).
