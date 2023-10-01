#ifndef _SAWANG_SUBSCRIBE_H_
#define _SAWANG_SUBSCRIBE_H_

#include "subscribe_struct.h"

extern void subscribe_thread_data_init(SubscribeThreadData *data);
extern bool subscribe_thread_data_setup(SubscribeThreadData *data,
    const LogContext *log_ctx,
    const char *sawang_name,
    SubscribeJob *subscribe_job,
    pthread_mutex_t *mtx_subscribe_reply,
    void (*task)(const SubscribeJobData*, void*),
    void (*dead_request_handler)(const char*, const LogContext*, void*),
    void *user_data);
extern void subscribe_thread_data_destroy(SubscribeThreadData *data, const char *sawang_name);
extern void subscribe_reply_setup(SubscribeReply *subscribe_reply,
    SubscribeThreadData *subscribe_thread_data, DispatcherHeartbeatThreadData *dispatcher_heartbeat_thread_data);
extern void* subscribe_thread(void *arg);
extern void subscribe_thread_stop(SubscribeThreadData* thread_data);
extern void job_list_item_destroy(gpointer data, gpointer user_data);
extern void job_data_destroy(SubscribeJobData *job_data);

#endif //_SAWANG_SUBSCRIBE_H_
