#include <glib.h>

#include "parsequeue.h"

static GQueue *parse_queue = NULL;
static pthread_mutex_t parse_queue_lock;

void parse_queue_create(void) {
    pthread_mutexattr_t mtx_attr;

    if(parse_queue==NULL) {
        pthread_mutexattr_init(&mtx_attr);
        pthread_mutexattr_setpshared(&mtx_attr, PTHREAD_PROCESS_PRIVATE);
        pthread_mutexattr_settype(&mtx_attr, PTHREAD_MUTEX_NORMAL);
        pthread_mutex_init(&parse_queue_lock, &mtx_attr);
        pthread_mutexattr_destroy(&mtx_attr);

        parse_queue = g_queue_new();
    }
}

void parse_queue_destroy(void) {
    if(parse_queue!=NULL) {
        g_queue_free_full(parse_queue, (GDestroyNotify)parse_queue_task_destroy);
        parse_queue = NULL;
        pthread_mutex_destroy(&parse_queue_lock);
    }
}

void parse_queue_append(const char *task_key, const char *unsubscribe_task_key, const char *unsubscribe_uuid, enum RespondTableType type) {
    ParseQueueTask *t;

    t = (ParseQueueTask*)malloc(sizeof(ParseQueueTask));
    t->task_key = strdup(task_key);
    t->unsubscribe_task_key = unsubscribe_task_key!=NULL && strlen(unsubscribe_task_key)>0 ? strdup(unsubscribe_task_key) : NULL;
    t->unsubscribe_uuid = unsubscribe_uuid!=NULL && strlen(unsubscribe_uuid)>0 ? strdup(unsubscribe_uuid) : NULL;
    t->type = t->unsubscribe_task_key!=NULL ? RESPONDTABLE_SINGLESHOT : type;

    pthread_mutex_lock(&parse_queue_lock);
    g_queue_push_tail(parse_queue, t);
    pthread_mutex_unlock(&parse_queue_lock);
}

ParseQueueTask *parse_queue_pop_head() {
    ParseQueueTask *t;

    pthread_mutex_lock(&parse_queue_lock);
    t = (ParseQueueTask*)g_queue_pop_head(parse_queue);
    pthread_mutex_unlock(&parse_queue_lock);
    return t;
}

void parse_queue_task_destroy(ParseQueueTask* task) {
    if(task!=NULL) {
        if(task->task_key!=NULL) {
            free(task->task_key);
        }
        if(task->unsubscribe_task_key!=NULL) {
            free(task->unsubscribe_task_key);
        }
        if(task->unsubscribe_uuid!=NULL) {
            free(task->unsubscribe_uuid);
        }
        free(task);
    }
}