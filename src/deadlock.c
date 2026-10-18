/*
 * Potential deadlock detection via a lock-order graph (GoodLock-style).
 *
 * Acquiring lock B while holding lock A adds the edge A -> B. A cycle in this graph
 * means the locks are taken in inconsistent orders, which can deadlock on some
 * schedule even if this run did not. A cycle is reported only if
 *   - its edges were taken by at least two different threads (a single thread
 *     cannot deadlock with itself this way), and
 *   - no "gate" lock was held across every edge (a common outer lock serialises
 *     the inner acquisitions, so they cannot interleave).
 *
 * Only blocking pthread_mutex_lock acquisitions add edges: trylock cannot block, and
 * prevention guard stripes always follow a fixed ascending order.
 */
#include "deadlock.h"
#include "sentinel_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define MAX_REPORTED_CYCLES 64

typedef struct {
    bool valid;
    uint64_t tid_mask;   /* threads that have taken this edge */
    uint64_t gate;       /* locks held across the first observation, besides the source */
    uint32_t tid;        /* first observing thread */
    char loc[96];        /* where the target lock was acquired */
} lo_edge_t;

typedef struct {
    int len;
    int locks[TS_MAX_LOCKS];   /* locks[0] -> locks[1] -> ... -> locks[len-1] -> locks[0] */
} lo_cycle_t;

static uint64_t g_adj[TS_MAX_LOCKS];                 /* bit b of g_adj[a]: edge a -> b */
static lo_edge_t g_edges[TS_MAX_LOCKS][TS_MAX_LOCKS];
static char g_lock_names[TS_MAX_LOCKS][64];
static uint64_t g_user_locks;                        /* ids seen through pthread_mutex_lock */
static lo_cycle_t g_cycles[MAX_REPORTED_CYCLES];
static uint64_t g_cycle_masks[MAX_REPORTED_CYCLES];
static int g_cycle_count;

void deadlock_init(void) {
    memset(g_adj, 0, sizeof(g_adj));
    memset(g_edges, 0, sizeof(g_edges));
    memset(g_lock_names, 0, sizeof(g_lock_names));
    g_user_locks = 0;
    g_cycle_count = 0;
}

int deadlock_count(void) {
    return g_cycle_count;
}

static const char *lock_name(int id, char *buf, size_t len) {
    if (g_lock_names[id][0]) return g_lock_names[id];
    snprintf(buf, len, "lock#%d", id);
    return buf;
}

/* Breadth-first search from `from` to `to`; on success fills path[0..n-1] with from ... to. */
static int find_path(int from, int to, int path[TS_MAX_LOCKS]) {
    int parent[TS_MAX_LOCKS];
    int queue[TS_MAX_LOCKS];
    uint64_t seen = 1ULL << from;
    int head = 0, tail = 0;
    queue[tail++] = from;
    parent[from] = -1;
    while (head < tail) {
        int cur = queue[head++];
        if (cur == to) {
            int rev[TS_MAX_LOCKS], n = 0;
            for (int v = to; v != -1; v = parent[v]) rev[n++] = v;
            for (int i = 0; i < n; i++) path[i] = rev[n - 1 - i];
            return n;
        }
        uint64_t next = g_adj[cur] & ~seen;
        while (next) {
            int v = __builtin_ctzll(next);
            next &= next - 1;
            seen |= 1ULL << v;
            parent[v] = cur;
            queue[tail++] = v;
        }
    }
    return 0;
}

/* The new edge `from -> to` closes a cycle if `to` already reaches `from`. */
static void check_cycle(int from, int to) {
    int path[TS_MAX_LOCKS];
    int n = find_path(to, from, path);   /* to ... from */
    if (n == 0) return;

    lo_cycle_t cyc;
    cyc.len = n;
    uint64_t mask = 0, threads = 0, gates = ~0ULL;
    for (int i = 0; i < n; i++) {
        cyc.locks[i] = path[i];
        mask |= 1ULL << path[i];
        const lo_edge_t *e = &g_edges[path[i]][path[(i + 1) % n]];
        threads |= e->tid_mask;
        gates &= e->gate;
    }
    /* With two or more threads across the cycle, two of its edges can run on different threads */
    if (__builtin_popcountll(threads) < 2) return;
    if (gates != 0) return;

    for (int i = 0; i < g_cycle_count; i++) {
        if (g_cycle_masks[i] == mask) return;
    }
    if (g_cycle_count >= MAX_REPORTED_CYCLES) return;
    g_cycles[g_cycle_count] = cyc;
    g_cycle_masks[g_cycle_count] = mask;
    g_cycle_count++;

    if (!g_ts_config.quiet) {
        const char *c_mag = "\033[95m", *c_reset = "\033[0m";
        char buf[32];
        fprintf(stderr, "\n%s[ThreadSentinel] POTENTIAL DEADLOCK%s: lock-order cycle ", c_mag, c_reset);
        for (int i = 0; i < n; i++) {
            fprintf(stderr, "%s -> ", lock_name(cyc.locks[i], buf, sizeof(buf)));
        }
        fprintf(stderr, "%s\n", lock_name(cyc.locks[0], buf, sizeof(buf)));
        for (int i = 0; i < n; i++) {
            int a = cyc.locks[i], b = cyc.locks[(i + 1) % n];
            char ba[32], bb[32];
            fprintf(stderr, "   T%u takes %s while holding %s at %s\n", g_edges[a][b].tid,
                    lock_name(b, bb, sizeof(bb)), lock_name(a, ba, sizeof(ba)),
                    g_edges[a][b].loc[0] ? g_edges[a][b].loc : "?");
        }
    }
}

void deadlock_on_acquire(uint32_t tid, int lock_id, uint64_t held_before,
                         const char *name, const char *loc) {
    if (lock_id < 0 || lock_id >= TS_MAX_LOCKS || tid >= TS_MAX_THREADS) return;
    g_user_locks |= 1ULL << lock_id;
    if (name && name[0] && !g_lock_names[lock_id][0]) {
        snprintf(g_lock_names[lock_id], sizeof(g_lock_names[lock_id]), "%s", name);
    }

    uint64_t held = held_before & g_user_locks & ~(1ULL << lock_id);
    while (held) {
        int h = __builtin_ctzll(held);
        held &= held - 1;
        lo_edge_t *e = &g_edges[h][lock_id];
        uint64_t tid_bit = 1ULL << tid;
        if (!e->valid) {
            e->valid = true;
            e->tid = tid;
            e->tid_mask = tid_bit;
            e->gate = held_before & g_user_locks & ~(1ULL << h) & ~(1ULL << lock_id);
            snprintf(e->loc, sizeof(e->loc), "%s", loc ? loc : "");
            g_adj[h] |= 1ULL << lock_id;
            check_cycle(h, lock_id);
        } else if (!(e->tid_mask & tid_bit)) {
            /* A new thread on a known edge can complete a previously single-thread cycle */
            e->tid_mask |= tid_bit;
            check_cycle(h, lock_id);
        }
    }
}

void deadlock_write_json(FILE *fp) {
    for (int c = 0; c < g_cycle_count; c++) {
        const lo_cycle_t *cyc = &g_cycles[c];
        char buf[32];
        fprintf(fp, "    {\n      \"cycle\": [");
        for (int i = 0; i < cyc->len; i++) {
            fprintf(fp, "%s\"%s\"", i ? ", " : "", lock_name(cyc->locks[i], buf, sizeof(buf)));
        }
        fprintf(fp, "],\n      \"edges\": [\n");
        for (int i = 0; i < cyc->len; i++) {
            int a = cyc->locks[i], b = cyc->locks[(i + 1) % cyc->len];
            char ba[32], bb[32];
            const lo_edge_t *e = &g_edges[a][b];
            fprintf(fp, "        {\"held\": \"%s\", \"acquired\": \"%s\", \"thread\": %u, \"location\": \"%s\"}%s\n",
                    lock_name(a, ba, sizeof(ba)), lock_name(b, bb, sizeof(bb)), e->tid, e->loc,
                    (i + 1 < cyc->len) ? "," : "");
        }
        fprintf(fp, "      ]\n    }%s\n", (c + 1 < g_cycle_count) ? "," : "");
    }
}
