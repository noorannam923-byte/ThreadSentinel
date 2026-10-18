# Evaluation

All numbers below are produced by `make bench` (`python -m sentinel bench`); raw data is in
`results/bench/`. Sections between `BENCH` markers are regenerated on every run; the prose
around them quotes the run of 2026-10-18 and should be re-read if the numbers move.

## Summary

- **Accuracy.** The hybrid detector is the only configuration with no false positives and no
  false negatives on the 12 demos (7 racy, 5 safe). Pure lockset raises false positives on the two
  lock-free safe demos (fork-join, barrier); pure happens-before and ThreadSanitizer both miss the
  latent race in `07_lucky_lock_order`, which the hybrid reports as POTENTIAL.
- **Overhead.** Geometric-mean slowdown is about 11× for Sentinel (detect and prevent alike), against
  about 13× for TSan. The spread is wide (about 2× to 470×) and depends almost entirely on how many
  instrumented accesses a program makes per unit of real work.
- **Scalability.** Sentinel's slowdown versus plain stays roughly flat (about 30–65×) from 4 to 16
  threads, but absolute time grows linearly with total accesses: every instrumented access takes
  one global runtime lock.
- **Prevention.** With a protect file from a prior detect run, prevention removed every manifestation
  of the bug: 0 of 120 runs were wrong, against 80 of 120 unprotected. Live protection (no protect file) only
  helps when the race repeats after the first detection.

## Environment

<!-- BENCH:environment -->
- **Date:** 2026-10-18
- **CPU:** 11th Gen Intel(R) Core(TM) i5-1135G7 @ 2.40GHz (8 logical cores)
- **Kernel:** Linux 6.18.54.1-microsoft-standard-WSL2
- **Compiler:** gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- **Repetitions:** 5 timed runs per configuration (median reported); 20 trials per prevention configuration
- **TSan:** gcc `-fsanitize=thread`, run under `setarch -R` (see Methodology)
<!-- /BENCH:environment -->

### Methodology

- **Builds.** *Plain* and *TSan* are the same demo sources compiled with `-DTS_DISABLE` (the
  `TS_*` macros become plain accesses), the latter adding `-fsanitize=thread`. *Detect* and
  *prevent* are the instrumented build linked against `libsentinel.a`. All use `-O2`.
- **Timing** is wall-clock time of the whole process (start-up and report writing included),
  median of N runs, measured from Python with `time.perf_counter`.
- **Accuracy.** A demo counts as *racy* when it contains a real data race on some schedule; this
  includes `07_lucky_lock_order`, whose race is hidden on the observed schedule. A tool *flags* a
  demo if it reports anything (CONFIRMED or POTENTIAL; any TSan `data race` warning). Each
  configuration ran 3 times and the majority verdict is shown; no verdict varied between runs.
- **Prevent** runs first execute the demo once in detect mode to produce the protect file
  (`TS_PROTECT_FILE`). *Live* prevention gets no protect file and protects variables as races are
  detected.
- **TSan** runs under `setarch -R`. gcc 13's TSan runtime aborts at start-up ("unexpected memory
  mapping") on kernels with 32-bit mmap randomisation (`vm.mmap_rnd_bits = 32` here); disabling
  ASLR for the process is the standard workaround and does not affect detection.
- **Machine.** WSL2 on a laptop. Absolute times are noisy at the 10 ms scale; compare ratios rather
  than milliseconds.

## 1. Detection accuracy

<!-- BENCH:accuracy -->
| Demo | Real race? | Hybrid | Lockset | HB | TSan |
|---|---|---|---|---|---|
| `01_counter_race` | yes | CONFIRMED | CONFIRMED | CONFIRMED | RACE |
| `02_counter_locked` | no | NONE | NONE | NONE | NONE |
| `03_bank_transfer` | yes | CONFIRMED | CONFIRMED | CONFIRMED | RACE |
| `04_check_then_act` | yes | CONFIRMED | CONFIRMED | CONFIRMED | RACE |
| `05_producer_consumer_bug` | yes | CONFIRMED | CONFIRMED | CONFIRMED | RACE |
| `06_fork_join_safe` | no | NONE | CONFIRMED | NONE | NONE |
| `07_lucky_lock_order` | yes | POTENTIAL | CONFIRMED | NONE | NONE |
| `08_condvar_handoff_safe` | no | NONE | NONE | NONE | NONE |
| `09_read_only_shared` | no | NONE | NONE | NONE | NONE |
| `10_mixed_locks` | yes | CONFIRMED | CONFIRMED | CONFIRMED | RACE |
| `11_barrier_phases_safe` | no | NONE | CONFIRMED | NONE | NONE |
| `12_linked_list_race` | yes | CONFIRMED | CONFIRMED | CONFIRMED | RACE |

✗ = differs from `demos/expected/`; ~ = verdict varied between runs.

| Tool | TP | FP | FN | TN | Precision | Recall | Matches expected |
|---|---|---|---|---|---|---|---|
| Sentinel hybrid | 7 | 0 | 0 | 5 | 100% | 100% | 12/12 |
| Sentinel lockset | 7 | 2 | 0 | 3 | 78% | 100% | 12/12 |
| Sentinel HB | 6 | 0 | 1 | 5 | 100% | 86% | 12/12 |
| ThreadSanitizer | 6 | 0 | 1 | 5 | 100% | 86% | — |
<!-- /BENCH:accuracy -->

**Discussion.** The three Sentinel modes reproduce the trade-off the hybrid design targets:

- *Lockset* reasons only about locks, so `06_fork_join_safe` (ordered by fork) and
  `11_barrier_phases_safe` (ordered by a barrier) look like unprotected write-sharing: 2 false
  positives, precision 78%.
- *Happens-before* (and TSan, which is HB-based) sees that thread 1's unlock of an unrelated mutex
  precedes thread 2's lock in `07_lucky_lock_order` and treats the two writes to `x` as ordered.
  On another schedule they are not, so both tools miss a real race.
- *Hybrid* keeps HB's precision on the safe demos and uses the structural clock (which ignores
  mutex hand-offs) to flag 07 as POTENTIAL rather than silently accepting the lucky ordering.

The demo set is small and was written to exercise these cases, so the 100%/100% result for the
hybrid detector demonstrates the mechanism rather than estimating real-world accuracy.

## 2. Runtime overhead

<!-- BENCH:overhead -->
![Runtime overhead](img/overhead.svg)

| Demo | Plain (ms) | Detect (ms) | Prevent (ms) | TSan (ms) | Detect × | Prevent × | TSan × |
|---|---|---|---|---|---|---|---|
| `01_counter_race` | 7.4 | 3458.6 | 2925.9 | 165.5 | 469.6 | 397.3 | 22.5 |
| `02_counter_locked` | 249.4 | 4599.3 | 4821.9 | 3256.6 | 18.4 | 19.3 | 13.1 |
| `03_bank_transfer` | 19.2 | 613.9 | 571.1 | 3580.8 | 32.0 | 29.8 | 186.8 |
| `04_check_then_act` | 4.9 | 13.3 | 18.1 | 167.2 | 2.7 | 3.7 | 33.8 |
| `05_producer_consumer_bug` | 8.7 | 46.4 | 48.4 | 205.0 | 5.4 | 5.6 | 23.6 |
| `06_fork_join_safe` | 7.6 | 26.0 | 28.6 | 31.2 | 3.4 | 3.8 | 4.1 |
| `07_lucky_lock_order` | 19.8 | 38.5 | 45.5 | 39.9 | 1.9 | 2.3 | 2.0 |
| `08_condvar_handoff_safe` | 11.6 | 25.4 | 27.6 | 28.9 | 2.2 | 2.4 | 2.5 |
| `09_read_only_shared` | 11.7 | 540.6 | 480.0 | 25.6 | 46.3 | 41.2 | 2.2 |
| `10_mixed_locks` | 35.7 | 825.9 | 556.5 | 5198.3 | 23.1 | 15.6 | 145.7 |
| `11_barrier_phases_safe` | 8.1 | 17.0 | 24.2 | 21.0 | 2.1 | 3.0 | 2.6 |
| `12_linked_list_race` | 8.6 | 164.8 | 159.3 | 208.8 | 19.2 | 18.5 | 24.3 |
| **Geometric mean** | | | | | **10.8** | **11.1** | **12.7** |
<!-- /BENCH:overhead -->

**Discussion.**

- **Worst cases are tight loops of instrumented accesses.** `01_counter_race` does 2M unlocked
  increments. In the plain build gcc folds each thread's loop into a single `addq $500000,
  counter`, so the baseline is about 7 ms of process start-up, while Sentinel processes every
  access (≈470×). `09_read_only_shared` (≈46×) is similar: TSan's lock-free shadow handles
  read-sharing cheaply, whereas every Sentinel access takes the global runtime lock and updates
  shadow state.
- **TSan's worst cases are different.** `03_bank_transfer` and `10_mixed_locks` are lock-heavy;
  TSan pays for every mutex operation and for its race reports, Sentinel less so (TSan ≈150–190×
  against Sentinel ≈15–32×).
- **Short, synchronisation-dominated demos** (04, 06, 07, 08, 11) cost 2–4× under every tool,
  mostly start-up and report writing.
- **Prevent ≈ detect.** Guard stripes are taken only for protected addresses, and a protected
  address skips race evaluation, so prevent mode is sometimes slightly *faster* than detect
  (e.g. 01, 10).

The main lever for lowering overhead is the single global runtime lock (see Scalability).

## 3. Scalability

<!-- BENCH:scalability -->
![Scalability](img/scalability.svg)

| Threads | Plain (ms) | Detect (ms) | TSan (ms) | Detect × | TSan × |
|---|---|---|---|---|---|
| 2 | 19.7 | 611.0 | 137.8 | 30.9 | 7.0 |
| 4 | 25.0 | 1542.2 | 264.4 | 61.6 | 10.6 |
| 8 | 40.9 | 3465.6 | 784.0 | 84.8 | 19.2 |
| 16 | 102.1 | 6521.0 | 1350.8 | 63.8 | 13.2 |
<!-- /BENCH:scalability -->

**Discussion.** `bench/scale_counter` is race-free weak scaling: each thread does the same
work (200k private-slot updates plus a mutex-protected shared increment every 8th iteration), so
total work doubles with the thread count. All three builds grow roughly linearly in absolute time.
Sentinel's ratio to plain stays at about 30–65× and TSan's at about 7–15×. Sentinel does not get
relatively worse with more threads, but it also cannot use them: every instrumented access goes
through `g_ts_runtime_mutex`, so accesses are serialised across all threads and wall time tracks
the *total* access count. Sharding the runtime lock (for example per shadow bucket, plus a lock
for vector-clock updates) is the obvious next step.

## 4. Prevention effectiveness

<!-- BENCH:prevention -->
Cells are runs (out of N) whose output showed the bug.

| Demo | N | Plain | Detect only | Prevent (protect file) | Prevent (live) | Elimination (protect file) |
|---|---|---|---|---|---|---|
| `01_counter_race` | 20 | 0 | 20 | 0 | 0 | n/a |
| `03_bank_transfer` | 20 | 20 | 19 | 0 | 0 | 100% |
| `04_check_then_act` | 20 | 20 | 20 | 0 | 20 | 100% |
| `05_producer_consumer_bug` | 20 | 0 | 0 | 0 | 0 | n/a |
| `10_mixed_locks` | 20 | 20 | 20 | 0 | 0 | 100% |
| `12_linked_list_race` | 20 | 20 | 20 | 0 | 14 | 100% |
| **Total** | 120 | **80** | **99** | **0** | **34** | |

Runs that still reported a CONFIRMED race: protect file 0, live 120 (live mode reports the race that triggered protection).
<!-- /BENCH:prevention -->

**Discussion.**

- **With a protect file, prevention eliminated every observed failure** (100% on the four demos
  whose bug manifests in the plain build). Demos 04 and 12 need the `TS_REGION_BEGIN/END`
  annotation around their read-then-write; per-access guards alone cannot make a check-then-act
  atomic.
- **Unprotected baselines.** The *plain* column under-reports 01: `-O2` collapses its racy loop
  (see Overhead), so the bug only shows once instrumentation stops that optimisation (*detect
  only*: 20/20). Demo 05's race (an unlocked read of `count` used as a hint) never corrupts the
  final count, so its row cannot show an effect either way.
- **Live protection is best-effort.** It starts after the first CONFIRMED race, i.e. after the
  damaging interleaving has already happened. In 04, all eight threads pass the unprotected check
  before the race is even detected (20/20 still wrong). In 12 the damage shrinks from about 22,000
  lost nodes to a handful: 14–37 of 40,000 in a spot check of 5 runs, so most runs still lose at
  least one node. In 01, 03 and 10 the race recurs constantly, so protecting from the first hit
  onward is enough. The intended workflow is therefore *detect once, then run with the protect
  file*.
