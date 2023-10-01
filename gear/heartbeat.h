#ifndef _SAWANG_HEARTBEAT_H_
#define _SAWANG_HEARTBEAT_H_

#include <pthread.h>
#include <stdbool.h>

#include "log.h"

typedef struct HeartbeatData {
    pthread_mutex_t mtx;
    pthread_cond_t wakeup;
    volatile time_t overdue;
} HeartbeatData;

typedef struct HeartbeatThreadData {
    long period;
    float timeout;
    const LogContext *log_ctx;
    HeartbeatData *segment;
    volatile bool stop;
    volatile bool started;
} HeartbeatThreadData;

extern void heartbeat_thread_data_init(HeartbeatThreadData *data);
extern bool heartbeat_thread_data_setup(HeartbeatThreadData *data,
    const LogContext *log_ctx, const char *sawang_name, long period, float timeout);
extern void heartbeat_thread_data_destroy(HeartbeatThreadData *data, const char *sawang_name);
extern void* heartbeat_thread(void *arg);
extern void heartbeat_thread_stop(HeartbeatThreadData* thread_data);

#endif //_SAWANG_HEARTBEAT_H_
