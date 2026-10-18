#ifndef DETECTOR_H
#define DETECTOR_H

#include "sentinel_internal.h"
#include "shadow.h"
#include <stdbool.h>

void detector_init(void);
void detector_shutdown(void);
void detector_flush_trace(void);
void detector_flush_trace_entry(ts_shadow_entry_t *entry);
void detector_process_access(const void *addr, const char *var_name, const char *loc, bool is_write);

#endif /* DETECTOR_H */
