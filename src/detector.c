#include "detector.h"
#include "sentinel_internal.h"
#include "shadow.h"
#include "threads.h"
#include "lockset.h"
#include "vector_clock.h"
#include "report.h"
#include "trace.h"
#include "prevention.h"
#include <stdio.h>
#include <string.h>

void detector_init(void) {
}

void detector_shutdown(void) {
}

/*
 * Trace compaction. Once thread t has written an address and no other thread has
 * touched it since, last_write is t's own and no other thread's reads remain, so every
 * further access by t is checked only against t's own records and cannot report a
 * race. Those accesses are not written as they happen; the entry's final records are
 * written when another thread arrives (or at free / shutdown). The replayed result is
 * identical, but a long same-thread run costs at most two trace lines.
 */
static void trace_emit_record(const ts_shadow_entry_t *entry, const ts_access_record_t *rec, bool is_write) {
    trace_record_event(rec->tid, is_write ? "write" : "read", (const void *)entry->addr, -1,
                       entry->var_name, rec->loc, rec->lockset, &rec->hb_clock, &rec->st_clock);
}

void detector_flush_trace_entry(ts_shadow_entry_t *entry) {
    if (entry->trace_pending_write) {
        trace_emit_record(entry, &entry->last_write, true);
    }
    if (entry->trace_pending_read) {
        trace_emit_record(entry, &entry->last_reads[entry->trace_run_tid], false);
    }
    entry->trace_pending_write = false;
    entry->trace_pending_read = false;
}

void detector_flush_trace(void) {
    if (trace_is_enabled()) shadow_for_each(detector_flush_trace_entry);
}

static void evaluate_conflict(ts_shadow_entry_t *entry,
                              const ts_access_record_t *p,
                              const char *p_op,
                              const char *cur_op,
                              uint32_t cur_tid,
                              uint64_t cur_locks,
                              const char *cur_loc,
                              const vector_clock_t *cur_hb,
                              const vector_clock_t *cur_st) {
    if (!p->valid || p->tid == cur_tid) return;
    
    /* 1. HB ordering check */
    bool hb_ordered = vc_happens_before_epoch(p->tid, p->hb_epoch, cur_hb);
    
    /* 2. Structural clock check */
    bool st_ordered = vc_happens_before_epoch(p->tid, p->st_epoch, cur_st);
    
    /* 3. Common lock check */
    bool common_lock = (p->lockset & cur_locks) != 0;
    
    race_verdict_t verdict = RACE_NONE;
    const char *reason = "";
    
    if (g_ts_config.detector == TS_DETECTOR_HB) {
        if (!hb_ordered) {
            verdict = RACE_CONFIRMED;
            reason = "not ordered by happens-before";
        }
    } else { /* TS_DETECTOR_HYBRID */
        if (!hb_ordered && !common_lock) {
            verdict = RACE_CONFIRMED;
            reason = "not ordered by happens-before, no common lock";
            if (g_ts_config.mode == TS_MODE_PREVENT) {
                prevention_add_protected((const void *)entry->addr, entry->var_name, cur_loc);
            }
        } else if (hb_ordered && !st_ordered && !common_lock) {
            verdict = RACE_POTENTIAL;
            reason = "ordered only by luck through unrelated lock, no common lock";
        }
    }
    
    if (verdict != RACE_NONE) {
        race_report_t r;
        memset(&r, 0, sizeof(r));
        r.verdict = verdict;
        r.addr = entry->addr;
        snprintf(r.var_name, sizeof(r.var_name), "%s", entry->var_name);
        
        r.op1 = p_op;
        r.t1 = p->tid;
        snprintf(r.loc1, sizeof(r.loc1), "%s", p->loc);
        r.locks1 = p->lockset;
        vc_copy(&r.hb1, &p->hb_clock);
        vc_copy(&r.st1, &p->st_clock);
        
        r.op2 = cur_op;
        r.t2 = cur_tid;
        snprintf(r.loc2, sizeof(r.loc2), "%s", cur_loc);
        r.locks2 = cur_locks;
        vc_copy(&r.hb2, cur_hb);
        vc_copy(&r.st2, cur_st);
        
        snprintf(r.reason, sizeof(r.reason), "%s", reason);
        
        report_record_race(&r);
        trace_record_event(cur_tid, "race", (const void *)entry->addr, -1,
                           entry->var_name, cur_loc, cur_locks, cur_hb, cur_st);
    }
}

void detector_process_access(const void *addr, const char *var_name, const char *loc, bool is_write) {
    uint32_t tid = ts_get_tid();
    ts_thread_info_t *tinfo = ts_get_thread_info(tid);
    uint64_t held_locks = tinfo->locks_held;
    
    ts_shadow_entry_t *entry = shadow_get_or_create(addr, var_name);
    
    /* Update Eraser state machine */
    lockset_update(&entry->eraser, tid, is_write, held_locks);
    
    /* Compaction relies on the shadow records being updated below, so only in detect mode */
    bool compactable = trace_is_enabled() &&
                       g_ts_config.mode == TS_MODE_DETECT &&
                       g_ts_config.detector != TS_DETECTOR_LOCKSET;
    bool same_run = compactable && entry->trace_run_valid && entry->trace_run_tid == tid;
    const ts_access_record_t *own_read = &entry->last_reads[tid];
    /* A re-read with the same epochs, locks and location can only re-find races already
     * reported (same location pair), and leaves an identical record behind. */
    bool repeat_read = same_run && !is_write && own_read->valid &&
                       own_read->hb_epoch == tinfo->hb_clock.clock[tid] &&
                       own_read->st_epoch == tinfo->st_clock.clock[tid] &&
                       own_read->lockset == held_locks &&
                       strcmp(own_read->loc, loc) == 0;
    if (same_run && entry->trace_run_written) {
        if (is_write) {
            entry->trace_pending_write = true;
            entry->trace_pending_read = false; /* the write clears the reads */
        } else {
            entry->trace_pending_read = true;
        }
    } else if (repeat_read) {
        /* already traced */
    } else {
        detector_flush_trace_entry(entry);
        if (!entry->trace_run_valid || entry->trace_run_tid != tid) {
            entry->trace_run_valid = true;
            entry->trace_run_tid = tid;
            entry->trace_run_written = false;
        }
        trace_record_event(tid, is_write ? "write" : "read", addr, -1,
                           var_name, loc, held_locks, &tinfo->hb_clock, &tinfo->st_clock);
    }
    if (is_write) entry->trace_run_written = true;
                       
    if (g_ts_config.mode == TS_MODE_OFF) return;
    
    /* In prevent mode, protected addresses cannot race (guard lock serialises them).
     * Skip detection to avoid false positives in the report. */
    if (g_ts_config.mode == TS_MODE_PREVENT &&
        prevention_is_protected(addr, var_name, NULL)) {
        return;
    }
    
    /* Check pure lockset mode */
    if (g_ts_config.detector == TS_DETECTOR_LOCKSET) {
        if (lockset_has_violation(&entry->eraser)) {
            race_report_t r;
            memset(&r, 0, sizeof(r));
            r.verdict = RACE_CONFIRMED;
            r.addr = entry->addr;
            snprintf(r.var_name, sizeof(r.var_name), "%s", entry->var_name);
            
            r.op1 = "ACCESS";
            r.t1 = entry->eraser.first_tid;
            snprintf(r.loc1, sizeof(r.loc1), "init/exclusive");
            r.locks1 = 0;
            vc_init(&r.hb1);
            vc_init(&r.st1);
            
            r.op2 = is_write ? "WRITE" : "READ";
            r.t2 = tid;
            snprintf(r.loc2, sizeof(r.loc2), "%s", loc);
            r.locks2 = held_locks;
            vc_copy(&r.hb2, &tinfo->hb_clock);
            vc_copy(&r.st2, &tinfo->st_clock);
            
            snprintf(r.reason, sizeof(r.reason),
                     "Eraser lockset violation: shared-modified with empty candidate lockset");
            report_record_race(&r);
        }
        return;
    }
    
    /* HB or Hybrid detector */
    if (!is_write) {
        /* Read conflicts with previous write */
        evaluate_conflict(entry, &entry->last_write, "WRITE", "READ",
                          tid, held_locks, loc, &tinfo->hb_clock, &tinfo->st_clock);
                          
        /* Record this read */
        entry->last_reads[tid].valid = true;
        entry->last_reads[tid].tid = tid;
        entry->last_reads[tid].hb_epoch = tinfo->hb_clock.clock[tid];
        entry->last_reads[tid].st_epoch = tinfo->st_clock.clock[tid];
        entry->last_reads[tid].lockset = held_locks;
        vc_copy(&entry->last_reads[tid].hb_clock, &tinfo->hb_clock);
        vc_copy(&entry->last_reads[tid].st_clock, &tinfo->st_clock);
        snprintf(entry->last_reads[tid].loc, sizeof(entry->last_reads[tid].loc), "%s", loc);
    } else {
        /* Write conflicts with previous write */
        evaluate_conflict(entry, &entry->last_write, "WRITE", "WRITE",
                          tid, held_locks, loc, &tinfo->hb_clock, &tinfo->st_clock);
                          
        /* Write conflicts with all concurrent reads since previous write */
        for (uint32_t u = 0; u < TS_MAX_THREADS; u++) {
            if (entry->last_reads[u].valid && u != tid) {
                evaluate_conflict(entry, &entry->last_reads[u], "READ", "WRITE",
                                  tid, held_locks, loc, &tinfo->hb_clock, &tinfo->st_clock);
            }
        }
        
        /* Reset reads array and record current write */
        for (uint32_t u = 0; u < TS_MAX_THREADS; u++) {
            entry->last_reads[u].valid = false;
        }
        
        entry->last_write.valid = true;
        entry->last_write.tid = tid;
        entry->last_write.hb_epoch = tinfo->hb_clock.clock[tid];
        entry->last_write.st_epoch = tinfo->st_clock.clock[tid];
        entry->last_write.lockset = held_locks;
        vc_copy(&entry->last_write.hb_clock, &tinfo->hb_clock);
        vc_copy(&entry->last_write.st_clock, &tinfo->st_clock);
        snprintf(entry->last_write.loc, sizeof(entry->last_write.loc), "%s", loc);
    }
}
