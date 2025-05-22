#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "log.h"
#include "proxy.h"
#include "util.h"
#include "globaldata.h"
#include "cJSON.h"
#include "callback.h"
#include "respondtable.h"
#include "replyqueue.h"
#include "proxysubscribe.h"
#include "proxycomm.h"
#include "parsequeue.h"
#include "proxyservicestatus.h"
#include "proxyuuid.h"

#define CHANNEL_SUFFIX "_channel"

//property
static char *proxy_channel_path = NULL;
static volatile bool proxy_channel_started = false;
static bool proxy_channel_end = false;
static bool proxy_channel_shm_unlink = false;
static ProxyChannelShm *proxy_channel_shm = NULL;
static ProxyPayloadParse proxy_channel_payload_parse = NULL;
static ProxyRest proxy_rest = NULL;

//function
static char* proxy_channel_path_create(void);
static bool proxy_channel_shm_create(const char *proxy_path);
static void proxy_channel_shm_idle(void);
static bool proxy_channel_exchange(void);
static bool proxy_channel_rest(const ConfVar *cv_head);
static cJSON* proxy_channel_payload_shm_read(const char *rid, size_t buff_length);
static char* proxy_channel_respond_create(int code, const char* err, cJSON *json);
static size_t proxy_rest_create_answer(const char *path, const char *respond);

bool proxy_channel_context_init(ProxyPayloadParse f_payload_parse, ProxyRest f_rest) 
{
    proxy_channel_path = proxy_channel_path_create();
    if(!proxy_channel_shm_create(proxy_channel_path)) {
        free(proxy_channel_path);
        proxy_channel_path = NULL;
        return false;
    }
    proxy_channel_payload_parse = f_payload_parse;
    proxy_rest = f_rest;
    return true;
}

void proxy_channel_shm_unlink_enable(void) {
    proxy_channel_shm_unlink = true;
}

void proxy_channel_context_destroy(void) {
    if(proxy_channel_shm!=NULL) {
        if(proxy_channel_shm_unlink) {
            pthread_mutex_destroy(&proxy_channel_shm->lock);

            proxy_cond_reset(&proxy_channel_shm->dispatcher_wakeup);
            pthread_cond_destroy(&proxy_channel_shm->dispatcher_wakeup);

            proxy_cond_reset(&proxy_channel_shm->proxy_wakeup);
            pthread_cond_destroy(&proxy_channel_shm->proxy_wakeup);

            proxy_cond_reset(&proxy_channel_shm->idle);
            pthread_cond_destroy(&proxy_channel_shm->idle);
        }
        munmap(proxy_channel_shm, sizeof(ProxyChannelShm));
        proxy_channel_shm = NULL;   
    }
    if(proxy_channel_path!=NULL) {
        if(proxy_channel_shm_unlink) {
            shm_unlink(proxy_channel_path);
        }
        free(proxy_channel_path);
        proxy_channel_path = NULL;
    }
}

void* proxy_channel(void *arg) {
    const ConfVar *cv_head;

    cv_head = (const ConfVar *)arg;

    proxy_log("INFO", "proxy %s channel thread is started", proxy_name);

    if(pthread_mutex_lock(&proxy_channel_shm->lock) == EOWNERDEAD) {
        pthread_mutex_consistent(&proxy_channel_shm->lock);//resurrection
    }

    proxy_channel_started = true;

    while(!proxy_channel_end) {
        proxy_channel_shm_idle();//set state to CHANNEL_IDLE
        pthread_cond_signal(&proxy_channel_shm->idle);

        if(pthread_cond_wait(&proxy_channel_shm->proxy_wakeup, &proxy_channel_shm->lock)==EOWNERDEAD) {
            pthread_mutex_consistent(&proxy_channel_shm->lock);
            proxy_log("INFO", "proxy %s channel waits wakeup with inconsistent mutex indicating gonggo dead", proxy_name);
            break;
        } else if(proxy_channel_shm->state == CHANNEL_TERMINATION) {
            proxy_log("INFO", "proxy %s channel waits wakeup with CHANNEL_TERMINATION", proxy_name);
            break;
        } else if(proxy_channel_shm->state==CHANNEL_STOP_REQUEST) {
            proxy_log("INFO", "proxy %s channel receive CHANNEL_STOP_REQUEST", proxy_name);
            proxy_exit = true;
            kill(getpid(), SIGTERM);
        } else if (proxy_channel_shm->state==CHANNEL_REQUEST) {
            proxy_log("INFO", "proxy %s channel receive CHANNEL_REQUEST", proxy_name);
            if(!proxy_channel_exchange()){
                break;
            }
        } else if (proxy_channel_shm->state==CHANNEL_REST) {
            proxy_log("INFO", "proxy %s channel receive CHANNEL_REST", proxy_name);
            if(!proxy_channel_rest(cv_head)) {
                break;
            }
        }
    }

    pthread_mutex_unlock(&proxy_channel_shm->lock);

    proxy_log("INFO", "proxy %s channel thread is stopped", proxy_name);
    pthread_exit(NULL);
}

void proxy_channel_waitfor_started(void) {
	while(!proxy_channel_started) {
		usleep(1000);
	}
}

bool proxy_channel_isstarted(void) {
    return proxy_channel_started;
}

void proxy_channel_stop(void) {
    if(pthread_mutex_lock(&proxy_channel_shm->lock)==EOWNERDEAD) {
        pthread_mutex_consistent(&proxy_channel_shm->lock);//resurrection
    }
    proxy_channel_shm->state = CHANNEL_TERMINATION;
    proxy_channel_end = true;
    pthread_cond_signal(&proxy_channel_shm->proxy_wakeup);
    pthread_mutex_unlock(&proxy_channel_shm->lock);
}

static char* proxy_channel_path_create() {
    char *shm_path;

    shm_path = (char*)malloc(strlen(proxy_name) + strlen(CHANNEL_SUFFIX) + 2);
    sprintf(shm_path, "/%s%s", proxy_name, CHANNEL_SUFFIX);
    return shm_path;
}

static bool proxy_channel_shm_create(const char *proxy_path) {
    int fd;
    char buff[PROXYLOGBUFLEN];
    pthread_mutexattr_t mutexattr;
    pthread_condattr_t condattr;

    fd = shm_open(proxy_path, O_RDWR, S_IRUSR | S_IWUSR);
    if( errno == ENOENT ) {
        fd = shm_open(proxy_path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if(fd>-1) {
            ftruncate(fd, sizeof(ProxyChannelShm)); //set size
        }
    }

    if(fd==-1) {
        strerror_r(errno, buff, PROXYLOGBUFLEN);
        proxy_log("ERROR", "shm %s creation failed, %s", proxy_path, buff);
        return false;
    }

    proxy_channel_shm = (ProxyChannelShm*)mmap(NULL, sizeof(ProxyChannelShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutexattr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_NORMAL);
    if(pthread_mutex_init(&proxy_channel_shm->lock, &mutexattr) == EBUSY) {
        if(pthread_mutex_consistent(&proxy_channel_shm->lock) == 0) {
            pthread_mutex_unlock(&proxy_channel_shm->lock);
        }
    }
    pthread_mutexattr_destroy(&mutexattr);//mutexattr is no longer needed

    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&proxy_channel_shm->dispatcher_wakeup, &condattr);
    pthread_cond_init(&proxy_channel_shm->proxy_wakeup, &condattr);
    pthread_cond_init(&proxy_channel_shm->idle, &condattr);
    pthread_condattr_destroy(&condattr);//condattr is no longer needed

    proxy_channel_shm->state = CHANNEL_INIT;
    proxy_channel_shm->rid[0] = 0;
    proxy_channel_shm->payload_buff_length = 0;

    return true;
}

static void proxy_channel_shm_idle() {
    proxy_channel_shm->state = CHANNEL_IDLE;
    proxy_channel_shm->rid[0] = 0; //request id
    proxy_channel_shm->payload_buff_length = 0;
}

static char *proxy_channel_create_unsubscribe_task_key(const char *service_name, const cJSON *payload, enum ProxyServiceStatus *status, const char**rid)
{
    cJSON *item;
    char *unsubscribe_task_key;

    *rid = NULL;

    if(payload==NULL) {
        proxy_log("ERROR", "multirespond unsubscribe service %s does not have payload", service_name);
        *status = PROXYSERVICESTATUS_MULTIRESPOND_CLEAR_PAYLOAD_MISSING;
        return NULL;
    }

    item = payload!=NULL ? cJSON_GetObjectItem(payload, SERVICE_RID_KEY) : NULL;
    *rid = (const char*)(item!=NULL ? cJSON_GetStringValue(item) : NULL);
    if(*rid==NULL || strlen(*rid)<1) {
        proxy_log("ERROR", "multirespond unsubscribe service %s payload does not have rid under key %s", service_name, SERVICE_RID_KEY);
        *status = PROXYSERVICESTATUS_MULTIRESPOND_CLEAR_PAYLOAD_RID_MISSING;
        return NULL;
    }

    unsubscribe_task_key = respond_table_dup_task_key(RESPONDTABLE_MULTIRESPOND, *rid);
    if(unsubscribe_task_key==NULL) {
        proxy_log("ERROR", "multirespond unsubscribe service %s rid does not exists", service_name);
        *status = PROXYSERVICESTATUS_MULTIRESPOND_CLEAR_PAYLOAD_RID_INVALID;
        return NULL;        
    }

    *status = PROXYSERVICESTATUS_MULTIRESPOND_CLEAR_SUCCESS;
    return unsubscribe_task_key;
}

static bool proxy_channel_exchange(void) {
    cJSON *service_and_payload, *norm_service_and_payload, *normalized_payload, *service, *payload;
    bool new_job;
    const char *service_name;
    unsigned int invalid_status;
    char *task_key, *unsubscribe_task_key;
    const char *request_uuid;
    enum ProxyPayloadParseResult parseResult = PARSE_INVALID;
    enum RespondTableType respond_table_type;
    bool alive = true, unsubscribe = false;
    enum ProxyServiceStatus proxy_service_status;

    norm_service_and_payload = NULL;
    normalized_payload = NULL;
    invalid_status = 0;
    payload = NULL;    
    unsubscribe_task_key = NULL;
    service_name = "";

    service_and_payload = proxy_channel_payload_shm_read(proxy_channel_shm->rid, proxy_channel_shm->payload_buff_length);
    if(service_and_payload==NULL) {
        proxy_channel_shm->state = CHANNEL_FAILS;        
    } else {
        service = cJSON_GetObjectItem(service_and_payload, SERVICE_SERVICE_KEY);
        payload = cJSON_GetObjectItem(service_and_payload, SERVICE_PAYLOAD_KEY);//optional
        service_name = service!=NULL ? cJSON_GetStringValue(service) : "";
        if(strcmp(service_name, GONGGOSERVICE_REQUEST_DROP)==0) {
            parseResult = PARSE_SINGLESHOT;
            normalized_payload = payload;
            unsubscribe = true;
        } else {
            parseResult = proxy_channel_payload_parse(service_name, payload, &normalized_payload, &unsubscribe, &invalid_status);
        }
        proxy_channel_shm->state = CHANNEL_ACKNOWLEDGED;
    }
    pthread_cond_signal(&proxy_channel_shm->dispatcher_wakeup); 

    do {
        proxy_log("INFO", "proxy %s channel waits proxy_wakeup after signaling dispatcher_wakeup", proxy_name);
        if(pthread_cond_wait(&proxy_channel_shm->proxy_wakeup, &proxy_channel_shm->lock)==EOWNERDEAD){
            proxy_log("INFO", "proxy %s channel detects inconsistent mutex while waiting proxy_wakeup", proxy_name);
            pthread_mutex_consistent(&proxy_channel_shm->lock);
            alive = false;
            break;
        }
        proxy_log("INFO", "proxy %s channel got proxy_wakeup signals with status %ld", proxy_name, proxy_channel_shm->state);
        if(proxy_channel_shm->state == CHANNEL_TERMINATION) {
            alive = false;
            break;
        }
        if(service_and_payload!=NULL && proxy_channel_shm->state==CHANNEL_DONE) {
            if(parseResult==PARSE_INVALID) {
                reply_queue_append_invalid_status(proxy_channel_shm->rid, invalid_status);
                proxy_subscribe_awake();
            } else {                
                proxy_service_status = PROXYSERVICESTATUS_MULTIRESPOND_CLEAR_SUCCESS;
                if(unsubscribe) {
                    unsubscribe_task_key = proxy_channel_create_unsubscribe_task_key(service_name, payload, &proxy_service_status, &request_uuid);
                    if(unsubscribe_task_key==NULL) {
                        reply_queue_append_invalid_status(proxy_channel_shm->rid, proxy_service_status);
                        proxy_subscribe_awake();
                    } else {
                        task_key = cJSON_PrintUnformatted(service_and_payload);                        
                        parse_queue_append(task_key, unsubscribe_task_key, request_uuid, RESPONDTABLE_SINGLESHOT);
                        respond_table_set(RESPONDTABLE_SINGLESHOT, task_key, proxy_channel_shm->rid);
                        proxy_comm_awake();
                        free(task_key);
                    }
                } else {
                    if(payload==NULL || payload==normalized_payload) {
                        norm_service_and_payload = service_and_payload;
                    } else {
                        norm_service_and_payload = cJSON_CreateObject();
                        cJSON_AddItemToObject(norm_service_and_payload, SERVICE_SERVICE_KEY, cJSON_CreateString(service_name));
                        if(normalized_payload!=NULL) {        
                            cJSON_AddItemToObject(norm_service_and_payload, SERVICE_PAYLOAD_KEY, cJSON_Duplicate(normalized_payload, true));
                        }                    
                    }
                    task_key = cJSON_PrintUnformatted(norm_service_and_payload);            
                    new_job = true;
                    respond_table_type = parseResult==PARSE_MULTIRESPOND && unsubscribe_task_key==NULL ? RESPONDTABLE_MULTIRESPOND
                        : RESPONDTABLE_SINGLESHOT;                    
                    if(respond_table_type==RESPONDTABLE_MULTIRESPOND) {
                        new_job = respond_table_set(RESPONDTABLE_MULTIRESPOND, task_key, proxy_channel_shm->rid);
                    } else {
                        respond_table_set(RESPONDTABLE_SINGLESHOT, task_key, proxy_channel_shm->rid);
                    }
                    if(new_job) {
                        parse_queue_append(task_key, NULL, NULL, respond_table_type);
                        proxy_comm_awake();
                    }
                    free(task_key);
                }
            }//if(parseResult!=PARSE_INVALID) {
        }
    } while(false);

    if(unsubscribe_task_key!=NULL) {
        free(unsubscribe_task_key);
    }
    if(normalized_payload!=NULL && (payload==NULL || normalized_payload!=payload)) {
        cJSON_Delete(normalized_payload);
    }
    if(norm_service_and_payload!=NULL && (service_and_payload==NULL || norm_service_and_payload!=service_and_payload)) {
        cJSON_Delete(norm_service_and_payload);
    }
    if(service_and_payload!=NULL) {
        cJSON_Delete(service_and_payload);
    }

    return alive;
}

static char* proxy_channel_respond_create(int code, const char* err, cJSON *json) {
    cJSON *respond;
    char *s;

    respond = cJSON_CreateObject();
    cJSON_AddNumberToObject(respond, "code", code);
    if(err!=NULL && strlen(err)>0) {
        cJSON_AddStringToObject(respond, "err", err);
    }
    if(json!=NULL) {
        cJSON_AddItemToObject(respond, "json", json);
    }
    s = cJSON_PrintUnformatted(respond);
    cJSON_Delete(respond);

    return s;
}

static bool proxy_channel_rest(const ConfVar *cv_head) {
    bool alive = true;
    cJSON *service_and_payload = NULL, *service, *payload;
    const char *endpoint;
    char *respond = NULL, *answer_path;
    ProxyRestRespond rest_respond;
    
    respond = NULL;

    do {
        if(proxy_rest==NULL) {
            respond = proxy_channel_respond_create(503, "REST handler is not implemented", NULL);
            break;
        }
        service_and_payload = proxy_channel_payload_shm_read(proxy_channel_shm->rid, proxy_channel_shm->payload_buff_length);
        if(service_and_payload==NULL) {
            respond = proxy_channel_respond_create(500, "REST payload read is failed", NULL);
            break;
        } else {
            service = cJSON_GetObjectItem(service_and_payload, SERVICE_SERVICE_KEY);
            payload = cJSON_GetObjectItem(service_and_payload, SERVICE_PAYLOAD_KEY);//optional
            endpoint = service!=NULL ? cJSON_GetStringValue(service) : "";
            rest_respond.code = 0;
            rest_respond.err = NULL;
            rest_respond.json = NULL;
            proxy_rest(cv_head, endpoint, payload, &rest_respond);            
            if(rest_respond.code == 0) {
                rest_respond.code = 503;
                rest_respond.err = strdup("REST endpoint is not implemented");
            }
            respond = proxy_channel_respond_create(rest_respond.code, rest_respond.err, rest_respond.json);
            if(rest_respond.err!=NULL) {
                free(rest_respond.err);
            }
            //rest_respond.json allocation is handled by proxy_channel_respond_create, do not delete
        }
        cJSON_Delete(service_and_payload);  
    } while(false);

    if(respond!=NULL) {
        proxy_uuid_generate(proxy_channel_shm->aid);
        answer_path = (char*)malloc(strlen(proxy_channel_shm->aid) + 2);
        sprintf(answer_path, "/%s", proxy_channel_shm->aid);
        proxy_channel_shm->answer_buff_length = proxy_rest_create_answer(answer_path, respond);
        if(proxy_channel_shm->answer_buff_length>0) {
            proxy_channel_shm->state = CHANNEL_REST_RESPOND;
            pthread_cond_signal(&proxy_channel_shm->dispatcher_wakeup);
            if(pthread_cond_wait(&proxy_channel_shm->proxy_wakeup, &proxy_channel_shm->lock)==EOWNERDEAD){
                pthread_mutex_consistent(&proxy_channel_shm->lock);
                alive = false;
            }
            shm_unlink(answer_path);
        }
        free(respond);
        free(answer_path);
    }

    return alive;
}

static cJSON* proxy_channel_payload_shm_read(const char *rid, size_t buff_length) {
    char *path;
    int fd;
    char buff[PROXYLOGBUFLEN];
    char *map;
    cJSON *json;

    path = (char*)malloc( strlen(rid)+2 );
    sprintf(path, "/%s", rid);

    fd = shm_open(path, O_RDWR, S_IRUSR | S_IWUSR);
    if(fd==-1) {
        strerror_r(errno, buff, PROXYLOGBUFLEN);
        proxy_log("ERROR", "proxy %s fails to open channel payload shared memory %s, %s", proxy_name, path, buff);
        free(path);
        return NULL;
    }    

    map = (char*)mmap(NULL, buff_length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if( map == MAP_FAILED ) {
        strerror_r(errno, buff, PROXYLOGBUFLEN);
        proxy_log("ERROR", "proxy %s fails to map channel payload shared memory %s, %s", proxy_name, path, buff);
        free(path);
        close(fd);
        return NULL;
    }

    json = cJSON_Parse(map);
    munmap(map, buff_length);
    free(path);
    close(fd);

    return json;
}

static size_t proxy_rest_create_answer(const char *path, const char *respond) {
    char buff[PROXYLOGBUFLEN], *shm_buff;
    int fd = -1;
    size_t buff_len;

    do {
        fd = shm_open(path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if(fd==-1) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log("ERROR", "proxy %s REST answer shared memory creation failed, %s", proxy_name, buff);
            buff_len = 0;
            break;
        }
        buff_len = strlen(respond) + 1;
        ftruncate(fd, buff_len);

        shm_buff = (char*)mmap(NULL, buff_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if(shm_buff==MAP_FAILED) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log("ERROR", "proxy %s REST answer shared memory map failed, %s", proxy_name, buff);
            shm_unlink(path);
            buff_len = 0;
            break;
        }

        strcpy(shm_buff, respond);
        munmap(shm_buff, buff_len);
    } while(false);

    if(fd!=-1) {
        close(fd);
    }

    return buff_len;
}