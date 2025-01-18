#include "define.h"
#include "replyqueue.h"

static GQueue *reply_queue = NULL;
static pthread_mutex_t reply_queue_lock;
static void reply_queue_task_destroy(ReplyQueueTask* task);

void reply_queue_create(void) {
    pthread_mutexattr_t mtx_attr;

    if(reply_queue==NULL) {
        pthread_mutexattr_init(&mtx_attr);
        pthread_mutexattr_setpshared(&mtx_attr, PTHREAD_PROCESS_PRIVATE);
        pthread_mutexattr_settype(&mtx_attr, PTHREAD_MUTEX_NORMAL);
        pthread_mutex_init(&reply_queue_lock, &mtx_attr);
        pthread_mutexattr_destroy(&mtx_attr);

        reply_queue = g_queue_new();
    }
}

void reply_queue_destroy(void) {
    if(reply_queue!=NULL) {
        g_queue_free_full(reply_queue, (GDestroyNotify)reply_queue_task_destroy);
        reply_queue = NULL;
        pthread_mutex_destroy(&reply_queue_lock);
    }
}

void reply_queue_append(cJSON *rid, cJSON *headers, cJSON *payload, bool multiple_respond) {
    cJSON *j;
    ReplyQueueTask *t;

    t = (ReplyQueueTask*)malloc(sizeof(ReplyQueueTask));

    j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, SERVICE_RID_KEY, rid);
    cJSON_AddItemToObject(j, SERVICE_HEADERS_KEY, headers);
    if(payload!=NULL) {
        cJSON_AddItemToObject(j, SERVICE_PAYLOAD_KEY, payload);
    }
    t->task = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    t->multiple_respond = multiple_respond;

    pthread_mutex_lock(&reply_queue_lock);
    g_queue_push_tail(reply_queue, t);
    pthread_mutex_unlock(&reply_queue_lock);
}

void reply_queue_append_invalid_status(const char *rid, int status) {
    cJSON *headers;

    headers = cJSON_CreateObject();
    cJSON_AddNumberToObject(headers, SERVICE_STATUS_KEY, status);
    reply_queue_append(cJSON_CreateString(rid), headers, NULL, false);
}

ReplyQueueTask *reply_queue_pop_head(void) {
    ReplyQueueTask *t;

    pthread_mutex_lock(&reply_queue_lock);
    t = (ReplyQueueTask*)g_queue_pop_head(reply_queue);
    pthread_mutex_unlock(&reply_queue_lock);
    return t;
}

void reply_queue_push_head(GQueue *src) {
    ReplyQueueTask *t;

    pthread_mutex_lock(&reply_queue_lock);
    while( (t = g_queue_pop_tail(src))!=NULL ) {
        g_queue_push_head(reply_queue, t);
    }   
    pthread_mutex_unlock(&reply_queue_lock);
}

static void reply_queue_task_destroy(ReplyQueueTask* task) {
    if(task!=NULL) {
        if(task->task!=NULL) {
            free(task->task);
        }
        free(task);
    }
}