#ifndef SYNC_H
#define SYNC_H

#include "sentinel_internal.h"
#include <pthread.h>

void ts_sync_init(void);
void ts_sync_shutdown(void);

#endif /* SYNC_H */
