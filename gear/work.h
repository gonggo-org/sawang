#ifndef _SAWANG_WORK_H_
#define _SAWANG_WORK_H_

#include <stdbool.h>

#include "cJSON.h"
#include "confvar.h"
#include "subscribe_struct.h"
#include "log.h"

extern int work(pid_t pid, const ConfVar *cv_head,
    bool (*init_task)(const LogContext*, void*),
    void (*stop_task)(const LogContext*, void*),
    bool (*valid_task)(const char *),
    void (*task)(const SubscribeJobData*, void*),
    void (*dead_request_handler)(const char*, const LogContext*, void*),
    void *user_data,
    SubscribeReply *subscribe_reply);

#endif //_SAWANG_WORK_H_
