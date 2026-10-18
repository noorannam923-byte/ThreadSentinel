#ifndef REAL_H
#define REAL_H

/*
 * LD_PRELOAD build only (-DTS_PRELOAD). The shim defines pthread_mutex_lock & co.
 * itself, so inside the shim those names would call back into the shim forever.
 * Every runtime call to them is redirected to a pointer to the next definition
 * (libc's), resolved on first use with dlsym(RTLD_NEXT, ...).
 */
#include <pthread.h>

/* Set by the shim's interposers: this process made at least one pthread call */
extern volatile int ts_preload_active;

extern int (*ts_real_pthread_create)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
extern int (*ts_real_pthread_join)(pthread_t, void **);
extern int (*ts_real_pthread_mutex_lock)(pthread_mutex_t *);
extern int (*ts_real_pthread_mutex_trylock)(pthread_mutex_t *);
extern int (*ts_real_pthread_mutex_unlock)(pthread_mutex_t *);
extern int (*ts_real_pthread_cond_wait)(pthread_cond_t *, pthread_mutex_t *);
extern int (*ts_real_pthread_cond_signal)(pthread_cond_t *);
extern int (*ts_real_pthread_cond_broadcast)(pthread_cond_t *);
extern int (*ts_real_pthread_barrier_wait)(pthread_barrier_t *);

#ifndef TS_REAL_IMPL
#define pthread_create         ts_real_pthread_create
#define pthread_join           ts_real_pthread_join
#define pthread_mutex_lock     ts_real_pthread_mutex_lock
#define pthread_mutex_trylock  ts_real_pthread_mutex_trylock
#define pthread_mutex_unlock   ts_real_pthread_mutex_unlock
#define pthread_cond_wait      ts_real_pthread_cond_wait
#define pthread_cond_signal    ts_real_pthread_cond_signal
#define pthread_cond_broadcast ts_real_pthread_cond_broadcast
#define pthread_barrier_wait   ts_real_pthread_barrier_wait
#endif

#endif /* REAL_H */
