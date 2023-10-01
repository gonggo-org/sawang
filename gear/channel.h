#ifndef _SAWANG_CHANNEL_H_
#define _SAWANG_CHANNEL_H_

#include <stdbool.h>

#include "log.h"
#include "define.h"
#include "subscribe.h"

enum ChannelState {
    CHANNEL_IDLE = 0,
    CHANNEL_REQUEST = 1,
    CHANNEL_INVALID_TASK = 2,
    CHANNEL_INVALID_PAYLOAD = 3,
    CHANNEL_ACKNOWLEDGED = 4,
    CHANNEL_DONE = 5,
    CHANNEL_STOP = 6,
    CHANNEL_REQUEST_REMOVE = 7
};

typedef struct ChannelSegmentData {
    pthread_mutex_t mtx;
    pthread_cond_t cond_idle;
    pthread_cond_t cond_dispatcher_wakeup;
    pthread_cond_t cond_proxy_wakeup;
    enum ChannelState state;
    char rid[UUIDBUFLEN]; //request id
    char task[TASKBUFLEN];
    unsigned int payload_buff_length;
} ChannelSegmentData;

typedef struct ChannelThreadData {
    const char* sawang_name;
    const LogContext *log_ctx;
    ChannelSegmentData *segment;
    SubscribeJob *job;
    bool (*valid_task)(const char *);
    volatile bool stop;
    volatile bool started;
} ChannelThreadData;

extern void channel_thread_data_init(ChannelThreadData *data);
extern bool channel_thread_data_setup(ChannelThreadData *data,
    const LogContext *log_ctx,
    const char *sawang_name,
    SubscribeJob *subscribe_job,
    bool (*valid_task)(const char *));
extern void channel_thread_data_destroy(ChannelThreadData *data);
extern void* channel_thread(void *arg);
extern void channel_thread_stop(ChannelThreadData *thread_data);

#endif //_SAWANG_CHANNEL_H_
