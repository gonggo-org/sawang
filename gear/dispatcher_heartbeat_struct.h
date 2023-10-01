#ifndef _SAWANG_DISPATCHER_HEARTBEAT_STRUCT_H_
#define _SAWANG_DISPATCHER_HEARTBEAT_STRUCT_H_

#include <pthread.h>
#include "log.h"

typedef struct DispatcherHeartbeatData {
    pthread_mutex_t mtx;
    pthread_cond_t wakeup;
    time_t overdue;
} DispatcherHeartbeatData;

typedef struct DispatcherHeartbeatLock {
    pthread_mutex_t mtx;
    pthread_cond_t wakeup;
} DispatcherHeartbeatLock;

typedef struct DispatcherHeartbeatThreadData {
    volatile long heartbeat_wait; /*in seconds*/
    const LogContext *log_ctx;
    DispatcherHeartbeatData *segment;
    DispatcherHeartbeatLock *lock;
    volatile bool stop;
    volatile bool started;
} DispatcherHeartbeatThreadData;

#endif //_SAWANG_DISPATCHER_HEARTBEAT_STRUCT_H_
