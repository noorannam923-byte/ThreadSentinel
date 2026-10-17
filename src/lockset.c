#include "lockset.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const void *g_lock_addrs[TS_MAX_LOCKS];
static int g_lock_count = 0;

int lockset_assign_id(const void *lock_addr) {
    if (!lock_addr) return -1;
    for (int i = 0; i < g_lock_count; i++) {
        if (g_lock_addrs[i] == lock_addr) {
            return i;
        }
    }
    if (g_lock_count >= TS_MAX_LOCKS) {
        fprintf(stderr, "[ThreadSentinel ERROR] Exceeded TS_MAX_LOCKS (%d)\n", TS_MAX_LOCKS);
        abort();
    }
    int id = g_lock_count++;
    g_lock_addrs[id] = lock_addr;
    return id;
}

int lockset_get_id(const void *lock_addr) {
    if (!lock_addr) return -1;
    for (int i = 0; i < g_lock_count; i++) {
        if (g_lock_addrs[i] == lock_addr) {
            return i;
        }
    }
    return -1;
}

void lockset_add_held(uint64_t *locks_held, int lock_id) {
    if (!locks_held || lock_id < 0 || lock_id >= TS_MAX_LOCKS) return;
    *locks_held |= (1ULL << lock_id);
}

void lockset_remove_held(uint64_t *locks_held, int lock_id) {
    if (!locks_held || lock_id < 0 || lock_id >= TS_MAX_LOCKS) return;
    *locks_held &= ~(1ULL << lock_id);
}

void lockset_init_tracker(eraser_tracker_t *tracker) {
    if (!tracker) return;
    tracker->state = ERASER_VIRGIN;
    tracker->first_tid = 0;
    tracker->candidate_locks = ~0ULL;
}

/*
 * Eraser State Machine implementation:
 * VIRGIN: Not yet accessed by any thread.
 * EXCLUSIVE: Accessed only by tracker->first_tid. No locks needed during initialization.
 * SHARED: Accessed read-only by multiple threads. Still race-free without locks.
 * SHARED_MODIFIED: Accessed concurrently and written by multiple threads. Must be protected by common lock.
 */
void lockset_update(eraser_tracker_t *tracker, uint32_t tid, bool is_write, uint64_t held_locks) {
    if (!tracker) return;
    
    switch (tracker->state) {
        case ERASER_VIRGIN:
            tracker->state = ERASER_EXCLUSIVE;
            tracker->first_tid = tid;
            tracker->candidate_locks = ~0ULL;
            break;
            
        case ERASER_EXCLUSIVE:
            if (tid != tracker->first_tid) {
                if (is_write) {
                    tracker->state = ERASER_SHARED_MODIFIED;
                } else {
                    tracker->state = ERASER_SHARED;
                }
                tracker->candidate_locks &= held_locks;
            }
            break;
            
        case ERASER_SHARED:
            if (is_write) {
                tracker->state = ERASER_SHARED_MODIFIED;
            }
            tracker->candidate_locks &= held_locks;
            break;
            
        case ERASER_SHARED_MODIFIED:
            tracker->candidate_locks &= held_locks;
            break;
    }
}

bool lockset_has_violation(const eraser_tracker_t *tracker) {
    if (!tracker) return false;
    return (tracker->state == ERASER_SHARED_MODIFIED && tracker->candidate_locks == 0);
}
