"""
replay.py — offline Python replay engine for Thread Sentinel JSONL traces.

Reads a .jsonl trace produced by TS_TRACE=<path> and re-runs the hybrid race
classification in pure Python, as an independent implementation of the rules in
src/detector.c, so its verdicts can be checked against the C runtime's report.

Each access event carries the accessing thread's happens-before clock (hb),
structural clock (st) and held locks exactly as the runtime saw them. The replay
rebuilds the shadow memory (last write + reads since it, per address) from those
events and re-applies:

    CONFIRMED  not hb-ordered and no common lock
    POTENTIAL  hb-ordered, not st-ordered, no common lock   (hybrid only)

with the same de-duplication as src/report.c (address + unordered location pair,
first verdict wins). Synchronisation events are informational: their effect is
already folded into the clocks recorded on each access.

The runtime compacts the trace by not writing same-thread accesses that cannot
produce a race (see detector.c); those events never trigger a check, so
replaying the compacted trace gives the same result as replaying a full one.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple


@dataclass
class AccessRecord:
    tid: int
    hb_epoch: int
    st_epoch: int
    lockset: int
    loc: str


@dataclass
class ShadowEntry:
    addr: str
    var_name: str
    last_write: Optional[AccessRecord] = None
    last_reads: Dict[int, AccessRecord] = field(default_factory=dict)


@dataclass
class Race:
    verdict: str  # "CONFIRMED" or "POTENTIAL"
    addr: str
    var_name: str
    reason: str
    t1: int
    op1: str
    loc1: str
    t2: int
    op2: str
    loc2: str

    def key(self) -> Tuple[str, frozenset, str]:
        return (self.addr, frozenset((self.loc1, self.loc2)), self.verdict)


def _clock_at(clock: List[int], tid: int) -> int:
    return clock[tid] if tid < len(clock) else 0


def _lockset(locks: List[int]) -> int:
    mask = 0
    for lid in locks:
        mask |= 1 << lid
    return mask


class ReplayEngine:
    """Offline replay of a Thread Sentinel JSONL trace."""

    SUPPORTED_DETECTORS = ("hybrid", "hb")

    def __init__(self, detector: str = "hybrid"):
        if detector not in self.SUPPORTED_DETECTORS:
            raise ValueError(f"replay supports detectors {self.SUPPORTED_DETECTORS}, not '{detector}'")
        self.detector = detector
        self.shadow: Dict[str, ShadowEntry] = {}
        self.races: List[Race] = []
        self._race_keys: Set[Tuple[str, frozenset]] = set()
        self.events = 0
        self.accesses = 0

    def _record_race(self, verdict: str, entry: ShadowEntry, p: AccessRecord, p_op: str,
                     cur_tid: int, cur_op: str, cur_loc: str, reason: str) -> None:
        # Same rule as report.c is_duplicate(): address + unordered location pair
        key = (entry.addr, frozenset((p.loc, cur_loc)))
        if key in self._race_keys:
            return
        self._race_keys.add(key)
        self.races.append(Race(verdict=verdict, addr=entry.addr, var_name=entry.var_name,
                               reason=reason, t1=p.tid, op1=p_op, loc1=p.loc,
                               t2=cur_tid, op2=cur_op, loc2=cur_loc))

    def _evaluate(self, entry: ShadowEntry, p: Optional[AccessRecord], p_op: str,
                  cur_tid: int, cur_op: str, cur_loc: str,
                  cur_hb: List[int], cur_st: List[int], cur_locks: int) -> None:
        if p is None or p.tid == cur_tid:
            return
        hb_ordered = p.hb_epoch <= _clock_at(cur_hb, p.tid)
        st_ordered = p.st_epoch <= _clock_at(cur_st, p.tid)
        common_lock = (p.lockset & cur_locks) != 0

        if self.detector == "hb":
            if not hb_ordered:
                self._record_race("CONFIRMED", entry, p, p_op, cur_tid, cur_op, cur_loc,
                                  "not ordered by happens-before")
        elif not hb_ordered and not common_lock:
            self._record_race("CONFIRMED", entry, p, p_op, cur_tid, cur_op, cur_loc,
                              "not ordered by happens-before, no common lock")
        elif hb_ordered and not st_ordered and not common_lock:
            self._record_race("POTENTIAL", entry, p, p_op, cur_tid, cur_op, cur_loc,
                              "ordered only by luck through unrelated lock, no common lock")

    def _process_access(self, ev: dict, is_write: bool) -> None:
        tid = ev.get("t", 0)
        addr = ev.get("obj") or ev.get("var", "")
        loc = ev.get("loc", "")
        hb = ev.get("hb", [])
        st = ev.get("st", [])
        locks = _lockset(ev.get("locks", []))

        entry = self.shadow.get(addr)
        if entry is None:
            entry = self.shadow[addr] = ShadowEntry(addr=addr, var_name=ev.get("var", ""))

        rec = AccessRecord(tid=tid, hb_epoch=_clock_at(hb, tid), st_epoch=_clock_at(st, tid),
                           lockset=locks, loc=loc)
        if not is_write:
            self._evaluate(entry, entry.last_write, "WRITE", tid, "READ", loc, hb, st, locks)
            entry.last_reads[tid] = rec
        else:
            self._evaluate(entry, entry.last_write, "WRITE", tid, "WRITE", loc, hb, st, locks)
            for u, prev_read in entry.last_reads.items():
                if u != tid:
                    self._evaluate(entry, prev_read, "READ", tid, "WRITE", loc, hb, st, locks)
            entry.last_reads.clear()
            entry.last_write = rec

    def process_event(self, ev: dict) -> None:
        """Process one JSONL event dict."""
        self.events += 1
        kind = ev.get("ev", "")
        if kind == "read" or kind == "write":
            self.accesses += 1
            self._process_access(ev, kind == "write")
        # Everything else (lock, unlock, cond_*, barrier*, guard_*, protect, race) is
        # either already reflected in the recorded clocks or is the runtime's own output.

    def confirmed_count(self) -> int:
        return sum(1 for r in self.races if r.verdict == "CONFIRMED")

    def potential_count(self) -> int:
        return sum(1 for r in self.races if r.verdict == "POTENTIAL")

    def race_keys(self) -> Set[Tuple[str, frozenset, str]]:
        return {r.key() for r in self.races}

    def summary_dict(self) -> dict:
        return {
            "events": self.events,
            "accesses": self.accesses,
            "confirmed_races": self.confirmed_count(),
            "potential_races": self.potential_count(),
            "total_unique_races": len(self.races),
            "races": [
                {"verdict": r.verdict, "address": r.addr, "variable": r.var_name,
                 "reason": r.reason,
                 "thread1": r.t1, "op1": r.op1, "loc1": r.loc1,
                 "thread2": r.t2, "op2": r.op2, "loc2": r.loc2}
                for r in self.races
            ],
        }


def report_race_keys(report: dict) -> Set[Tuple[str, frozenset, str]]:
    """Race identities from a C runtime JSON report, comparable with ReplayEngine.race_keys()."""
    return {
        (r["address"], frozenset((r["first_access"]["location"], r["second_access"]["location"])),
         r["verdict"])
        for r in report.get("races", [])
    }


def replay_trace(trace_path: str, detector: str = "hybrid") -> ReplayEngine:
    """Load and replay all events from a JSONL trace file."""
    engine = ReplayEngine(detector=detector)
    with open(trace_path, "r") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                ev = json.loads(line)
            except json.JSONDecodeError:
                continue  # Skip a truncated final line
            engine.process_event(ev)
    return engine
