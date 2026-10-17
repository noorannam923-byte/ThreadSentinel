#ifndef REPORT_H
#define REPORT_H

#include "vector_clock.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    RACE_NONE = 0,
    RACE_POTENTIAL,
    RACE_CONFIRMED
} race_verdict_t;

typedef struct {
    race_verdict_t verdict;
    uintptr_t addr;
    char var_name[64];
    
    /* First access */
    const char *op1;          /* "READ" or "WRITE" */
    uint32_t t1;
    char loc1[128];
    uint64_t locks1;
    vector_clock_t hb1;
    vector_clock_t st1;
    
    /* Second access */
    const char *op2;          /* "READ" or "WRITE" */
    uint32_t t2;
    char loc2[128];
    uint64_t locks2;
    vector_clock_t hb2;
    vector_clock_t st2;
    
    char reason[256];
} race_report_t;

void report_init(const char *report_path, int quiet);
void report_record_race(const race_report_t *race);
void report_flush_and_close(void);
int  report_get_confirmed_count(void);
int  report_get_potential_count(void);

#endif /* REPORT_H */
