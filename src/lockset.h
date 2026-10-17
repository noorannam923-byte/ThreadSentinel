#ifndef LOCKSET_H
#define LOCKSET_H

#include "sentinel_internal.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    ERASER_VIRGIN = 0,
    ERASER_EXCLUSIVE,
    ERASER_SHARED,
    ERASER_SHARED_MODIFIED
} eraser_state_t;

typedef struct {
    eraser_state_t state;
    uint32_t first_tid;        /* Thread that first accessed in EXCLUSIVE state */
    uint64_t candidate_locks;  /* C(v) bitmask */
} eraser_tracker_t;

void lockset_init_tracker(eraser_tracker_t *tracker);
void lockset_update(eraser_tracker_t *tracker, uint32_t tid, bool is_write, uint64_t held_locks);
bool lockset_has_violation(const eraser_tracker_t *tracker);

int  lockset_assign_id(const void *lock_addr);
int  lockset_get_id(const void *lock_addr);
void lockset_add_held(uint64_t *locks_held, int lock_id);
void lockset_remove_held(uint64_t *locks_held, int lock_id);

#endif /* LOCKSET_H */
