#ifndef SENTINEL_H
#define SENTINEL_H

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>

/* Limits as defined in Section 4.4 */
#define TS_MAX_THREADS    64
#define TS_MAX_LOCKS      64
#define TS_GUARD_STRIPES  256

#ifdef TS_DISABLE

/* Uninstrumented baseline: operations map directly to standard C / POSIX */
#define TS_READ(x)               (x)
#define TS_WRITE(x, val)         do { (x) = (val); } while (0)
#define TS_UPDATE(x, op)         do { (x) op; } while (0)
#define TS_REGION_BEGIN(n, ...)  do { (void)(n); } while (0)
#define TS_REGION_END()          do {} while (0)
#define TS_FREE(ptr, size)       do { (void)(size); free(ptr); } while (0)

#else /* !TS_DISABLE */

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime hook functions called by instrumentation macros */
void *ts_read_pre(const void *addr, const char *var_name);
void  ts_read_post(const void *addr, const char *var_name, const char *file, int line);

void *ts_write_pre(const void *addr, const char *var_name);
void  ts_write_post(const void *addr, const char *var_name, const char *file, int line);

void *ts_update_pre(const void *addr, const char *var_name);
void  ts_update_post(const void *addr, const char *var_name, const char *file, int line);

void  ts_region_begin(int n, const char *names_str, ...);
void  ts_region_end(void);
void  ts_free(void *ptr, size_t size);

/* Pthread synchronization function interceptors */
int ts_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                      void *(*start_routine)(void *), void *arg);
int ts_pthread_join(pthread_t thread, void **retval);
int ts_mutex_lock(pthread_mutex_t *mutex);
int ts_mutex_lock_at(pthread_mutex_t *mutex, const char *name, const char *file, int line);
int ts_mutex_trylock(pthread_mutex_t *mutex);
int ts_mutex_unlock(pthread_mutex_t *mutex);
int ts_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int ts_cond_signal(pthread_cond_t *cond);
int ts_cond_broadcast(pthread_cond_t *cond);
int ts_barrier_wait(pthread_barrier_t *barrier);

#ifdef __cplusplus
}
#endif

/* Instrumentation macros */
#define TS_READ(x) ({ \
    ts_read_pre((const void *)&(x), #x); \
    __typeof__(x) _ts_read_val = (x); \
    ts_read_post((const void *)&(x), #x, __FILE__, __LINE__); \
    _ts_read_val; \
})

#define TS_WRITE(x, val) do { \
    ts_write_pre((const void *)&(x), #x); \
    (x) = (val); \
    ts_write_post((const void *)&(x), #x, __FILE__, __LINE__); \
} while (0)

#define TS_UPDATE(x, op) do { \
    ts_update_pre((const void *)&(x), #x); \
    (x) op; \
    ts_update_post((const void *)&(x), #x, __FILE__, __LINE__); \
} while (0)

#define TS_REGION_BEGIN(n, ...) ts_region_begin((n), #__VA_ARGS__, __VA_ARGS__)
#define TS_REGION_END()         ts_region_end()
#define TS_FREE(ptr, size)      ts_free((ptr), (size))

/* Intercept standard pthread calls unless inside the Sentinel runtime */
#ifndef TS_INTERNAL
#define pthread_create         ts_pthread_create
#define pthread_join           ts_pthread_join
/* Function-like so the lock-order report can name the mutex and the call site */
#define pthread_mutex_lock(m)  ts_mutex_lock_at((m), #m, __FILE__, __LINE__)
#define pthread_mutex_trylock  ts_mutex_trylock
#define pthread_mutex_unlock   ts_mutex_unlock
#define pthread_cond_wait      ts_cond_wait
#define pthread_cond_signal    ts_cond_signal
#define pthread_cond_broadcast ts_cond_broadcast
#define pthread_barrier_wait   ts_barrier_wait
#endif /* !TS_INTERNAL */

#endif /* !TS_DISABLE */

#endif /* SENTINEL_H */
