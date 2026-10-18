#ifndef DEADLOCK_H
#define DEADLOCK_H

#include "sentinel_internal.h"
#include <stdint.h>
#include <stdio.h>

void deadlock_init(void);

/* Called under the runtime lock right after tid acquired lock_id (blocking lock only)
 * while already holding held_before. name/loc may be NULL. */
void deadlock_on_acquire(uint32_t tid, int lock_id, uint64_t held_before,
                         const char *name, const char *loc);

int  deadlock_count(void);
void deadlock_write_json(FILE *fp);

#endif /* DEADLOCK_H */
