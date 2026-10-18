# Project Report Outline

A chapter-by-chapter skeleton for the written report, with what each section should contain and
which repository artefact supplies it. Suggested length is 30–40 pages plus appendices.

## Front matter
- Title page, declaration, certificate, acknowledgements
- **Abstract** (≈250 words): the problem (data races; the weaknesses of lockset and of
  happens-before), the contribution (a dual-clock hybrid detector with CONFIRMED/POTENTIAL
  verdicts, plus runtime prevention), the method (C runtime library, 12 demos, comparison with
  TSan), and the headline results (hybrid 7/7 races with 0 false positives; ≈11× geometric-mean
  overhead against TSan's ≈13×; 0/120 failing runs with prevention against 80/120 without).
- Table of contents, list of figures, list of tables, abbreviations (HB, ST, VC, TSan, CVD, …)

## 1. Introduction (3–4 pages)
1.1 Motivation: why races are hard (non-determinism, Heisenbugs, undefined behaviour in C11)
1.2 Problem statement: the false positives of lockset and the false negatives of pure HB
1.3 Objectives: detect, classify by confidence, prevent, and evaluate against an industry tool
1.4 Scope and assumptions: POSIX threads, C, manual annotation, at most 64 threads and 64 locks
1.5 Report organisation

## 2. Background and Literature Review (6–8 pages)
2.1 Data races, atomicity violations and the C11 memory model
2.2 Lamport happens-before and vector clocks (Lamport 1978; Fidge / Mattern 1988)
2.3 Lockset analysis: Eraser (Savage et al., 1997) and its state machine
2.4 Hybrid detectors: O'Callahan & Choi (2003), MultiRace
2.5 Epoch optimisation: FastTrack (Flanagan & Freund, 2009)
2.6 Production tools: ThreadSanitizer v2, Helgrind, Intel Inspector
2.7 Race prevention and avoidance: lock insertion, deterministic execution, Kivati, AVIO
2.8 Gap this project addresses → table comparing approaches *(source: README §1)*

## 3. Requirements and Design (5–6 pages)
3.1 Functional requirements (detect modes, verdicts, prevention, reports, traces, CLI)
3.2 Non-functional requirements (zero-cost baseline build, warning-free C11, deterministic tests)
3.3 Architecture overview → **Figure: component diagram** *(docs/ARCHITECTURE.md §1)*
3.4 Data model: thread table, sync records, shadow entries, report entries
3.5 Key design decisions and alternatives considered
   - dual clocks vs. a single clock plus lockset
   - manual macros vs. compiler / binary instrumentation
   - striped guards vs. per-variable or global locks
   - one global runtime lock (simplicity) vs. fine-grained locking (scalability)

## 4. Algorithms (6–8 pages)
4.1 Clock update rules → **Table** *(docs/ALGORITHMS.md §3)*
4.2 Shadow memory and which access pairs are checked
4.3 Classification rule (CONFIRMED / POTENTIAL / safe), with a correctness argument (ST ⊑ HB)
4.4 Eraser lockset mode
4.5 Prevention: protected set, stripes, regions, ascending-order deadlock avoidance (proof sketch)
4.6 **Worked example**: demo 07 step by step *(docs/ALGORITHMS.md §10)*

## 5. Implementation (5–6 pages)
5.1 Technology and build (C11, pthreads, gcc flags, `-DTS_DISABLE`, static/shared library)
5.2 Interception: macro renaming, the trampoline for thread creation, constructor/destructor
5.3 Thread safety of the runtime (global recursive lock; lock-free protected-set check)
5.4 Reporting (de-duplication, JSON schema) and trace format; trace compaction and why it is sound
5.5 Python tooling: CLI, replay engine, HTML report, benchmark runner
5.6 Challenges and how they were solved (pick 3–4; all are recorded in PROGRESS.md "Deviations"):
   self-deadlock on re-entrant guard stripes, unbalanced unlocks from live protection,
   `-O2` hoisting a racy spin loop, TSan vs. ASLR, 350 MB traces

## 6. Testing and Verification (3–4 pages)
6.1 Unit tests (C) per module
6.2 Demo suite with ground truth for three detector modes → **Table: the 12 demos**
6.3 Schedule independence (5 runs each) and 20-run prevention tests
6.4 Differential testing: offline replay must match the runtime's race set exactly
6.5 Memory safety: Valgrind on every demo (`make valgrind`)

## 7. Evaluation (6–8 pages) *(source: docs/EVALUATION.md)*
7.1 Experimental setup and methodology
7.2 Detection accuracy: verdict table and precision/recall per tool
7.3 Runtime overhead → **Figure: overhead chart**, geometric means, explanation of outliers
7.4 Scalability → **Figure: thread-scaling chart**; the global-lock bottleneck
7.5 Prevention effectiveness: protect-file vs. live; why live fails on 04
7.6 Threats to validity: a small, purpose-built demo set; a WSL2 laptop; manual annotation; process
    start-up dominating short runs

## 8. Conclusion and Future Work (2 pages)
8.1 Summary of contributions and objectives met
8.2 Limitations *(docs/ARCHITECTURE.md §9)*
8.3 Future work: compiler instrumentation, sharded runtime lock, per-generation barrier clocks,
    deadlock detection, `LD_PRELOAD` shim

## References
IEEE style. At minimum: Lamport 1978; Mattern 1988; Fidge 1988; Savage et al. 1997 (Eraser);
O'Callahan & Choi 2003; Flanagan & Freund 2009 (FastTrack); Serebryany & Iskhodzhanov 2009
(ThreadSanitizer); ISO/IEC 9899:2011 (C11).

## Appendices
- A. User manual: build, environment variables, CLI *(README.md)*
- B. Public API reference *(include/sentinel.h)*
- C. JSON report and JSONL trace formats with examples
- D. Full benchmark tables *(results/bench/)*
- E. Viva preparation notes *(docs/VIVA_QA.md)*
