#include "report.h"
#include "sentinel_internal.h"
#include "prevention.h"
#include "deadlock.h"
#include "vector_clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define MAX_REPORTED_RACES 1024

static race_report_t g_reported_races[MAX_REPORTED_RACES];
static int g_race_count = 0;
static char g_report_path[512] = "ts_report.json";
static int g_quiet = 0;
static struct timespec g_start_time;

void report_init(const char *report_path, int quiet) {
    if (report_path && report_path[0]) {
        snprintf(g_report_path, sizeof(g_report_path), "%s", report_path);
    } else {
        snprintf(g_report_path, sizeof(g_report_path), "ts_report.json");
    }
    g_quiet = quiet;
    g_race_count = 0;
    clock_gettime(CLOCK_MONOTONIC, &g_start_time);
}

int report_get_confirmed_count(void) {
    int count = 0;
    for (int i = 0; i < g_race_count; i++) {
        if (g_reported_races[i].verdict == RACE_CONFIRMED) count++;
    }
    return count;
}

int report_get_potential_count(void) {
    int count = 0;
    for (int i = 0; i < g_race_count; i++) {
        if (g_reported_races[i].verdict == RACE_POTENTIAL) count++;
    }
    return count;
}

static void format_lockset(uint64_t locks, char *buf, size_t max_len) {
    if (locks == 0) {
        snprintf(buf, max_len, "{}");
        return;
    }
    size_t off = 0;
    off += snprintf(buf + off, max_len > off ? max_len - off : 0, "{");
    bool first = true;
    for (int i = 0; i < TS_MAX_LOCKS; i++) {
        if (locks & (1ULL << i)) {
            off += snprintf(buf + off, max_len > off ? max_len - off : 0,
                            "%sL%d", first ? "" : ",", i);
            first = false;
        }
        if (off >= max_len - 2) break;
    }
    snprintf(buf + off, max_len > off ? max_len - off : 0, "}");
}

static bool is_duplicate(const race_report_t *race) {
    for (int i = 0; i < g_race_count; i++) {
        const race_report_t *r = &g_reported_races[i];
        if (r->addr == race->addr) {
            bool same_order = (strcmp(r->loc1, race->loc1) == 0 && strcmp(r->loc2, race->loc2) == 0);
            bool swap_order = (strcmp(r->loc1, race->loc2) == 0 && strcmp(r->loc2, race->loc1) == 0);
            if (same_order || swap_order) {
                return true;
            }
        }
    }
    return false;
}

void report_record_race(const race_report_t *race) {
    if (!race) return;
    if (is_duplicate(race)) return;
    
    if (g_race_count < MAX_REPORTED_RACES) {
        g_reported_races[g_race_count++] = *race;
    }
    
    if (!g_quiet) {
        int use_color = isatty(fileno(stderr));
        const char *c_red    = use_color ? "\033[1;31m" : "";
        const char *c_yellow = use_color ? "\033[1;33m" : "";
        const char *c_cyan   = use_color ? "\033[36m"   : "";
        const char *c_reset  = use_color ? "\033[0m"    : "";
        
        char l1_buf[64], l2_buf[64];
        char hb1_buf[128], hb2_buf[128];
        format_lockset(race->locks1, l1_buf, sizeof(l1_buf));
        format_lockset(race->locks2, l2_buf, sizeof(l2_buf));
        vc_to_string(&race->hb1, hb1_buf, sizeof(hb1_buf));
        vc_to_string(&race->hb2, hb2_buf, sizeof(hb2_buf));
        
        const char *tag_color = (race->verdict == RACE_CONFIRMED) ? c_red : c_yellow;
        const char *verdict_str = (race->verdict == RACE_CONFIRMED) ? "CONFIRMED RACE" : "POTENTIAL RACE";
        
        fprintf(stderr, "\n%s[ThreadSentinel] %s%s on '%s' (%p)\n",
                tag_color, verdict_str, c_reset,
                race->var_name[0] ? race->var_name : "anonymous",
                (void *)race->addr);
                
        fprintf(stderr, "   %s%s%s by T%u at %s%s%s  locks=%s  HB=%s\n",
                c_cyan, race->op2, c_reset, race->t2,
                c_cyan, race->loc2, c_reset, l2_buf, hb2_buf);
                
        fprintf(stderr, "   %s%s%s by T%u at %s%s%s  locks=%s  HB=%s\n",
                c_cyan, race->op1, c_reset, race->t1,
                c_cyan, race->loc1, c_reset, l1_buf, hb1_buf);
                
        fprintf(stderr, "   -> %s\n", race->reason);
    }
}

void report_flush_and_close(void) {
    if (g_report_path[0] == '\0') return;
#ifdef TS_PRELOAD
    /* LD_PRELOAD is inherited by every process in a command (timeout, env, shells).
     * Processes that never called a pthread function must not overwrite the report
     * written by the one that did. */
    if (!ts_preload_active) return;
#endif

    FILE *fp = fopen(g_report_path, "w");
    if (!fp) {
        fprintf(stderr, "[ThreadSentinel WARNING] Could not write report to %s\n", g_report_path);
        return;
    }
    
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double runtime_ms = (double)(now.tv_sec - g_start_time.tv_sec) * 1000.0 +
                        (double)(now.tv_nsec - g_start_time.tv_nsec) / 1000000.0;
                        
    const char *mode_str = (g_ts_config.mode == TS_MODE_PREVENT) ? "prevent" :
                           (g_ts_config.mode == TS_MODE_OFF) ? "off" : "detect";
                           
    const char *det_str = (g_ts_config.detector == TS_DETECTOR_LOCKSET) ? "lockset" :
                          (g_ts_config.detector == TS_DETECTOR_HB) ? "hb" : "hybrid";
                          
    fprintf(fp, "{\n");
    fprintf(fp, "  \"metadata\": {\n");
    fprintf(fp, "    \"mode\": \"%s\",\n", mode_str);
    fprintf(fp, "    \"detector\": \"%s\",\n", det_str);
    fprintf(fp, "    \"runtime_ms\": %.2f\n", runtime_ms);
    fprintf(fp, "  },\n");
    
    fprintf(fp, "  \"summary\": {\n");
    fprintf(fp, "    \"confirmed_races\": %d,\n", report_get_confirmed_count());
    fprintf(fp, "    \"potential_races\": %d,\n", report_get_potential_count());
    fprintf(fp, "    \"total_unique_races\": %d,\n", g_race_count);
    fprintf(fp, "    \"potential_deadlocks\": %d\n", deadlock_count());
    fprintf(fp, "  },\n");
    
    fprintf(fp, "  \"races\": [\n");
    for (int i = 0; i < g_race_count; i++) {
        const race_report_t *r = &g_reported_races[i];
        char hb1_buf[128], st1_buf[128], hb2_buf[128], st2_buf[128];
        vc_to_string(&r->hb1, hb1_buf, sizeof(hb1_buf));
        vc_to_string(&r->st1, st1_buf, sizeof(st1_buf));
        vc_to_string(&r->hb2, hb2_buf, sizeof(hb2_buf));
        vc_to_string(&r->st2, st2_buf, sizeof(st2_buf));
        
        fprintf(fp, "    {\n");
        fprintf(fp, "      \"verdict\": \"%s\",\n", (r->verdict == RACE_CONFIRMED) ? "CONFIRMED" : "POTENTIAL");
        fprintf(fp, "      \"severity\": \"%s\",\n", (r->verdict == RACE_CONFIRMED) ? "HIGH" : "MEDIUM");
        fprintf(fp, "      \"address\": \"%p\",\n", (void *)r->addr);
        fprintf(fp, "      \"variable\": \"%s\",\n", r->var_name);
        fprintf(fp, "      \"reason\": \"%s\",\n", r->reason);
        
        fprintf(fp, "      \"first_access\": {\n");
        fprintf(fp, "        \"type\": \"%s\",\n", r->op1);
        fprintf(fp, "        \"thread\": %u,\n", r->t1);
        fprintf(fp, "        \"location\": \"%s\",\n", r->loc1);
        fprintf(fp, "        \"locks\": %lu,\n", (unsigned long)r->locks1);
        fprintf(fp, "        \"hb\": %s,\n", hb1_buf);
        fprintf(fp, "        \"st\": %s\n", st1_buf);
        fprintf(fp, "      },\n");
        
        fprintf(fp, "      \"second_access\": {\n");
        fprintf(fp, "        \"type\": \"%s\",\n", r->op2);
        fprintf(fp, "        \"thread\": %u,\n", r->t2);
        fprintf(fp, "        \"location\": \"%s\",\n", r->loc2);
        fprintf(fp, "        \"locks\": %lu,\n", (unsigned long)r->locks2);
        fprintf(fp, "        \"hb\": %s,\n", hb2_buf);
        fprintf(fp, "        \"st\": %s\n", st2_buf);
        fprintf(fp, "      }\n");
        
        fprintf(fp, "    }%s\n", (i + 1 < g_race_count) ? "," : "");
    }
    fprintf(fp, "  ],\n");
    
    /* Output protected addresses list for learn-then-prevent mode */
    fprintf(fp, "  \"protected_items\": [\n");
    prevention_write_protected_json(fp);
    fprintf(fp, "  ],\n");

    /* Lock-order cycles (keys deliberately avoid "variable", which the protect-file parser reads) */
    fprintf(fp, "  \"deadlocks\": [\n");
    deadlock_write_json(fp);
    fprintf(fp, "  ]\n");
    
    fprintf(fp, "}\n");
    fclose(fp);
}
