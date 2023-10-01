#ifndef _SAWANG_SUBSCRIBE_STRUCT_H_
#define _SAWANG_SUBSCRIBE_STRUCT_H_

#include <stdbool.h>
#include <glib.h>

#include "cJSON.h"

#include "define.h"
#include "dispatcher_heartbeat_struct.h"

enum SubscribeState {
    SUBSCRIBE_IDLE = 0,
    SUBSCRIBE_ANSWER = 1,
    SUBSCRIBE_FAILED = 2,
    SUBSCRIBE_DONE = 3
};

typedef struct SubscribeSegmentData {
    pthread_mutex_t mtx;
    pthread_cond_t cond_dispatcher_wakeup;
    pthread_cond_t cond_proxy_wakeup;
    enum SubscribeState state;
    char aid[UUIDBUFLEN]; //answer id
    int payload_buff_length;
    bool remove_request;
} SubscribeSegmentData;

enum SubscribeJobType {
    JOB_TASK = 1,
    JOB_DEAD_REQUEST = 2
};

typedef struct SubscribeJobData {
    char *rid; //request id
    char *task;
    int payload_buff_length;
    void *buff;
    enum SubscribeJobType job_type;
} SubscribeJobData;

typedef struct SubscribeJob {
    pthread_mutex_t mtx;
    pthread_cond_t wakeup;
    GSList *list;
} SubscribeJob;

typedef struct SubscribeThreadData {
    const LogContext *log_ctx;
    SubscribeSegmentData *segment;
    SubscribeJob *job;
    pthread_mutex_t *mtx_subscribe_reply;
    void (*task)(const SubscribeJobData*, void*);
    void (*dead_request_handler)(const char*, const LogContext*, void*);
    void *user_data;
    volatile bool stop;
    volatile bool started;
} SubscribeThreadData;

typedef struct SubscribeReply {
    SubscribeThreadData *subscribe_thread_data;
    DispatcherHeartbeatThreadData *dispatcher_heartbeat_thread_data;
    void (*reply)(const struct SubscribeReply *subscribe_reply, const cJSON *data, bool remove_request);
} SubscribeReply;

#endif //_SAWANG_SUBSCRIBE_STRUCT_H_
