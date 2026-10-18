# Algorithms

This document states the rules Thread Sentinel implements, exactly as the code applies them, and
then works through one execution by hand. File references point at the implementing code.

## 1. Notation

| Symbol | Meaning |
|---|---|
| `t`, `u` | thread ids (0 = main; children get 1, 2, … in creation order, at most 64 in total) |
| `HB_t` | happens-before vector clock of thread `t` |
| `ST_t` | structural vector clock of thread `t` |
| `V[i]` | component `i` of vector clock `V` |
| `max(A, B)` | component-wise maximum (join) |
| `LS_t` | set of locks `t` currently holds (64-bit mask; lock ids 0..63) |
| `L_m` | clock stored on mutex `m` by its last unlock (HB only) |
| `S_c` | clock accumulated on condition variable `c` by signals (HB and ST) |
| `A_b` | clock accumulated on barrier `b` by arrivals (HB and ST) |

An **epoch** of an access by `t` is the single number `HB_t[t]` (or `ST_t[t]`) at that moment.

## 2. The two clocks

Both clocks follow the standard vector-clock rules for **thread structure** and
**explicit hand-off** (fork, join, condition variables, barriers). They differ in one place:

- `HB` *also* transfers ordering through **mutexes** (unlock → later lock of the same mutex).
- `ST` ignores mutexes completely.

So `HB` describes what *did* happen on this schedule, while `ST` describes what the program
structure *guarantees* on every schedule. A mutex hand-off orders two accesses on this run, but
unless both accesses hold that mutex, a different schedule could flip the order.

## 3. Clock update rules

C means "apply to both HB and ST". All rules run under the global runtime lock.

| Event | Rule | Code |
|---|---|---|
| program start (main) | `C_0 = [1, 0, …]` | `ts_threads_init` |
| `create(t → u)` | `C_u = C_t` (snapshot), then `C_u[u]++`; afterwards `C_t[t]++` | `ts_pthread_create`, trampoline |
| `join(t ← u)` | `C_t = max(C_t, C_u)`; `C_t[t]++` | `ts_pthread_join` |
| `lock(m)` / successful `trylock(m)` | `HB_t = max(HB_t, L_m)`; `LS_t += m` | `ts_mutex_lock` |
| `unlock(m)` | `L_m = HB_t`; `HB_t[t]++`; `LS_t -= m` | `ts_mutex_unlock` |
| `signal(c)` / `broadcast(c)` | `S_c = max(S_c, C_t)`; `C_t[t]++` | `ts_cond_signal` |
| `wait(c, m)` – before blocking | unlock rule for `m` | `ts_cond_wait` |
| `wait(c, m)` – on return | `C_t = max(C_t, S_c)`; then lock rule for `m` | `ts_cond_wait` |
| `barrier_wait(b)` – arrive | `A_b = max(A_b, C_t)` | `ts_barrier_wait` |
| `barrier_wait(b)` – depart | `C_t = max(C_t, A_b)`; `C_t[t]++` | `ts_barrier_wait` |

`S_c` and `A_b` only ever grow, so a waiter inherits every earlier signal on `c` (and every earlier
barrier generation on `b`), not just the one that woke it. This over-approximates ordering: it can
hide a race (a false negative) but never invents one.

## 4. Shadow memory

Every instrumented address has a shadow entry (`src/shadow.c`, a 65,536-bucket hash table) holding:

- `last_write`: the most recent write, as an access record
  `(tid, hb_epoch, st_epoch, lockset, location)` plus full clocks for reporting;
- `last_reads[u]`: the most recent read by each thread `u` since `last_write`;
- an Eraser tracker (state + candidate lockset) used by the lockset-only mode.

## 5. Classifying a pair of accesses

Given an earlier access record `p` and the current access by `t` (with `HB_t`, `ST_t`, `LS_t`),
from different threads (`p.tid ≠ t`):

```
hb_ordered  = p.hb_epoch <= HB_t[p.tid]
st_ordered  = p.st_epoch <= ST_t[p.tid]
common_lock = (p.lockset ∩ LS_t) ≠ ∅
```

**Hybrid detector** (`evaluate_conflict` in `src/detector.c`):

| hb_ordered | st_ordered | common_lock | Verdict |
|---|---|---|---|
| no | – | no | **CONFIRMED** – the accesses really were concurrent and unprotected |
| yes | no | no | **POTENTIAL** – ordered only by an unrelated mutex on this schedule |
| – | – | yes | safe – a common lock protects them |
| yes | yes | no | safe – fork/join/condvar/barrier guarantees the order |

(`st_ordered` implies `hb_ordered`, because ST is HB minus mutex edges.)

**Pure HB mode**: CONFIRMED iff `¬hb_ordered`; locks only matter through their HB edges.

**Which pairs are checked** (`detector_process_access`):

- a **read** by `t` is checked against `last_write`, then stored in `last_reads[t]`;
- a **write** by `t` is checked against `last_write` and every `last_reads[u]` (`u ≠ t`), then the
  reads are cleared and the write becomes `last_write`.

Only the most recent write is kept, as in FastTrack-style detectors: a race with an older write that
was later overwritten by a correctly ordered write is not reported.

## 6. Lockset (Eraser) mode

`src/lockset.c`, per address:

```
VIRGIN          --any access by t-->             EXCLUSIVE(first = t)
EXCLUSIVE       --access by first-->             EXCLUSIVE
EXCLUSIVE       --read by u ≠ first-->           SHARED,           C = C ∩ LS_u
EXCLUSIVE       --write by u ≠ first-->          SHARED_MODIFIED,  C = C ∩ LS_u
SHARED          --read-->                        SHARED,           C = C ∩ LS
SHARED          --write-->                       SHARED_MODIFIED,  C = C ∩ LS
SHARED_MODIFIED --any-->                         SHARED_MODIFIED,  C = C ∩ LS
```

`C` starts as "all locks". A violation is reported when the state is `SHARED_MODIFIED` and `C = ∅`.
EXCLUSIVE→SHARED deliberately tolerates "initialise, then only read". What Eraser cannot see is a
lock-free hand-off of *writes* between threads (fork/join, barrier, condvar): demos 06 and 11 are
its false positives.

## 7. Reporting and de-duplication

A race is identified by `(address, {location₁, location₂})`, with the location pair unordered. The
first verdict recorded for that identity wins (`report.c: is_duplicate`). Each report entry keeps
both accesses' thread, location, lockset and full HB/ST clocks.

## 8. Prevention

- **Protected set.** Variables enter it by name from a previous report (`TS_PROTECT_FILE`; the
  address is bound on first access) or live, when the hybrid detector reports CONFIRMED in prevent
  mode.
- **Guard stripes.** 256 mutexes; an address maps to stripe `splitmix64(addr) mod 256`. Every
  instrumented access to a protected address holds its stripe for the duration of the access.
- **Guards are locks to the detector.** Taking a stripe gets a lock id and follows the normal
  lock/unlock clock rules, so serialised accesses are HB-ordered and share a lock. A protected
  address is also skipped by the detector, so prevent-mode reports contain no self-inflicted races.
- **Regions.** `TS_REGION_BEGIN(n, &a, &b, …)` collects the stripes of the protected variables
  listed, de-duplicates them and locks them in **ascending stripe order**, so two regions can never
  deadlock on each other (a total order on lock acquisition). `TS_REGION_END()` releases them in
  reverse. Inside a region, accesses whose stripe is already held do not re-lock it (per-thread
  hold depth).
- **Limitation.** A region must name every protected variable it touches. Touching another one
  takes that stripe out of order.

## 9. Worked example: `07_lucky_lock_order`

```c
// T1                              // T2
TS_WRITE(x, 42);        // line 39  usleep(10 ms);
lock(m); unlock(m);                 lock(m); unlock(m);
                                    TS_WRITE(x, 99);   // line 59
```

Clocks are written `[main, T1, T2]`. HB and ST are equal until the first mutex event.

| Step | Event | HB | ST | Notes |
|---|---|---|---|---|
| 0 | main starts | main `[1,0,0]` | main `[1,0,0]` | |
| 1 | create T1 | T1 `[1,1,0]`; main `[2,0,0]` | same | child = snapshot + own tick |
| 2 | create T2 | T2 `[2,0,1]`; main `[3,0,0]` | same | |
| 3 | T1 writes `x` | T1 `[1,1,0]` | T1 `[1,1,0]` | record `p = (T1, hb 1, st 1, {}, :39)` |
| 4 | T1 lock m | T1 `[1,1,0]` | – | `L_m` empty, no change; `LS = {m}` |
| 5 | T1 unlock m | `L_m = [1,1,0]`; T1 `[1,2,0]` | T1 `[1,1,0]` | ST ignores the mutex |
| 6 | T2 lock m | T2 `max([2,0,1],[1,1,0]) = [2,1,1]` | T2 `[2,0,1]` | HB inherits T1's past |
| 7 | T2 unlock m | `L_m = [2,1,1]`; T2 `[2,1,2]` | T2 `[2,0,1]` | |
| 8 | T2 writes `x` | T2 `[2,1,2]` | T2 `[2,0,1]` | check against `p` |

Step 8, checking `p` (T1's write):

```
hb_ordered  = p.hb_epoch (1) <= HB_T2[T1] (1)   -> true
st_ordered  = p.st_epoch (1) <= ST_T2[T1] (0)   -> false
common_lock = {} ∩ {}                           -> false
=> POTENTIAL: "ordered only by luck through unrelated lock, no common lock"
```

These are exactly the clocks in the runtime's report (`first_access.hb = [1,1]`,
`second_access.hb = [2,1,2]`, `second_access.st = [2,0,1]`). Pure HB stops at
`hb_ordered = true` and reports nothing; ThreadSanitizer behaves the same way. Had T2 run first,
the lock hand-off would have been reversed and the writes would be concurrent. The hybrid detector
reports that possibility from this single run.

For contrast, in `01_counter_race` two workers created back to back increment `counter`. T2's
clock is `[2,0,1]` and T1's epoch is 1, but `HB_T2[T1] = 0`, so `hb_ordered` is false, no lock is
held, and the pair is CONFIRMED.
