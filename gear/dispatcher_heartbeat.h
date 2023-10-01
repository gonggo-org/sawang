#ifndef _SAWANG_DISPATCHER_HEARTBEAT_H_
#define _SAWANG_DISPATCHER_HEARTBEAT_H_

#include "dispatcher_heartbeat_struct.h"

extern void dispatcher_heartbeat_thread_data_init(DispatcherHeartbeatThreadData *data);
extern bool dispatcher_heartbeat_thread_data_setup(DispatcherHeartbeatThreadData *data,
    const LogContext *log_ctx, const char *gonggo_name,
    long heartbeat_wait, DispatcherHeartbeatLock *lock);
extern void dispatcher_heartbeat_thread_data_destroy(DispatcherHeartbeatThreadData *data);
extern void* dispatcher_heartbeat_thread(void *arg);
extern void dispatcher_heartbeat_thread_resume(DispatcherHeartbeatThreadData *thread_data, long heartbeat_wait);
extern void dispatcher_heartbeat_thread_stop(DispatcherHeartbeatThreadData *thread_data);
extern bool dispatcher_heartbeat_overdue(DispatcherHeartbeatThreadData *thread_data);

#endif //_SAWANG_DISPATCHER_HEARTBEAT_H_
