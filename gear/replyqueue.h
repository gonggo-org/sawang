#ifndef _REPLYQUEUE_H_
#define _REPLYQUEUE_H_

#include <stdbool.h>
#include <glib.h>

#include "cJSON.h"

typedef struct ReplyQueueTask {
    char *task;
    bool multiple_respond;
} ReplyQueueTask;

extern void reply_queue_create(void);
extern void reply_queue_destroy(void);
extern void reply_queue_append(cJSON *rid, cJSON *headers, cJSON *payload, bool multiple_respond);
extern void reply_queue_append_invalid_status(const char *rid, int status);
extern ReplyQueueTask *reply_queue_pop_head(void);
extern void reply_queue_push_head(GQueue *src);

#endif //_REPLYQUEUE_H_