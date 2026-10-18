#include "shadow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHADOW_BUCKETS (1 << 16) /* 65,536 buckets */

static ts_shadow_entry_t *g_shadow_table[SHADOW_BUCKETS];

static inline uint32_t hash_address(uintptr_t addr) {
    /* 64-bit to 32-bit integer mixer */
    uint64_t x = (uint64_t)addr;
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return (uint32_t)(x & (SHADOW_BUCKETS - 1));
}

void shadow_init(void) {
    memset(g_shadow_table, 0, sizeof(g_shadow_table));
}

void shadow_shutdown(void) {
    for (uint32_t i = 0; i < SHADOW_BUCKETS; i++) {
        ts_shadow_entry_t *cur = g_shadow_table[i];
        while (cur) {
            ts_shadow_entry_t *next = cur->next;
            free(cur);
            cur = next;
        }
        g_shadow_table[i] = NULL;
    }
}

ts_shadow_entry_t *shadow_get_or_create(const void *addr, const char *var_name) {
    uintptr_t uaddr = (uintptr_t)addr;
    uint32_t bucket = hash_address(uaddr);
    
    ts_shadow_entry_t *cur = g_shadow_table[bucket];
    while (cur) {
        if (cur->addr == uaddr) {
            if (var_name && var_name[0] && cur->var_name[0] == '\0') {
                snprintf(cur->var_name, sizeof(cur->var_name), "%s", var_name);
            }
            return cur;
        }
        cur = cur->next;
    }
    
    /* Create new shadow entry */
    ts_shadow_entry_t *entry = (ts_shadow_entry_t *)malloc(sizeof(ts_shadow_entry_t));
    if (!entry) {
        fprintf(stderr, "[ThreadSentinel ERROR] Out of memory allocating shadow entry\n");
        abort();
    }
    memset(entry, 0, sizeof(ts_shadow_entry_t));
    entry->addr = uaddr;
    if (var_name) {
        snprintf(entry->var_name, sizeof(entry->var_name), "%s", var_name);
    }
    lockset_init_tracker(&entry->eraser);
    
    entry->next = g_shadow_table[bucket];
    g_shadow_table[bucket] = entry;
    return entry;
}

ts_shadow_entry_t *shadow_lookup(const void *addr) {
    uintptr_t uaddr = (uintptr_t)addr;
    uint32_t bucket = hash_address(uaddr);
    
    ts_shadow_entry_t *cur = g_shadow_table[bucket];
    while (cur) {
        if (cur->addr == uaddr) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

void shadow_for_each(void (*fn)(ts_shadow_entry_t *entry)) {
    for (uint32_t i = 0; i < SHADOW_BUCKETS; i++) {
        for (ts_shadow_entry_t *cur = g_shadow_table[i]; cur; cur = cur->next) {
            fn(cur);
        }
    }
}

void shadow_free_entry(const void *addr) {
    uintptr_t uaddr = (uintptr_t)addr;
    uint32_t bucket = hash_address(uaddr);
    
    ts_shadow_entry_t **indirect = &g_shadow_table[bucket];
    while (*indirect) {
        if ((*indirect)->addr == uaddr) {
            ts_shadow_entry_t *to_free = *indirect;
            *indirect = to_free->next;
            free(to_free);
            return;
        }
        indirect = &((*indirect)->next);
    }
}
