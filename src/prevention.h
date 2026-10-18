#ifndef PREVENTION_H
#define PREVENTION_H

#include "sentinel_internal.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

void prevention_init(const char *protect_file);
void prevention_shutdown(void);

bool prevention_is_protected(const void *addr, const char *var_name, const char *loc);
void prevention_add_protected(const void *addr, const char *var_name, const char *loc);

int  prevention_get_stripe(const void *addr);
void prevention_lock_stripe(int stripe);
void prevention_unlock_stripe(int stripe);

void prevention_lock_guard_for_addr(const void *addr);
void prevention_unlock_guard_for_addr(const void *addr);

void prevention_region_begin(int n, void *addrs[], const char *names[]);
void prevention_region_end(void);

void prevention_write_protected_json(FILE *fp);

#endif /* PREVENTION_H */
