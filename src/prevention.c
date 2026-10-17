#include "prevention.h"
#include "sentinel_internal.h"
#include "threads.h"
#include "lockset.h"
#include "vector_clock.h"
#include "trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PROTECTED_ITEMS 256
#define MAX_REGION_STRIPES 16

typedef struct {
    uintptr_t addr;
    char var_name[64];
    char loc[128];
    bool from_file;
} protected_item_t;

static protected_item_t g_protected_items[MAX_PROTECTED_ITEMS];
static int g_protected_count = 0;

static pthread_mutex_t g_guard_stripes[TS_GUARD_STRIPES];
static vector_clock_t  g_guard_hb[TS_GUARD_STRIPES];
static int             g_guard_lock_ids[TS_GUARD_STRIPES];

static __thread int  t_region_stripes[MAX_REGION_STRIPES];
static __thread int  t_region_count = 0;

int prevention_get_stripe(const void *addr) {
    uint64_t x = (uint64_t)(uintptr_t)addr;
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return (int)(x % TS_GUARD_STRIPES);
}

void prevention_init(const char *protect_file) {
    g_protected_count = 0;
    for (int i = 0; i < TS_GUARD_STRIPES; i++) {
        pthread_mutex_init(&g_guard_stripes[i], NULL);
        vc_init(&g_guard_hb[i]);
        g_guard_lock_ids[i] = -1;
    }
    
    if (protect_file && protect_file[0]) {
        FILE *fp = fopen(protect_file, "r");
        if (fp) {
            char line[1024];
            while (fgets(line, sizeof(line), fp)) {
                /* Parse "variable": "var_name" from JSON report */
                char *v = strstr(line, "\"variable\": \"");
                if (v) {
                    v += 13;
                    char *end = strchr(v, '\"');
                    if (end && (end - v < 64)) {
                        char var_name[64];
                        size_t len = end - v;
                        memcpy(var_name, v, len);
                        var_name[len] = '\0';
                        if (g_protected_count < MAX_PROTECTED_ITEMS) {
                            /* Check if already present */
                            bool exists = false;
                            for (int i = 0; i < g_protected_count; i++) {
                                if (strcmp(g_protected_items[i].var_name, var_name) == 0) {
                                    exists = true;
                                    break;
                                }
                            }
                            if (!exists) {
                                protected_item_t *item = &g_protected_items[g_protected_count++];
                                item->addr = 0;
                                snprintf(item->var_name, sizeof(item->var_name), "%s", var_name);
                                item->loc[0] = '\0';
                                item->from_file = true;
                            }
                        }
                    }
                }
            }
            fclose(fp);
        }
    }
}

void prevention_shutdown(void) {
    for (int i = 0; i < TS_GUARD_STRIPES; i++) {
        pthread_mutex_destroy(&g_guard_stripes[i]);
    }
}

bool prevention_is_protected(const void *addr, const char *var_name, const char *loc) {
    uintptr_t uaddr = (uintptr_t)addr;
    
    /* 1. Fast match by runtime address */
    for (int i = 0; i < g_protected_count; i++) {
        if (g_protected_items[i].addr == uaddr && uaddr != 0) {
            return true;
        }
    }
    
    /* 2. Match by variable name from protect file */
    if (var_name && var_name[0]) {
        for (int i = 0; i < g_protected_count; i++) {
            if (strcmp(g_protected_items[i].var_name, var_name) == 0) {
                if (g_protected_items[i].addr == 0 && uaddr != 0) {
                    /* Bind the runtime address */
                    g_protected_items[i].addr = uaddr;
                    if (loc && loc[0]) {
                        snprintf(g_protected_items[i].loc, sizeof(g_protected_items[i].loc), "%s", loc);
                    }
                }
                return true;
            }
        }
    }
    return false;
}

void prevention_add_protected(const void *addr, const char *var_name, const char *loc) {
    uintptr_t uaddr = (uintptr_t)addr;
    for (int i = 0; i < g_protected_count; i++) {
        if (g_protected_items[i].addr == uaddr) {
            return;
        }
    }
    if (g_protected_count < MAX_PROTECTED_ITEMS) {
        protected_item_t *item = &g_protected_items[g_protected_count++];
        item->addr = uaddr;
        item->var_name[0] = '\0';
        if (var_name) {
            snprintf(item->var_name, sizeof(item->var_name), "%s", var_name);
        }
        item->loc[0] = '\0';
        if (loc) {
            snprintf(item->loc, sizeof(item->loc), "%s", loc);
        }
        item->from_file = false;
        
        trace_record_event(ts_get_tid(), "protect", addr, -1, var_name, loc, 0, NULL, NULL);
    }
}

void prevention_lock_stripe(int stripe) {
    if (stripe < 0 || stripe >= TS_GUARD_STRIPES) return;
    pthread_mutex_lock(&g_guard_stripes[stripe]);
    
    ts_lock_runtime();
    uint32_t tid = ts_get_tid();
    ts_thread_info_t *tinfo = ts_get_thread_info(tid);
    
    if (g_guard_lock_ids[stripe] < 0) {
        g_guard_lock_ids[stripe] = lockset_assign_id(&g_guard_stripes[stripe]);
    }
    int lid = g_guard_lock_ids[stripe];
    lockset_add_held(&tinfo->locks_held, lid);
    vc_join(&tinfo->hb_clock, &g_guard_hb[stripe]);
    
    trace_record_event(tid, "guard_lock", &g_guard_stripes[stripe], lid, NULL, NULL,
                       tinfo->locks_held, &tinfo->hb_clock, &tinfo->st_clock);
    ts_unlock_runtime();
}

void prevention_unlock_stripe(int stripe) {
    if (stripe < 0 || stripe >= TS_GUARD_STRIPES) return;
    
    ts_lock_runtime();
    uint32_t tid = ts_get_tid();
    ts_thread_info_t *tinfo = ts_get_thread_info(tid);
    int lid = g_guard_lock_ids[stripe];
    
    vc_copy(&g_guard_hb[stripe], &tinfo->hb_clock);
    vc_increment(&tinfo->hb_clock, tid);
    lockset_remove_held(&tinfo->locks_held, lid);
    
    trace_record_event(tid, "guard_unlock", &g_guard_stripes[stripe], lid, NULL, NULL,
                       tinfo->locks_held, &tinfo->hb_clock, &tinfo->st_clock);
    ts_unlock_runtime();
    
    pthread_mutex_unlock(&g_guard_stripes[stripe]);
}

void prevention_lock_guard_for_addr(const void *addr) {
    int stripe = prevention_get_stripe(addr);
    prevention_lock_stripe(stripe);
}

void prevention_unlock_guard_for_addr(const void *addr) {
    int stripe = prevention_get_stripe(addr);
    prevention_unlock_stripe(stripe);
}

void prevention_region_begin(int n, void *addrs[]) {
    if (g_ts_config.mode != TS_MODE_PREVENT || n <= 0) return;
    
    /* Collect stripes for any protected address in the region */
    int stripes[MAX_REGION_STRIPES];
    int count = 0;
    
    ts_lock_runtime();
    for (int i = 0; i < n; i++) {
        if (prevention_is_protected(addrs[i], NULL, NULL)) {
            int st = prevention_get_stripe(addrs[i]);
            /* Avoid duplicates */
            bool dup = false;
            for (int k = 0; k < count; k++) {
                if (stripes[k] == st) {
                    dup = true;
                    break;
                }
            }
            if (!dup && count < MAX_REGION_STRIPES) {
                stripes[count++] = st;
            }
        }
    }
    ts_unlock_runtime();
    
    /* Sort stripes in ascending order for deadlock avoidance */
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (stripes[j] > stripes[j + 1]) {
                int tmp = stripes[j];
                stripes[j] = stripes[j + 1];
                stripes[j + 1] = tmp;
            }
        }
    }
    
    /* Lock in sorted ascending order */
    for (int i = 0; i < count; i++) {
        prevention_lock_stripe(stripes[i]);
    }
    
    /* Save to thread-local state for prevention_region_end */
    t_region_count = count;
    for (int i = 0; i < count; i++) {
        t_region_stripes[i] = stripes[i];
    }
}

void prevention_region_end(void) {
    if (g_ts_config.mode != TS_MODE_PREVENT) return;
    
    /* Unlock in reverse order */
    for (int i = t_region_count - 1; i >= 0; i--) {
        prevention_unlock_stripe(t_region_stripes[i]);
    }
    t_region_count = 0;
}

void prevention_write_protected_json(FILE *fp) {
    for (int i = 0; i < g_protected_count; i++) {
        fprintf(fp, "    {\"variable\": \"%s\", \"address\": \"%p\", \"location\": \"%s\"}%s\n",
                g_protected_items[i].var_name,
                (void *)g_protected_items[i].addr,
                g_protected_items[i].loc,
                (i + 1 < g_protected_count) ? "," : "");
    }
}
