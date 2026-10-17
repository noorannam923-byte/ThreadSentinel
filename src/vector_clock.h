#ifndef VECTOR_CLOCK_H
#define VECTOR_CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "sentinel_internal.h"

typedef struct {
    uint32_t clock[TS_MAX_THREADS];
} vector_clock_t;

void vc_init(vector_clock_t *vc);
void vc_copy(vector_clock_t *dst, const vector_clock_t *src);
void vc_increment(vector_clock_t *vc, uint32_t tid);
void vc_join(vector_clock_t *dst, const vector_clock_t *src);
bool vc_happens_before(const vector_clock_t *a, const vector_clock_t *b);
bool vc_happens_before_epoch(uint32_t a_tid, uint32_t a_epoch, const vector_clock_t *b);
void vc_to_string(const vector_clock_t *vc, char *buf, size_t max_len);

#endif /* VECTOR_CLOCK_H */
