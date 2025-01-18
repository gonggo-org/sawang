#ifndef _PARSEQUEUE_H_
#define _PARSEQUEUE_H_

#include "respondtable.h"

typedef struct ParseQueueTask {
    char *task_key;
    char *unsubscribe_task_key;
    char *unsubscribe_uuid;
    enum RespondTableType type;
} ParseQueueTask;

extern void parse_queue_create(void);
extern void parse_queue_destroy(void);
extern void parse_queue_append(const char *task_key, const char *unsubscribe_task_key, const char *unsubscribe_uuid, enum RespondTableType type);
extern ParseQueueTask *parse_queue_pop_head();
extern void parse_queue_task_destroy(ParseQueueTask* task);

#endif //_PARSEQUEUE_H_