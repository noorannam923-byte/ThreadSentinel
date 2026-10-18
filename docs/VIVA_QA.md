# Viva Questions & Answers

Short answers to the questions an examiner is most likely to ask, with pointers to where the
evidence lives. Numbers come from [EVALUATION.md](EVALUATION.md) (run of 2026-10-18).

## Concepts

**1. What is a data race?**
Two accesses to the same memory location from different threads, at least one a write, with no
synchronisation ordering them. In C11 a data race is undefined behaviour, so the compiler may
assume it never happens.

**2. Is every data race a bug? Is every concurrency bug a data race?**
Neither. A race on a "hint" variable can be benign in effect (demo 05: the unlocked read of `count`
never corrupts the result), though it is still undefined behaviour. Conversely, an atomicity
violation can happen with every access locked: check-then-act where the check and the act take
the lock separately. Demos 04 and 12 are atomicity bugs *built from* races, which is why
prevention needs regions for them.

**3. What is the lockset algorithm and what goes wrong with it?**
Eraser assumes every shared variable is consistently protected by some lock. It keeps a candidate
set `C(v)`, intersects it with the locks held at each access, and complains when `C(v)` becomes
empty for write-shared data. It produces false positives for code ordered by fork/join, barriers
or condvar hand-offs, which need no common lock (demos 06 and 11).

**4. What is happens-before and what goes wrong with it?**
Lamport's partial order: program order plus synchronisation edges (unlock→lock, fork, join,
signal→wait). Two accesses race if neither happens before the other. It is precise for the
*observed* schedule, so it misses races that were ordered by luck on this run: in demo 07 an
unrelated mutex happened to be released by T1 before T2 took it.

**5. What is a vector clock and why use one?**
One logical counter per thread. `V_t[u]` is the latest event of thread `u` that `t` knows about.
`a → b` iff `V_a ≤ V_b` component-wise. A vector clock (not a scalar Lamport clock) is needed
because it can also tell when two events are *concurrent*.

**6. What is an epoch and why compare epochs instead of full clocks?**
An access by `t` is summarised by `(t, V_t[t])`. It happened before the current access by `u` iff
`V_t[t] ≤ V_u[t]`. That is one integer comparison instead of 64 (the FastTrack insight), and it is
exact for this question.

## Design

**7. What is the core idea of Thread Sentinel?**
Keep two vector clocks per thread. `HB` follows all synchronisation including mutexes. `ST`
(structural) follows only fork/join, condition variables and barriers. Combine them with locksets:
not HB-ordered and no common lock → **CONFIRMED**; HB-ordered but not ST-ordered and no common
lock → **POTENTIAL**; otherwise safe.

**8. Why does ST ignore mutexes?**
A mutex orders two critical sections in *whichever order they happened to run*. If the accesses
being compared aren't both inside that mutex, the order isn't guaranteed by the program. Fork,
join, barriers and condvar hand-offs do encode an order the program relies on.

**9. Then why not just use lockset plus ST, and drop HB?**
HB gives the CONFIRMED/POTENTIAL distinction: whether the race actually happened concurrently on
this run, or only could have. That drives severity and the prevention policy (only CONFIRMED races
trigger live protection).

**10. Walk me through demo 07.**
See [ALGORITHMS.md §9](ALGORITHMS.md#9-worked-example-07_lucky_lock_order). At T2's write:
`hb_ordered` is true (1 ≤ HB[T1] = 1, inherited through the mutex), `st_ordered` is false
(1 ≤ ST[T1] = 0 fails), no common lock → POTENTIAL. Pure HB and TSan report nothing.

**11. How do condition variables and barriers update the clocks?**
Signal joins the signaller's clocks into the condvar's clock; a returning waiter joins it. Barrier
arrivals accumulate into the barrier's clock and every departing thread joins it. Both are applied
to HB and ST because they are intentional hand-offs. The accumulated clocks never reset, which
over-approximates ordering (possible false negatives, never false positives).

**12. What does the shadow memory store?**
Per address: the last write and the last read of each thread since that write, each as
(tid, HB epoch, ST epoch, lockset, source location), plus Eraser state. A read is checked against
the last write; a write against the last write and all those reads.

**13. Why is the instrumentation manual (`TS_READ`/`TS_WRITE`) instead of compiler-based?**
Scope and transparency for a mini-project: no compiler plugin, and every checked access is visible
in the source. Compiler instrumentation (what TSan uses) would remove the annotation burden; it is
the main item of future work. The `TS_DISABLE` build gives a zero-cost baseline from the same
source.

**14. How are `pthread_*` calls intercepted?**
`sentinel.h` renames them with macros (`#define pthread_mutex_lock ts_mutex_lock`, …) in user
code. The wrapper calls the real function, then updates the clocks under the runtime lock. An
`LD_PRELOAD` shim is listed as a stretch goal.

## Prevention

**15. How does prevention work?**
Racy variables are put in a protected set: from a previous report (`TS_PROTECT_FILE`) or live, on
the first CONFIRMED race. Each protected address hashes to one of 256 guard mutexes ("stripes"),
which is held for the duration of every instrumented access to it. That serialises the accesses.

**16. Why stripes instead of one lock per variable, or one global lock?**
One global lock serialises unrelated variables; one lock per variable needs dynamic allocation and
a lookup structure on the hot path. A fixed array of 256 mutexes indexed by a hash costs one
multiply-xor-shift and no allocation. Two variables can share a stripe, which costs some extra
contention but is always safe.

**17. Why isn't per-access locking enough for demos 04 and 12?**
They read, decide, then write. Locking the read and the write separately still lets another thread
slip in between. `TS_REGION_BEGIN(n, &a, &b)` holds the stripes for the whole compound operation.

**18. Could regions deadlock?**
Not with each other: a region sorts its stripes and locks them in ascending order, which imposes a
global order. Re-locking a stripe the thread already holds is handled by a per-thread hold depth.
The remaining hazard is a region touching a protected variable it did not list; that is documented
as a limitation.

**19. Why does live prevention fail on demo 04?**
Protection starts *after* the first race is detected. All eight threads pass the unprotected
`if (!initialized)` check within microseconds, before the detector sees the conflicting write. Live
mode helps when a race recurs (01, 03, 10: 0/20 wrong), not when the damage is done in the first
interleaving. Hence the recommended workflow: detect once, then run with the protect file
(0/120 wrong).

**20. Doesn't the guard lock confuse the detector?**
No. The guard is registered as an ordinary lock: it updates the lockset and the HB clock, so the
serialised accesses are correctly seen as ordered and protected. Protected addresses are also
skipped by the detector, so prevent mode reports no self-inflicted races.

## Implementation & verification

**21. How is the runtime itself kept thread-safe?**
One recursive global mutex around all clock, shadow, lockset and report state. The only lock-free
path is the "is this address protected?" check in the pre-hook, which uses atomic acquire/release
on the list count and a CAS to bind names to addresses.

**22. How do you know the detector is correct?**
- C unit tests for vector clocks, threads, locksets, shadow memory and the detector.
- 12 demos with ground-truth verdicts for all three detector modes (`demos/expected/`), checked by
  pytest. The hybrid verdict is checked over 5 runs for schedule independence.
- An independent Python re-implementation (`replay.py`) re-classifies the JSONL trace and must
  produce *exactly* the same race set as the C runtime, for every demo, under hybrid and HB.
- 20/20-run prevention tests, Valgrind on every demo (`make valgrind`), and a TSan comparison.

**23. What is trace compaction and why is it safe?**
Full traces were 350 MB for one demo. After thread `t` writes `x`, and until another thread touches
`x`, every further access by `t` is compared only with `t`'s own records, so it cannot produce a
race. Those events are deferred and only the final state is written. Replay over the compacted
trace gives the same race set, which the replay tests confirm.

**24. How does your overhead compare to ThreadSanitizer?**
Geometric-mean slowdown is about 11× for Sentinel versus about 13× for TSan across the 12 demos.
The spread is wide: Sentinel is worst on tight loops of instrumented accesses (≈470× on demo 01,
whose plain build gcc folds into one add), and TSan is worst on lock-heavy code (≈150–190× on 03
and 10).

**25. How does it scale with threads?**
Slowdown versus plain stays roughly constant (about 30–65×) from 4 to 16 threads, but Sentinel cannot use the
extra cores: the global runtime lock serialises every instrumented access. Sharding that lock is
the main performance work left.

**26. Why did TSan need `setarch -R`?**
gcc 13's TSan runtime expects a particular address-space layout and aborts with "unexpected memory
mapping" on kernels using 32-bit mmap randomisation (this WSL2 kernel). Disabling ASLR for the
process restores the expected layout. Detection is unaffected.

**27. Why does demo 01 show no lost updates in the plain build?**
With the race being undefined behaviour, gcc -O2 legally turns each thread's 500,000-iteration
loop into a single `addq $500000, counter`, so there is almost no window for interleaving. The
instrumented build keeps every access, so the race shows (20/20). It is a good example of why
"the test passed" proves nothing about a race.

## Limitations & future work

**28. What can Thread Sentinel not detect?**
Accesses that are not annotated; races with an older write that was later overwritten by an
ordered write (only the last write is kept); races hidden by the accumulating condvar/barrier
clocks; anything beyond 64 threads or 64 locks. It detects data races, not deadlocks or atomicity
violations in general (deadlock detection via a lock-order graph is a planned stretch goal).

**29. Is POTENTIAL ever a false alarm?**
It can be, when the "unrelated" mutex actually encodes a real protocol: for example, a flag set
under mutex `m` that the program waits on before touching the data. Then the order is guaranteed
even though the data accesses are not inside `m`. That is why POTENTIAL is reported at MEDIUM
severity and does not trigger live protection.

**30. What would you do with more time?**
Compiler-pass instrumentation (or binary instrumentation) instead of macros; sharding the runtime
lock; resetting barrier/condvar clocks per generation; reusing thread slots; lock-order deadlock
detection; and the `LD_PRELOAD` interception shim.
