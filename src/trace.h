#ifndef TRACE_H
#define TRACE_H

#include "vector_clock.h"
#include <stdint.h>
#include <stdbool.h>

void trace_init(const char *trace_path);
void trace_shutdown(void);
void trace_record_event(uint32_t tid, const char *ev, const void *obj, int lock_id,
                        const char *var, const char *loc, uint64_t locks_held,
                        const vector_clock_t *hb, const vector_clock_t *st);

#endif /* TRACE_H */
