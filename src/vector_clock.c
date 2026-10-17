#include "vector_clock.h"
#include <stdio.h>
#include <string.h>

void vc_init(vector_clock_t *vc) {
    if (!vc) return;
    memset(vc->clock, 0, sizeof(vc->clock));
}

void vc_copy(vector_clock_t *dst, const vector_clock_t *src) {
    if (!dst || !src) return;
    memcpy(dst->clock, src->clock, sizeof(dst->clock));
}

void vc_increment(vector_clock_t *vc, uint32_t tid) {
    if (!vc || tid >= TS_MAX_THREADS) return;
    vc->clock[tid]++;
}

void vc_join(vector_clock_t *dst, const vector_clock_t *src) {
    if (!dst || !src) return;
    for (uint32_t i = 0; i < TS_MAX_THREADS; i++) {
        if (src->clock[i] > dst->clock[i]) {
            dst->clock[i] = src->clock[i];
        }
    }
}

/* Full vector clock comparison: returns true if a <= b and a != b */
bool vc_happens_before(const vector_clock_t *a, const vector_clock_t *b) {
    if (!a || !b) return false;
    bool strictly_less = false;
    for (uint32_t i = 0; i < TS_MAX_THREADS; i++) {
        if (a->clock[i] > b->clock[i]) {
            return false;
        }
        if (a->clock[i] < b->clock[i]) {
            strictly_less = true;
        }
    }
    return strictly_less;
}

/* Epoch comparison: event A in thread a_tid at a_epoch happened-before current clock B */
bool vc_happens_before_epoch(uint32_t a_tid, uint32_t a_epoch, const vector_clock_t *b) {
    if (!b || a_tid >= TS_MAX_THREADS) return false;
    return a_epoch <= b->clock[a_tid];
}

void vc_to_string(const vector_clock_t *vc, char *buf, size_t max_len) {
    if (!vc || !buf || max_len == 0) return;
    
    /* Find highest non-zero tid for compact readable output */
    int highest = -1;
    for (int i = TS_MAX_THREADS - 1; i >= 0; i--) {
        if (vc->clock[i] > 0) {
            highest = i;
            break;
        }
    }
    
    if (highest < 0) {
        snprintf(buf, max_len, "[]");
        return;
    }
    
    size_t offset = 0;
    offset += snprintf(buf + offset, max_len > offset ? max_len - offset : 0, "[");
    for (int i = 0; i <= highest; i++) {
        offset += snprintf(buf + offset, max_len > offset ? max_len - offset : 0,
                           "%u%s", vc->clock[i], (i < highest) ? "," : "");
        if (offset >= max_len - 1) break;
    }
    snprintf(buf + offset, max_len > offset ? max_len - offset : 0, "]");
}
