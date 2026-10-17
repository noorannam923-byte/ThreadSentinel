# Thread Sentinel

**Hybrid Race Detection and Runtime Prevention using Vector Clocks and Lockset Analysis**

> An Operating Systems mini-project providing lightweight, dynamic data race detection and automatic runtime serialization for POSIX multi-threaded applications in C.

---

## 1. Problem Statement

When multiple threads concurrently access the same memory location without synchronization, and at least one access is a write, a **data race** occurs. Data races cause non-deterministic bugs (such as lost updates and memory corruption) that are notoriously difficult to reproduce and debug.

Two classic race detection techniques each suffer from notable weaknesses:

| Technique | Core Concept | Inherent Weakness |
|---|---|---|
| **Lockset Analysis (Eraser)** | Assumes every shared variable must be consistently guarded by a common mutex lock. | **High False Positives**: Flags safe code synchronized by structural mechanisms like `fork/join`, barriers, or condition variable handoffs. |
| **Happens-Before (Vector Clocks)** | Verifies whether conflicting memory accesses are ordered by a causal timeline. | **False Negatives / Misses**: Fails to detect potential races if accesses were accidentally serialized by an unrelated lock during a particular schedule execution. |

**Thread Sentinel** combines both techniques into a **hybrid detector**:
1. Uses dual vector clocks (`HB` happens-before and `ST` structural clocks) alongside Eraser lockset tracking.
2. Distinguishes **CONFIRMED RACES** (true concurrent conflicts) from **POTENTIAL RACES** (ordered only by lucky lock schedules) and **SAFE** synchronizations.
3. Automatically **prevents** detected data races at runtime via striped guard locks with lock-ordering deadlock avoidance.

---

## 2. Quick Start

### Prerequisites
- Linux (Ubuntu 22.04/24.04 or WSL2)
- GCC with C11 and POSIX threads support
- Make
- Python 3.10+

### Setup & Build
```bash
# Set up Python virtual environment and install tools
make setup

# Build static & shared libraries and instrumented demos
make

# Build baseline binaries (plain and ThreadSanitizer)
make baseline

# Run all unit and integration tests
make test
```

---

## 3. Project Status
See [PROGRESS.md](PROGRESS.md) for current implementation phase status and verification checklists.
