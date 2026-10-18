#include "trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static FILE *g_trace_fp = NULL;
static uint64_t g_trace_seq = 0;
static struct timespec g_trace_start_time;

void trace_init(const char *trace_path) {
    if (trace_path && trace_path[0] != '\0') {
        g_trace_fp = fopen(trace_path, "w");
        if (!g_trace_fp) {
            fprintf(stderr, "[ThreadSentinel WARNING] Failed to open trace file: %s\n", trace_path);
        } else {
            setvbuf(g_trace_fp, NULL, _IOFBF, 1 << 20);
        }
    }
    g_trace_seq = 0;
    clock_gettime(CLOCK_MONOTONIC, &g_trace_start_time);
}

void trace_shutdown(void) {
    if (g_trace_fp) {
        fflush(g_trace_fp);
        fclose(g_trace_fp);
        g_trace_fp = NULL;
    }
}

bool trace_is_enabled(void) {
    return g_trace_fp != NULL;
}

void trace_record_event(uint32_t tid, const char *ev, const void *obj, int lock_id,
                        const char *var, const char *loc, uint64_t locks_held,
                        const vector_clock_t *hb, const vector_clock_t *st) {
    if (!g_trace_fp) return;
    
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t ns = (uint64_t)(now.tv_sec - g_trace_start_time.tv_sec) * 1000000000ULL +
                  (uint64_t)(now.tv_nsec - g_trace_start_time.tv_nsec);
                  
    fprintf(g_trace_fp, "{\"seq\":%lu,\"t\":%u,\"ev\":\"%s\",\"ns\":%lu",
            (unsigned long)g_trace_seq++, tid, ev, (unsigned long)ns);
            
    if (obj) {
        fprintf(g_trace_fp, ",\"obj\":\"%p\"", obj);
    }
    if (lock_id >= 0) {
        fprintf(g_trace_fp, ",\"lock_id\":%d", lock_id);
    }
    if (var && var[0]) {
        fprintf(g_trace_fp, ",\"var\":\"%s\"", var);
    }
    if (loc && loc[0]) {
        fprintf(g_trace_fp, ",\"loc\":\"%s\"", loc);
    }
    if (locks_held != 0) {
        fprintf(g_trace_fp, ",\"locks\":[");
        bool first = true;
        for (int i = 0; i < TS_MAX_LOCKS; i++) {
            if (locks_held & (1ULL << i)) {
                fprintf(g_trace_fp, "%s%d", first ? "" : ",", i);
                first = false;
            }
        }
        fprintf(g_trace_fp, "]");
    }
    if (hb) {
        char buf[256];
        vc_to_string(hb, buf, sizeof(buf));
        fprintf(g_trace_fp, ",\"hb\":%s", buf);
    }
    if (st) {
        char buf[256];
        vc_to_string(st, buf, sizeof(buf));
        fprintf(g_trace_fp, ",\"st\":%s", buf);
    }
    fprintf(g_trace_fp, "}\n");
}
