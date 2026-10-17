#ifndef SENTINEL_INTERNAL_H
#define SENTINEL_INTERNAL_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define TS_INTERNAL 1

#include "sentinel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

/* Runtime operation modes */
typedef enum {
    TS_MODE_DETECT,
    TS_MODE_PREVENT,
    TS_MODE_OFF
} ts_mode_t;

/* Detector algorithms */
typedef enum {
    TS_DETECTOR_HYBRID,
    TS_DETECTOR_LOCKSET,
    TS_DETECTOR_HB
} ts_detector_mode_t;

/* Configuration parsed from environment variables */
typedef struct {
    ts_mode_t mode;
    ts_detector_mode_t detector;
    char trace_path[512];
    char report_path[512];
    char protect_file[512];
    int quiet;
} ts_config_t;

extern ts_config_t g_ts_config;
extern pthread_mutex_t g_ts_runtime_mutex;
extern bool g_ts_initialized;

void ts_lock_runtime(void);
void ts_unlock_runtime(void);

void ts_runtime_init(void);
void ts_runtime_shutdown(void);

#endif /* SENTINEL_INTERNAL_H */
