#include <unistd.h>

#include "proxycomm.h"
#include "proxysubscribe.h"
#include "respondtable.h"
#include "replyqueue.h"
#include "parsequeue.h"
#include "log.h"
#include "proxyservicestatus.h"

//property
static volatile bool proxy_comm_started = false;
static bool proxy_comm_end = false;
static ProxyStart proxy_comm_f_start = NULL;
static ProxyRun proxy_comm_f_run = NULL;
static ProxyMultiRespondClear proxy_comm_f_multirespond_clear = NULL;
static ProxyStop proy_comm_f_stop = NULL;
static pthread_mutex_t proxy_comm_lock;
static pthread_cond_t proxy_comm_wakeup;

//function
static void proxy_comm_reply(const ProxyReplyArg *arg, cJSON *headers, cJSON *payload);
static void proxy_comm_drop_multirespond_request(const cJSON *payload);
static ProxyReplyArg *proxy_comm_create_reply_arg(const char *task_key);
static void proxy_comm_free(ProxyReplyArg *arg);
static char *task_key_from_reply_arg(const ProxyReplyArg *arg);

void proxy_comm_context_init(ProxyStart f_start, ProxyRun f_run, ProxyMultiRespondClear f_multirespond_clear, ProxyStop f_stop) 
{
    pthread_mutexattr_t mutexattr;
    pthread_condattr_t condattr;

    proxy_comm_f_start = f_start;
    proxy_comm_f_run = f_run;
    proxy_comm_f_multirespond_clear = f_multirespond_clear;
    proy_comm_f_stop = f_stop;

    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_PRIVATE);
    pthread_mutexattr_setrobust(&mutexattr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init(&proxy_comm_lock, &mutexattr);
    pthread_mutexattr_destroy(&mutexattr);//mutexattr is no longer needed

    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_PRIVATE);
    pthread_cond_init(&proxy_comm_wakeup, &condattr);
    pthread_condattr_destroy(&condattr);//condattr is no longer neededs    
}

void proxy_comm_context_destroy(void) {
    pthread_mutex_destroy(&proxy_comm_lock);
    pthread_cond_destroy(&proxy_comm_wakeup);
}

void* proxy_comm(void *arg) {
    ParseQueueTask *task;
    ProxyReplyArg *reply_arg;
    guint remaining;
    bool run;

    proxy_comm_started = true;
    if(proxy_comm_f_start!=NULL) { 
        proxy_comm_f_start((const ProxyCommData*)arg);
    }
    pthread_mutex_lock(&proxy_comm_lock);
    while(!proxy_comm_end) {
        while( (task=parse_queue_pop_head())!=NULL ) {
            if(task->type==RESPONDTABLE_SINGLESHOT || task->type==RESPONDTABLE_MULTIRESPOND) {
                run = true;
                if((task->type==RESPONDTABLE_SINGLESHOT && task->unsubscribe_task_key!=NULL && task->unsubscribe_uuid!=NULL)) {
                    reply_arg = proxy_comm_create_reply_arg(task->unsubscribe_task_key);                    
                    if(respond_table_drop(RESPONDTABLE_MULTIRESPOND, task->unsubscribe_task_key, task->unsubscribe_uuid, &remaining)) {
                        if(remaining<1 && proxy_comm_f_multirespond_clear!=NULL) {
                            proxy_comm_f_multirespond_clear(reply_arg, proxy_comm_free);
                            reply_arg = NULL;//do not free
                        }
                    }                    
                    if(reply_arg!=NULL) {
                        proxy_comm_free(reply_arg);
                    }
                    reply_arg = proxy_comm_create_reply_arg(task->task_key);
                    cJSON *headers = cJSON_CreateObject();
                    cJSON_AddNumberToObject(headers, SERVICE_STATUS_KEY, PROXYSERVICESTATUS_MULTIRESPOND_CLEAR_SUCCESS);
                    cJSON *payload = cJSON_CreateObject();
                    cJSON_AddStringToObject(payload, SERVICE_RID_KEY, task->unsubscribe_uuid);
                    proxy_comm_reply(reply_arg, headers, payload);
                    proxy_comm_free(reply_arg);       
                    run = false;
                }
                if(run) {
                    reply_arg = proxy_comm_create_reply_arg(task->task_key);
                    if(strcmp(reply_arg->service, GONGGOSERVICE_REQUEST_DROP)==0) {
                        proxy_comm_drop_multirespond_request(reply_arg->payload);
                        proxy_comm_free(reply_arg);
                    } else if(proxy_comm_f_run!=NULL){
                        proxy_comm_f_run(reply_arg, proxy_comm_reply, proxy_comm_free);
                    }
                }
            }
            parse_queue_task_destroy(task);            
        }
        pthread_cond_wait(&proxy_comm_wakeup, &proxy_comm_lock);
    }    
    pthread_mutex_unlock(&proxy_comm_lock);
    if(proy_comm_f_stop!=NULL) {
        proy_comm_f_stop();
    }
    pthread_exit(NULL);
}

void proxy_comm_waitfor_started(void) {
	while(!proxy_comm_started) {
		usleep(1000);
	}
}

bool proxy_comm_isstarted(void) {
    return proxy_comm_started;
}

void proxy_comm_awake(void) {
    pthread_mutex_lock(&proxy_comm_lock);
    pthread_cond_signal(&proxy_comm_wakeup);
    pthread_mutex_unlock(&proxy_comm_lock);    
}

void proxy_comm_stop(void) {
    pthread_mutex_lock(&proxy_comm_lock);
    proxy_comm_end = true;
    pthread_cond_signal(&proxy_comm_wakeup);
    pthread_mutex_unlock(&proxy_comm_lock);
}

static void proxy_comm_reply(const ProxyReplyArg *arg, cJSON *headers, cJSON *payload) {
    cJSON *rid;
    guint i;
    char *task_key, *request_uuid;
    GPtrArray *request_uuid_arr; 
    enum RespondTableType which;

    task_key = task_key_from_reply_arg(arg);
    
    which = RESPONDTABLE_SINGLESHOT;    
    if((request_uuid_arr = respond_table_request_dup(which, task_key))==NULL){
        which = RESPONDTABLE_MULTIRESPOND;
        request_uuid_arr = respond_table_request_dup(which, task_key);
    }
    if(request_uuid_arr==NULL) {
        free(task_key);
        return;
    }

    rid = NULL;
    if(request_uuid_arr->len > 1) {//an array
        rid = cJSON_CreateArray();
        for(i=0; i<request_uuid_arr->len; i++) {
            request_uuid = (char*)g_ptr_array_index(request_uuid_arr, i);
            cJSON_AddItemToArray(rid, cJSON_CreateString(request_uuid));
            if(which==RESPONDTABLE_SINGLESHOT) {
                respond_table_drop(RESPONDTABLE_SINGLESHOT, task_key, request_uuid, NULL);                   
            }
        }
    } else if(request_uuid_arr->len==1) {
        request_uuid = (char*)g_ptr_array_index(request_uuid_arr, 0);
        rid = cJSON_CreateString(request_uuid);
    }
    g_ptr_array_free(request_uuid_arr, true);
    free(task_key);

    if(rid!=NULL) {
        reply_queue_append(rid, headers, payload, which==RESPONDTABLE_MULTIRESPOND);
        proxy_subscribe_awake();
    }
}

static void proxy_comm_drop_multirespond_request(const cJSON *payload) {
    cJSON *arr, *item;
    int len, i;
    const char *request_uuid;
    char *unsubscribe_task_key;
    guint remaining;
    ProxyReplyArg *reply_arg;

    arr = cJSON_GetObjectItem(payload, SERVICE_RID_KEY);
    len = arr!=NULL ? cJSON_GetArraySize(arr) : 0;
    for(i=0; i<len; i++) {
        item = cJSON_GetArrayItem(arr, i);
        request_uuid = cJSON_GetStringValue(item);        
        unsubscribe_task_key = request_uuid!=NULL ? respond_table_dup_task_key(RESPONDTABLE_MULTIRESPOND, request_uuid) : NULL;
        if(unsubscribe_task_key!=NULL) {
            if(respond_table_drop(RESPONDTABLE_MULTIRESPOND, unsubscribe_task_key, request_uuid, &remaining)){
                if(remaining<1 && proxy_comm_f_multirespond_clear!=NULL) {
                    reply_arg = proxy_comm_create_reply_arg(unsubscribe_task_key);
                    proxy_comm_f_multirespond_clear(reply_arg, proxy_comm_free);
                }
            }
            free(unsubscribe_task_key);
        }
    }
}

static ProxyReplyArg *proxy_comm_create_reply_arg(const char *task_key) {
    ProxyReplyArg *arg;
    cJSON *j, *item;

    arg = (ProxyReplyArg*)malloc(sizeof(ProxyReplyArg));
    j = cJSON_Parse(task_key);

    item = cJSON_GetObjectItem(j, SERVICE_SERVICE_KEY);
    arg->service = strdup(cJSON_GetStringValue(item));

    item = cJSON_GetObjectItem(j, SERVICE_PAYLOAD_KEY);
    arg->payload = item!=NULL ? cJSON_Duplicate(item, true) : NULL;

    cJSON_Delete(j);

    return arg;
}

static void proxy_comm_free(ProxyReplyArg *arg) {
    free(arg->service);
    if(arg->payload!=NULL) {
        cJSON_Delete(arg->payload);
    }
    free(arg);
}

static char *task_key_from_reply_arg(const ProxyReplyArg *arg) {
    cJSON *j;
    char *s;

    j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, SERVICE_SERVICE_KEY, cJSON_CreateString(arg->service));
    if(arg->payload!=NULL) {
        cJSON_AddItemToObject(j, SERVICE_PAYLOAD_KEY, cJSON_Duplicate(arg->payload, true));
    }
    s = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);

    return s;
}