#include "sentinel_internal.h"
#include "threads.h"
#include "sync.h"
#include "shadow.h"
#include "detector.h"
#include "prevention.h"
#include "report.h"
#include "trace.h"
#include "deadlock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

ts_config_t g_ts_config;
pthread_mutex_t g_ts_runtime_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
bool g_ts_initialized = false;
static bool g_ts_shutdown_complete = false;

void ts_lock_runtime(void) {
    pthread_mutex_lock(&g_ts_runtime_mutex);
}

void ts_unlock_runtime(void) {
    pthread_mutex_unlock(&g_ts_runtime_mutex);
}

static void parse_env_config(void) {
    memset(&g_ts_config, 0, sizeof(g_ts_config));
    
    /* TS_MODE: detect (default), prevent, off */
    const char *mode_env = getenv("TS_MODE");
    if (mode_env) {
        if (strcasecmp(mode_env, "prevent") == 0) {
            g_ts_config.mode = TS_MODE_PREVENT;
        } else if (strcasecmp(mode_env, "off") == 0) {
            g_ts_config.mode = TS_MODE_OFF;
        } else {
            g_ts_config.mode = TS_MODE_DETECT;
        }
    } else {
        g_ts_config.mode = TS_MODE_DETECT;
    }
    
    /* TS_DETECTOR: hybrid (default), lockset, hb */
    const char *det_env = getenv("TS_DETECTOR");
    if (det_env) {
        if (strcasecmp(det_env, "lockset") == 0) {
            g_ts_config.detector = TS_DETECTOR_LOCKSET;
        } else if (strcasecmp(det_env, "hb") == 0) {
            g_ts_config.detector = TS_DETECTOR_HB;
        } else {
            g_ts_config.detector = TS_DETECTOR_HYBRID;
        }
    } else {
        g_ts_config.detector = TS_DETECTOR_HYBRID;
    }
    
    /* TS_TRACE: file path or empty */
    const char *trace_env = getenv("TS_TRACE");
    if (trace_env) {
        snprintf(g_ts_config.trace_path, sizeof(g_ts_config.trace_path), "%s", trace_env);
    }
    
    /* TS_REPORT: default "ts_report.json" */
    const char *rep_env = getenv("TS_REPORT");
    if (rep_env && rep_env[0]) {
        snprintf(g_ts_config.report_path, sizeof(g_ts_config.report_path), "%s", rep_env);
    } else {
        snprintf(g_ts_config.report_path, sizeof(g_ts_config.report_path), "ts_report.json");
    }
    
    /* TS_PROTECT_FILE: path to prior report */
    const char *prot_env = getenv("TS_PROTECT_FILE");
    if (prot_env) {
        snprintf(g_ts_config.protect_file, sizeof(g_ts_config.protect_file), "%s", prot_env);
    }
    
    /* TS_QUIET: 1 = suppress stderr race messages */
    const char *quiet_env = getenv("TS_QUIET");
    if (quiet_env && (strcmp(quiet_env, "1") == 0 || strcasecmp(quiet_env, "true") == 0)) {
        g_ts_config.quiet = 1;
    } else {
        g_ts_config.quiet = 0;
    }
}

__attribute__((constructor))
void ts_runtime_init(void) {
    if (g_ts_initialized) return;
    g_ts_initialized = true;
    
    parse_env_config();
    
    ts_threads_init();
    ts_sync_init();
    shadow_init();
    detector_init();
    deadlock_init();
    prevention_init(g_ts_config.protect_file);
    report_init(g_ts_config.report_path, g_ts_config.quiet);
    trace_init(g_ts_config.trace_path);
    
    atexit(ts_runtime_shutdown);
}

__attribute__((destructor))
void ts_runtime_shutdown(void) {
    if (g_ts_shutdown_complete) return;
    g_ts_shutdown_complete = true;
    
    ts_lock_runtime();
    report_flush_and_close();
    detector_flush_trace();
    trace_shutdown();
    prevention_shutdown();
    detector_shutdown();
    shadow_shutdown();
    ts_sync_shutdown();
    ts_threads_shutdown();
    ts_unlock_runtime();
}

void *ts_read_pre(const void *addr, const char *var_name) {
    if (!g_ts_initialized) ts_runtime_init();
    if (g_ts_config.mode == TS_MODE_PREVENT && prevention_is_protected(addr, var_name, NULL)) {
        prevention_lock_guard_for_addr(addr);
    }
    return (void *)addr;
}

void ts_read_post(const void *addr, const char *var_name, const char *file, int line) {
    char loc[128];
    snprintf(loc, sizeof(loc), "%s:%d", file, line);
    
    ts_lock_runtime();
    detector_process_access(addr, var_name, loc, false);
    ts_unlock_runtime();
    
    if (g_ts_config.mode == TS_MODE_PREVENT && prevention_is_protected(addr, var_name, loc)) {
        prevention_unlock_guard_for_addr(addr);
    }
}

void *ts_write_pre(const void *addr, const char *var_name) {
    if (!g_ts_initialized) ts_runtime_init();
    if (g_ts_config.mode == TS_MODE_PREVENT && prevention_is_protected(addr, var_name, NULL)) {
        prevention_lock_guard_for_addr(addr);
    }
    return (void *)addr;
}

void ts_write_post(const void *addr, const char *var_name, const char *file, int line) {
    char loc[128];
    snprintf(loc, sizeof(loc), "%s:%d", file, line);
    
    ts_lock_runtime();
    detector_process_access(addr, var_name, loc, true);
    ts_unlock_runtime();
    
    if (g_ts_config.mode == TS_MODE_PREVENT && prevention_is_protected(addr, var_name, loc)) {
        prevention_unlock_guard_for_addr(addr);
    }
}

void *ts_update_pre(const void *addr, const char *var_name) {
    if (!g_ts_initialized) ts_runtime_init();
    if (g_ts_config.mode == TS_MODE_PREVENT && prevention_is_protected(addr, var_name, NULL)) {
        prevention_lock_guard_for_addr(addr);
    }
    return (void *)addr;
}

void ts_update_post(const void *addr, const char *var_name, const char *file, int line) {
    char loc[128];
    snprintf(loc, sizeof(loc), "%s:%d", file, line);
    
    ts_lock_runtime();
    detector_process_access(addr, var_name, loc, true);
    ts_unlock_runtime();
    
    if (g_ts_config.mode == TS_MODE_PREVENT && prevention_is_protected(addr, var_name, loc)) {
        prevention_unlock_guard_for_addr(addr);
    }
}

/* Split the stringified TS_REGION_BEGIN arguments ("&a, &b") into bare variable
 * names ("a", "b") so protect-file entries can be matched by name. */
static void split_region_names(const char *names_str, int count, char buf[][64], const char *names[]) {
    const char *p = names_str ? names_str : "";
    for (int i = 0; i < count; i++) {
        while (*p == ' ' || *p == '&' || *p == '(') p++;
        size_t len = 0;
        while (p[len] && p[len] != ',') len++;
        size_t end = len;
        while (end > 0 && (p[end - 1] == ' ' || p[end - 1] == ')')) end--;
        if (end >= 64) end = 63;
        memcpy(buf[i], p, end);
        buf[i][end] = '\0';
        names[i] = buf[i];
        p += len;
        if (*p == ',') p++;
    }
}

void ts_region_begin(int n, const char *names_str, ...) {
    if (!g_ts_initialized) ts_runtime_init();
    if (n <= 0) return;
    void *addrs[16];
    char name_buf[16][64];
    const char *names[16];
    int count = (n > 16) ? 16 : n;

    va_list ap;
    va_start(ap, names_str);
    for (int i = 0; i < count; i++) {
        addrs[i] = va_arg(ap, void *);
    }
    va_end(ap);

    split_region_names(names_str, count, name_buf, names);
    prevention_region_begin(count, addrs, names);
}

void ts_region_end(void) {
    prevention_region_end();
}

void ts_free(void *ptr, size_t size) {
    (void)size;
    if (!ptr) return;
    ts_lock_runtime();
    ts_shadow_entry_t *entry = shadow_lookup(ptr);
    if (entry) detector_flush_trace_entry(entry);
    shadow_free_entry(ptr);
    ts_unlock_runtime();
    free(ptr);
}
