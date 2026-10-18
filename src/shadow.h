#ifndef SHADOW_H
#define SHADOW_H

#include "sentinel_internal.h"
#include "lockset.h"
#include "vector_clock.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t tid;
    uint32_t hb_epoch;
    uint32_t st_epoch;
    uint64_t lockset;
    vector_clock_t hb_clock;
    vector_clock_t st_clock;
    char loc[128];
    bool valid;
} ts_access_record_t;

typedef struct ts_shadow_entry {
    uintptr_t addr;
    char var_name[64];
    
    /* Last write information */
    ts_access_record_t last_write;
    
    /* Reads since last write: indexed by thread id */
    ts_access_record_t last_reads[TS_MAX_THREADS];
    
    /* Eraser lockset tracker */
    eraser_tracker_t eraser;
    
    /* Trace compaction (see detector.c): the thread currently on a same-thread run of
     * accesses, whether it has written during the run, and which of its records have
     * been elided from the trace and are still to be flushed. */
    uint32_t trace_run_tid;
    bool trace_run_valid;
    bool trace_run_written;
    bool trace_pending_write;
    bool trace_pending_read;
    
    struct ts_shadow_entry *next;
} ts_shadow_entry_t;

void shadow_init(void);
void shadow_shutdown(void);
ts_shadow_entry_t *shadow_get_or_create(const void *addr, const char *var_name);
ts_shadow_entry_t *shadow_lookup(const void *addr);
void shadow_free_entry(const void *addr);
void shadow_for_each(void (*fn)(ts_shadow_entry_t *entry));

#endif /* SHADOW_H */
