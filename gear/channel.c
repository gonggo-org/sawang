#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <errno.h>
#include <unistd.h>

#include "channel.h"
#include "exit.h"

#define CHANNEL_SUFFIX "_channel"
#define REMOVE_REQUEST_SUFFIX "_requestremove"

static ChannelSegmentData* channel_segment_create(const char *sawang_name, const LogContext *log_ctx);
static void channel_segment_init(ChannelSegmentData *segment);
static char* channel_path(const char *sawang_name);

static SubscribeJobData* channel_conversation(ChannelSegmentData *segment, bool (*valid_task)(const char *), const LogContext *log_ctx);
static void* channel_payload(const char *rid, unsigned int buff_length, const LogContext *log_ctx);

static SubscribeJobData* channel_dead_request_conversation(const char *sawang_name, ChannelSegmentData *segment, const LogContext *log_ctx);
static void* channel_dead_request_payload(const char *sawang_name, unsigned int buff_length, const LogContext *log_ctx);

void channel_thread_data_init(ChannelThreadData *data) {
    data->sawang_name = NULL;
    data->log_ctx = NULL;
    data->segment = NULL;
    data->job = NULL;
    data->valid_task = NULL;
    data->stop = false;
    data->started = false;
}

bool channel_thread_data_setup(ChannelThreadData *data,
    const LogContext *log_ctx,
    const char *sawang_name,
    SubscribeJob *subscribe_job,
    bool (*valid_task)(const char *))
{
    data->segment = channel_segment_create(sawang_name, log_ctx);
    if( data->segment==NULL )
        return false;
    data->sawang_name = sawang_name;
    data->log_ctx = log_ctx;
    data->job = subscribe_job;
    data->valid_task = valid_task;
    data->stop = false;
    data->started = false;
    return true;
}

void channel_thread_data_destroy(ChannelThreadData *data) {
    char *sawang_path;

    if(data->segment!=NULL) {
        pthread_mutex_destroy( &data->segment->mtx );
        pthread_cond_destroy( &data->segment->cond_idle );
        pthread_cond_destroy( &data->segment->cond_dispatcher_wakeup );
        pthread_cond_destroy( &data->segment->cond_proxy_wakeup );

        munmap(data->segment, sizeof(ChannelSegmentData));
        if(data->sawang_name!=NULL) {
            sawang_path = channel_path(data->sawang_name);
            shm_unlink(sawang_path);
            free(sawang_path);
            data->sawang_name = NULL;
        }
    }
    return;
}

static ChannelSegmentData* channel_segment_create(const char *sawang_name, const LogContext *log_ctx) {
    char *sawang_path, buff[PROXYLOGBUFLEN];
    int fd;
    ChannelSegmentData* segment;
    pthread_mutexattr_t mutexattr;
    pthread_condattr_t condattr;

    sawang_path = channel_path(sawang_name);
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

        ftruncate(fd, sizeof(ChannelSegmentData));

        segment = (ChannelSegmentData*)mmap(NULL, sizeof(ChannelSegmentData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
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
        pthread_cond_init( &segment->cond_idle, &condattr );
        pthread_cond_init( &segment->cond_dispatcher_wakeup, &condattr );
        pthread_cond_init( &segment->cond_proxy_wakeup, &condattr );
        pthread_condattr_destroy( &condattr );//condattr is no longer needed
    } while(false);

    free(sawang_path);
    if(fd>-1)
        close(fd);

    return segment;
}

void* channel_thread(void *arg) {
    ChannelThreadData *td;
    ChannelSegmentData *segment;
    struct timespec ts;
    SubscribeJob *job;
    SubscribeJobData *job_data;

    td = (ChannelThreadData*)arg;
    segment = td->segment;
    job = td->job;
    channel_segment_init(segment);

    td->started = true;
    while( !td->stop ) {
        if( pthread_mutex_lock( &segment->mtx )==EOWNERDEAD ) {
            //dispatcher died may put mutex in inconsistent state
            proxy_log(td->log_ctx, "ERROR", "dispatcher left channel mutex inconsistent");
            sawang_kill();
            break;
        }

        if( td->segment->state != CHANNEL_REQUEST
            && td->segment->state != CHANNEL_STOP
            && td->segment->state != CHANNEL_REQUEST_REMOVE
        ) {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 10; //wait 10 seconds from now
            if( pthread_cond_timedwait( &segment->cond_proxy_wakeup, &segment->mtx, &ts )==EOWNERDEAD ) {
                //dispatcher died may put mutex in inconsistent state
                proxy_log(td->log_ctx, "ERROR", "dispatcher left channel mutex inconsistent");
                sawang_kill();
                break;
            }
        }

        if( td->segment->state == CHANNEL_STOP ) {
            pthread_mutex_unlock( &segment->mtx );
            dispatcher_deactivate_on_exit = false;
            sawang_exit = true;
            kill(getpid(), SIGTERM);
            break;
        } else if( td->segment->state == CHANNEL_REQUEST ) {
            job_data = channel_conversation(segment, td->valid_task, td->log_ctx);
            if( job_data != NULL ) {
                pthread_mutex_lock(&job->mtx);
                job->list = g_slist_prepend(job->list, job_data);
                pthread_cond_signal(&job->wakeup);
                pthread_mutex_unlock(&job->mtx);
            }
        } else if( td->segment->state == CHANNEL_REQUEST_REMOVE ) {
            job_data = channel_dead_request_conversation(td->sawang_name, segment, td->log_ctx);
            if( job_data != NULL ) {
                pthread_mutex_lock(&job->mtx);
                job->list = g_slist_prepend(job->list, job_data);
                pthread_cond_signal(&job->wakeup);
                pthread_mutex_unlock(&job->mtx);
            }
        } else {
            pthread_mutex_unlock( &segment->mtx );
            usleep(1000); //1000us = 1ms
        }
    }

    pthread_exit(NULL);
}

void channel_thread_stop(ChannelThreadData *thread_data) {
    thread_data->stop = true;
    pthread_mutex_lock( &thread_data->segment->mtx );
    if(thread_data->segment->state == CHANNEL_IDLE)
        pthread_cond_signal(&thread_data->segment->cond_proxy_wakeup);
    pthread_mutex_unlock( &thread_data->segment->mtx );
}

static void channel_segment_init(ChannelSegmentData *segment) {
    segment->state = CHANNEL_IDLE;
    segment->rid[0] = 0;
    segment->task[0] = 0;
    segment->payload_buff_length = 0;
    return;
}

static char* channel_path(const char *sawang_name) {
    char *shm_path;

    shm_path = (char*)malloc(strlen(sawang_name) + strlen(CHANNEL_SUFFIX) + 2);
    sprintf(shm_path, "/%s%s", sawang_name, CHANNEL_SUFFIX);
    return shm_path;
}

static SubscribeJobData* channel_conversation(ChannelSegmentData *segment, bool (*valid_task)(const char *), const LogContext *log_ctx) {
    SubscribeJobData* job_data;
    struct timespec ts;
    bool success, kill;
    int ttl;

    success = false;
    job_data = NULL;
    kill = false;
    do {
        if( valid_task!=NULL && !valid_task(segment->task) ) {
            segment->state = CHANNEL_INVALID_TASK;
            pthread_cond_signal(&segment->cond_dispatcher_wakeup);
            break;
        }

        job_data = (SubscribeJobData*)malloc(sizeof(SubscribeJobData));
        job_data->rid = strdup(segment->rid);
        job_data->task = strdup(segment->task);
        job_data->payload_buff_length = segment->payload_buff_length;
        job_data->buff = channel_payload(segment->rid, segment->payload_buff_length, log_ctx);
        job_data->job_type = JOB_TASK;

        if(job_data->payload_buff_length>0 && job_data->buff==NULL) {
            segment->state = CHANNEL_INVALID_PAYLOAD;
            pthread_cond_signal(&segment->cond_dispatcher_wakeup);
            break;
        }

        segment->state = CHANNEL_ACKNOWLEDGED;
        pthread_cond_signal(&segment->cond_dispatcher_wakeup);
        pthread_mutex_unlock(&segment->mtx);
        usleep(1000);

        if( pthread_mutex_lock(&segment->mtx)==EOWNERDEAD ) {
            proxy_log(log_ctx, "ERROR", "dispatcher left channel mutex inconsistent");
            sawang_kill();
            kill = true;
            break;
        }

        ttl = 5;
        while(segment->state != CHANNEL_DONE && ttl-->0) {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 10; //wait 10 seconds from now
            if( pthread_cond_timedwait( &segment->cond_proxy_wakeup, &segment->mtx, &ts )==EOWNERDEAD ) {
                proxy_log(log_ctx, "ERROR", "dispatcher left channel mutex inconsistent");
                sawang_kill();
                kill = true;
                break;
            }
        }

        if(kill)
            break;

        channel_segment_init(segment);
        pthread_cond_signal(&segment->cond_idle);

        success = true;
    } while(false);

    if(!kill) {
        pthread_mutex_unlock(&segment->mtx);
        usleep(1000);
    }

    if(!success && job_data!=NULL) {
        job_data_destroy(job_data);
        job_data = NULL;
    }

    return job_data;
}

static void* channel_payload(const char *rid, unsigned int buff_length, const LogContext *log_ctx) {
    char *shm_path;
    int fd;
    void *map, *payload;
    char buff[PROXYLOGBUFLEN];

    if(buff_length<1)
        return NULL;

    shm_path = (char*)malloc( strlen(rid)+2 );
    sprintf(shm_path, "/%s", rid);
    payload = NULL;
    fd = -1;

    do {
        fd = shm_open(shm_path, O_RDWR, S_IRUSR | S_IWUSR);
        if(fd==-1) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(log_ctx, "ERROR", "open channel payload shared memory %s is failed, %s",
                shm_path, buff);
            break;
        }

        map = (void*)mmap(NULL, buff_length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if( map == MAP_FAILED ) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(log_ctx, "ERROR", "map channel payload shared memory %s is failed, %s",
                shm_path, buff);
            break;
        }

        payload = (char*)malloc(buff_length);
        memcpy(payload, map, buff_length);

        munmap(map, buff_length);
    } while(false);

    free(shm_path);
    if(fd>-1)
        close(fd);

    return payload;
}

static SubscribeJobData* channel_dead_request_conversation(const char *sawang_name, ChannelSegmentData *segment, const LogContext *log_ctx) {
    SubscribeJobData* job_data;
    struct timespec ts;
    bool success, kill;
    int ttl;

    success = false;
    job_data = NULL;
    kill = false;
    do {
        job_data = (SubscribeJobData*)malloc(sizeof(SubscribeJobData));
        job_data->rid = strdup(segment->rid);
        job_data->task = strdup(segment->task);
        job_data->payload_buff_length = segment->payload_buff_length;
        job_data->buff = channel_dead_request_payload(sawang_name, segment->payload_buff_length, log_ctx);
        job_data->job_type = JOB_DEAD_REQUEST;

        if(job_data->payload_buff_length>0 && job_data->buff==NULL) {
            segment->state = CHANNEL_INVALID_PAYLOAD;
            pthread_cond_signal(&segment->cond_dispatcher_wakeup);
            break;
        }

        segment->state = CHANNEL_ACKNOWLEDGED;
        pthread_cond_signal(&segment->cond_dispatcher_wakeup);
        pthread_mutex_unlock(&segment->mtx);
        usleep(1000);

        if( pthread_mutex_lock(&segment->mtx)==EOWNERDEAD ) {
            proxy_log(log_ctx, "ERROR", "dispatcher left channel mutex inconsistent");
            sawang_kill();
            kill = true;
            break;
        }

        ttl = 5;
        while(segment->state != CHANNEL_DONE && ttl-->0) {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 10; //wait 10 seconds from now
            if( pthread_cond_timedwait( &segment->cond_proxy_wakeup, &segment->mtx, &ts )==EOWNERDEAD ) {
                proxy_log(log_ctx, "ERROR", "dispatcher left channel mutex inconsistent");
                sawang_kill();
                kill = true;
                break;
            }
        }

        if(kill)
            break;

        channel_segment_init(segment);
        pthread_cond_signal(&segment->cond_idle);

        success = true;
    } while(false);

    if(!kill) {
        pthread_mutex_unlock(&segment->mtx);
        usleep(1000);
    }

    if(!success && job_data!=NULL) {
        job_data_destroy(job_data);
        job_data = NULL;
    }

    return job_data;
}

static void* channel_dead_request_payload(const char *sawang_name, unsigned int buff_length, const LogContext *log_ctx) {
    char *shm_path;
    int fd;
    void *map, *payload;
    char buff[PROXYLOGBUFLEN];

    if(buff_length<1)
        return NULL;

    shm_path = (char*)malloc(strlen(sawang_name) + strlen(REMOVE_REQUEST_SUFFIX) + 2/*backslash and zero terminator*/);
    sprintf(shm_path, "/%s%s", sawang_name, REMOVE_REQUEST_SUFFIX);
    payload = NULL;
    fd = -1;

    do {
        fd = shm_open(shm_path, O_RDWR, S_IRUSR | S_IWUSR);
        if(fd==-1) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(log_ctx, "ERROR", "open channel payload shared memory %s is failed, %s",
                shm_path, buff);
            break;
        }

        map = (void*)mmap(NULL, buff_length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if( map == MAP_FAILED ) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(log_ctx, "ERROR", "map channel payload shared memory %s is failed, %s",
                shm_path, buff);
            break;
        }

        payload = (char*)malloc(buff_length);
        memcpy(payload, map, buff_length);

        munmap(map, buff_length);
    } while(false);

    free(shm_path);
    if(fd>-1)
        close(fd);

    return payload;
}
