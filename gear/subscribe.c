#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <errno.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include "subscribe.h"
#include "dispatcher_heartbeat.h"
#include "exit.h"

#define SUBSCRIBE_SUFFIX "_subscribe"

static SubscribeSegmentData* subscribe_segment_create(const char *sawang_name, const LogContext *log_ctx);
static void subscribe_segment_init(SubscribeSegmentData *segment);
static char* subscribe_path(const char *sawang_name);
static void job_conversation(const struct SubscribeReply *subscribe_reply, const cJSON *data, bool remove_request);
static void dead_request_parse(const char *buff, const LogContext *log_ctx, void *user_data,
    void (*dead_request_handler)(const char*, const LogContext*, void*));

void subscribe_thread_data_init(SubscribeThreadData *data) {
    data->log_ctx = NULL;
    data->segment = NULL;
    data->job = NULL;
    data->mtx_subscribe_reply = NULL;
    data->task = NULL;
    data->dead_request_handler = NULL;
    data->user_data = NULL;
    data->stop = false;
    data->started = false;
}

bool subscribe_thread_data_setup(SubscribeThreadData *data,
    const LogContext *log_ctx,
    const char *sawang_name,
    SubscribeJob *subscribe_job,
    pthread_mutex_t *mtx_subscribe_reply,
    void (*task)(const SubscribeJobData*, void*),
    void (*dead_request_handler)(const char*, const LogContext*, void*),
    void *user_data)
{
    data->segment =subscribe_segment_create(sawang_name, log_ctx);
    if(data->segment==NULL)
        return false;

    data->log_ctx = log_ctx;
    data->job = subscribe_job;
    data->mtx_subscribe_reply = mtx_subscribe_reply;
    data->task = task;
    data->dead_request_handler = dead_request_handler;
    data->user_data = user_data;
    data->stop = false;
    data->started = false;

    return true;
}

void subscribe_thread_data_destroy(SubscribeThreadData *data, const char *sawang_name) {
    char *sawang_path;

    if(data->segment!=NULL) {
        pthread_mutex_destroy(&data->segment->mtx);
        pthread_cond_destroy(&data->segment->cond_dispatcher_wakeup);
        pthread_cond_destroy(&data->segment->cond_proxy_wakeup);

        munmap(data->segment, sizeof(SubscribeSegmentData));
        if(sawang_name!=NULL) {
            sawang_path = subscribe_path(sawang_name);
            shm_unlink(sawang_path);
            free(sawang_path);
        }
    }
    return;
}

void subscribe_reply_setup(SubscribeReply *subscribe_reply,
    SubscribeThreadData *subscribe_thread_data, DispatcherHeartbeatThreadData *dispatcher_heartbeat_thread_data)
{
    subscribe_reply->subscribe_thread_data = subscribe_thread_data;
    subscribe_reply->dispatcher_heartbeat_thread_data = dispatcher_heartbeat_thread_data;
    subscribe_reply->reply = job_conversation;
    return;
}

static SubscribeSegmentData* subscribe_segment_create(const char *sawang_name, const LogContext *log_ctx) {
    char *sawang_path, buff[PROXYLOGBUFLEN];
    int fd;
    SubscribeSegmentData* segment;
    pthread_mutexattr_t mutexattr;
    pthread_condattr_t condattr;

    sawang_path = subscribe_path(sawang_name);
    segment = NULL;
    fd = -1;
    do {
        fd = shm_open(sawang_path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if(fd==-1) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(log_ctx, "ERROR", "open shared memory %s failed, %s",
                sawang_path, buff);
            break;
        }

        ftruncate(fd, sizeof(SubscribeSegmentData));

        segment = (SubscribeSegmentData*)mmap(NULL, sizeof(SubscribeSegmentData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if( segment == MAP_FAILED ) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(log_ctx, "ERROR", "map shared memory %s failed, %s",
                sawang_path, buff);
            segment = NULL;
            break;
        }

        pthread_mutexattr_init( &mutexattr );
        pthread_mutexattr_setpshared( &mutexattr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust( &mutexattr, PTHREAD_MUTEX_ROBUST );
        if(pthread_mutex_init( &segment->mtx, &mutexattr ) == EBUSY) {
            if( pthread_mutex_consistent(&segment->mtx)==0 )
                pthread_mutex_unlock(&segment->mtx);
        }
        pthread_mutexattr_destroy( &mutexattr );//mutexattr is no longer needed

        pthread_condattr_init ( &condattr );
        pthread_condattr_setpshared( &condattr, PTHREAD_PROCESS_SHARED);
        pthread_cond_init( &segment->cond_dispatcher_wakeup, &condattr );
        pthread_cond_init( &segment->cond_proxy_wakeup, &condattr );
        pthread_condattr_destroy( &condattr );//condattr is no longer needed
    } while(false);

    free(sawang_path);
    if(fd>-1)
        close(fd);

    return segment;
}

void* subscribe_thread(void *arg) {
    SubscribeThreadData *thread_data;
    SubscribeJob *job;
    struct timespec ts;
    SubscribeJobData *job_to_do;
    bool kill;

    thread_data = (SubscribeThreadData*)arg;
    job = thread_data->job;
    subscribe_segment_init(thread_data->segment);

    kill = false;
    thread_data->started = true;
    while( !thread_data->stop ) {
        job_to_do = NULL;

        if( pthread_mutex_lock( &job->mtx )==EOWNERDEAD ) {
            //dispatcher died may put mutex in inconsistent state
            proxy_log(thread_data->log_ctx, "ERROR", "dispatcher left subscribe mutex inconsistent");
            sawang_kill();
            kill = true;
            break;
        }

        if(job->list==NULL) {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 10; //wait 10 seconds from now
            if( pthread_cond_timedwait( &job->wakeup, &job->mtx, &ts )==EOWNERDEAD ) {
                proxy_log(thread_data->log_ctx, "ERROR", "dispatcher left subscribe mutex inconsistent");
                sawang_kill();
                kill = true;
                break;
            }
        }

        if(job->list!=NULL) {
            GSList *last = g_slist_last(job->list);
            gpointer pointer = last->data;
            job_to_do = (SubscribeJobData*)pointer;
            job->list = g_slist_remove(job->list, pointer);
        }

        pthread_mutex_unlock( &job->mtx );

        if(job_to_do!=NULL) {
            if( job_to_do->job_type==JOB_TASK ) {
                if(thread_data->task!=NULL)
                    thread_data->task(job_to_do, thread_data->user_data);
            } else if(  job_to_do->job_type==JOB_DEAD_REQUEST  ) {
                if(thread_data->dead_request_handler!=NULL)
                    dead_request_parse(job_to_do->buff, thread_data->log_ctx, thread_data->user_data,
                        thread_data->dead_request_handler);
            }
            job_data_destroy(job_to_do);
        } else
            usleep(1000); //1000us = 1ms
    }

    if(!kill) {
        pthread_mutex_lock( thread_data->mtx_subscribe_reply );//wait for any job_conversation finished
        pthread_mutex_unlock( thread_data->mtx_subscribe_reply );
    }

    pthread_exit(NULL);
}

void subscribe_thread_stop(SubscribeThreadData* thread_data) {
    thread_data->stop = true;
    pthread_mutex_lock(&thread_data->job->mtx);
    if(thread_data->job->list==NULL)
        pthread_cond_signal(&thread_data->job->wakeup);
    pthread_mutex_unlock(&thread_data->job->mtx);
}

void job_list_item_destroy(gpointer data, gpointer user_data) {
    if(data!=NULL)
        job_data_destroy((SubscribeJobData*)user_data);
    return;
}

void job_data_destroy(SubscribeJobData *job_data) {
    if(job_data->rid!=NULL)
        free(job_data->rid);
    if(job_data->task!=NULL)
        free(job_data->task);
    if(job_data->buff!=NULL)
        free(job_data->buff);
    free(job_data);
}

static void subscribe_segment_init(SubscribeSegmentData *segment) {
    segment->state = SUBSCRIBE_IDLE;
    segment->aid[0] = 0;
    segment->payload_buff_length = 0;
    segment->remove_request = true;
    return;
}

static char* subscribe_path(const char *sawang_name) {
    char *shm_path;

    shm_path = (char*)malloc(strlen(sawang_name) + strlen(SUBSCRIBE_SUFFIX) + 2);
    sprintf(shm_path, "/%s%s", sawang_name, SUBSCRIBE_SUFFIX);
    return shm_path;
}

static void job_conversation(const struct SubscribeReply *subscribe_reply, const cJSON *data, bool remove_request) {
    SubscribeThreadData *thread_data;
    char *str, *aid_path/*answer id path*/, uuid[UUIDBUFLEN], buff[PROXYLOGBUFLEN], *payload_buff;
    uuid_t binuuid;
    int fd, ttl;
    size_t map_len;
    struct timespec ts;

    thread_data = subscribe_reply->subscribe_thread_data;

    if( thread_data->stop ) {
        proxy_log(thread_data->log_ctx, "INFO", "job reply is canceled, subscibe thread is stopping");
        return;
    }

    if( dispatcher_heartbeat_overdue(subscribe_reply->dispatcher_heartbeat_thread_data) ) {
        proxy_log(thread_data->log_ctx, "INFO", "job reply is canceled, dispatcher heartbeat is overdue");
        return;
    }

    pthread_mutex_lock( thread_data->mtx_subscribe_reply );

    if( thread_data->stop ) {//check once more
        proxy_log(thread_data->log_ctx, "INFO", "job reply is canceled, subscibe thread is stopping");
        pthread_mutex_unlock( thread_data->mtx_subscribe_reply );
        return;
    }

    uuid_generate_random(binuuid); //uuid_generate_random is not therad safe
    uuid_unparse_lower(binuuid, uuid);

    str = cJSON_Print(data);

    aid_path = (char*)malloc(strlen(uuid) + 2);
    sprintf(aid_path, "/%s", uuid);

    fd = -1;
    do {
        fd = shm_open(aid_path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if(fd==-1) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(thread_data->log_ctx, "ERROR", "open answer shared memory %s failed, %s",
                aid_path, buff);
            break;
        }
        map_len = strlen(str) + 1;
        ftruncate(fd, map_len);
        payload_buff = (char*)mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if( payload_buff == MAP_FAILED ) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(thread_data->log_ctx, "ERROR", "map answer shared memory %s failed, %s",
                aid_path, buff);
            break;
        }

        strcpy(payload_buff, str);
        free(str);
        str = NULL;
        munmap(payload_buff, map_len);

        if( pthread_mutex_lock( &thread_data->segment->mtx )==EOWNERDEAD ) {
            proxy_log(thread_data->log_ctx, "ERROR", "dispatcher left subscribe mutex inconsistent");
            sawang_kill();
            break;
        }

        thread_data->segment->state = SUBSCRIBE_ANSWER;
        strcpy(thread_data->segment->aid, uuid);
        thread_data->segment->payload_buff_length = map_len;
        thread_data->segment->remove_request = remove_request;
        pthread_cond_signal(&thread_data->segment->cond_dispatcher_wakeup);
        pthread_mutex_unlock( &thread_data->segment->mtx );
        usleep(1000);

        if( pthread_mutex_lock( &thread_data->segment->mtx )==EOWNERDEAD ) {
            proxy_log(thread_data->log_ctx, "ERROR", "dispatcher left subscribe mutex inconsistent");
            sawang_kill();
            break;
        }

        ttl = 5;
        while( thread_data->segment->state!=SUBSCRIBE_FAILED
            && thread_data->segment->state!=SUBSCRIBE_DONE
            && ttl-->0
        ) {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 10; //wait 10 seconds from now
            if( pthread_cond_timedwait( &thread_data->segment->cond_dispatcher_wakeup, &thread_data->segment->mtx, &ts )==EOWNERDEAD ) {
                proxy_log(thread_data->log_ctx, "ERROR", "dispatcher left subscribe mutex inconsistent");
                sawang_kill();
                //kill = true;
                break;
            }
        }

        subscribe_segment_init(thread_data->segment);
        pthread_mutex_unlock( &thread_data->segment->mtx );
    } while(false);

    if(str!=NULL)
        free(str);
    if(fd>-1)
        close(fd);
    shm_unlink(aid_path);
    free(aid_path);

    pthread_mutex_unlock( thread_data->mtx_subscribe_reply );

    return;
}

static void dead_request_parse(const char *buff, const LogContext *log_ctx, void *user_data,
    void (*dead_request_handler)(const char*, const LogContext*, void*))
{
    const char *run;
    char rid[UUIDBUFLEN];

    run = buff;
    while( *run!='\0' ) {
        strncpy(rid, run, UUIDBUFLEN-1);
        rid[UUIDBUFLEN-1] = '\0';
        dead_request_handler(rid, log_ctx, user_data);
        run += (UUIDBUFLEN-1);
    }
}
