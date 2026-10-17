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

- [ ] **Phase 6: Prevention runtime**
  - [ ] `src/prevention.c` & `src/prevention.h` (striped guard locks, protected address/symbol set)
  - [ ] Dynamic live protection on first race detection
  - [ ] Pre-execution protection via `TS_PROTECT_FILE`
  - [ ] `TS_REGION_BEGIN(n, ...)` and `TS_REGION_END()` with ascending stripe ordering to prevent deadlocks
  - [ ] Integration test: 20/20 runs correct output with 0 CONFIRMED races for all racy demos

- [ ] **Phase 7: Trace logging, Python CLI, replay & HTML report**
  - [ ] `src/trace.c` & `src/trace.h` JSONL event trace writer (`TS_TRACE`)
  - [ ] Python CLI: `tools/sentinel/__main__.py` (`run`, `report`, `replay`, `demo`, `bench`)
  - [ ] Python offline replay engine: `tools/sentinel/replay.py` verifying against C detector results
  - [ ] Standalone HTML report generator with inlined Plotly.js in `tools/sentinel/report_html.py`
  - [ ] Verify `replay` outputs `MATCH` for all demo traces

- [ ] **Phase 8: Evaluation & benchmarks**
  - [ ] Benchmark runner `tools/sentinel/bench.py` and `make bench`
  - [ ] Accuracy comparison: Sentinel (Hybrid, Lockset, HB) vs ThreadSanitizer (TSan) across 12 demos
  - [ ] Performance overhead: uninstrumented vs Sentinel detect vs Sentinel prevent vs TSan
  - [ ] Scalability benchmark: thread count scaling (2, 4, 8, 16 threads)
  - [ ] Prevention effectiveness: 20-run trials measuring bug elimination rate
  - [ ] Populate `docs/EVALUATION.md` with real benchmark tables and charts

- [ ] **Phase 9: Documentation & viva preparation**
  - [ ] `README.md` (overview, architecture diagram, quick start, usage)
  - [ ] `docs/ARCHITECTURE.md` (subsystems, data flows, invariants)
  - [ ] `docs/ALGORITHMS.md` (formal rules, step-by-step worked trace example)
  - [ ] `docs/VIVA_QA.md` (25+ viva questions with concise answers)
  - [ ] `docs/REPORT_OUTLINE.md` (academic project report outline)
  - [ ] Valgrind memory leak verification (`make valgrind`)
  - [ ] Optional GitHub Actions CI workflow

- [ ] **Phase 10: Stretch features**
  - [ ] Potential deadlock detection (lock-order acquisition graph cycle detection)
  - [ ] `LD_PRELOAD` synchronization interception shim

---

## Deviations from Spec
*None so far.*
