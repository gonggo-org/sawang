#include <stdbool.h>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>

#include "dispatcher_heartbeat.h"
#include "define.h"
#include "exit.h"

#define HEARTBEAT_SUFFIX "_heartbeat"

static DispatcherHeartbeatData* dispatcher_heartbeat_segment_create(const char *gonggo_name, const LogContext *log_ctx);
static char* heartbeat_path(const char *gonggo_name);

void dispatcher_heartbeat_thread_data_init(DispatcherHeartbeatThreadData *data) {
    data->heartbeat_wait = 0;
    data->log_ctx = NULL;
    data->segment = NULL;
    data->lock = NULL;
    data->stop = false;
    data->started = false;
}

bool dispatcher_heartbeat_thread_data_setup(DispatcherHeartbeatThreadData *data,
    const LogContext *log_ctx, const char *gonggo_name,
    long heartbeat_wait, DispatcherHeartbeatLock *lock)
{
    data->segment = dispatcher_heartbeat_segment_create(gonggo_name, log_ctx);
    if( data->segment==NULL )
        return false;
    data->heartbeat_wait = heartbeat_wait;
    data->log_ctx = log_ctx;
    data->lock = lock;
    data->stop = false;
    data->started = false;
    return true;
}

void dispatcher_heartbeat_thread_data_destroy(DispatcherHeartbeatThreadData *data) {
    if( data->segment!=NULL )
        munmap(data->segment, sizeof(DispatcherHeartbeatData));
}

void* dispatcher_heartbeat_thread(void *arg) {
    DispatcherHeartbeatThreadData* thread_data;
    struct timespec ts;
    int status;

    thread_data = (DispatcherHeartbeatThreadData*)arg;
    thread_data->started = true;

///resumed by dispatcher_heartbeat_thread_resume(DispatcherHeartbeatThreadData *thread_data, long heartbeat_wait)
    pthread_mutex_lock( &thread_data->lock->mtx );
    if( thread_data->heartbeat_wait==-1 )
        pthread_cond_wait( &thread_data->lock->wakeup, &thread_data->lock->mtx );
    pthread_mutex_unlock( &thread_data->lock->mtx );

    proxy_log(thread_data->log_ctx, "INFO", "dispatcher heartbeat thread starts");

    while( !thread_data->stop ) {
        if( dispatcher_heartbeat_overdue(thread_data)) {
            proxy_log(thread_data->log_ctx, "INFO", "dispatcher heartbeat is timed out");
            dispatcher_deactivate_on_exit = false;
            sawang_exit = true;
            kill(getpid(), SIGTERM);
            break;
        }

        usleep(1000); //1000us = 1ms

        //can not determine whether dispatcher or other proxy died which put mutex in inconsistent state
        while( (status = pthread_mutex_lock( &thread_data->lock->mtx ))==EOWNERDEAD )
            pthread_mutex_consistent( &thread_data->lock->mtx );

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += thread_data->heartbeat_wait; //wait n seconds from now
        if( pthread_cond_timedwait( &thread_data->lock->wakeup, &thread_data->lock->mtx, &ts )==EOWNERDEAD ) {
            do {
                pthread_mutex_consistent( &thread_data->lock->mtx );
            } while(pthread_mutex_lock( &thread_data->lock->mtx )==EOWNERDEAD);
        }

        pthread_mutex_unlock( &thread_data->lock->mtx );
    }

    proxy_log(thread_data->log_ctx, "INFO", "dispatcher heartbeat thread exits");
    pthread_exit(NULL);
}

void dispatcher_heartbeat_thread_resume(DispatcherHeartbeatThreadData *thread_data, long heartbeat_wait) {
    pthread_mutex_lock( &thread_data->lock->mtx );
    thread_data->heartbeat_wait = heartbeat_wait;
    pthread_cond_signal( &thread_data->lock->wakeup );
    pthread_mutex_unlock( &thread_data->lock->mtx );
}

void dispatcher_heartbeat_thread_stop(DispatcherHeartbeatThreadData *thread_data) {
    thread_data->stop = true;
    pthread_mutex_lock( &thread_data->lock->mtx );
    pthread_cond_signal(&thread_data->lock->wakeup);
    pthread_mutex_unlock( &thread_data->lock->mtx );
}

bool dispatcher_heartbeat_overdue(DispatcherHeartbeatThreadData *thread_data) {
    time_t overdue;

    pthread_mutex_lock( &thread_data->segment->mtx );
    overdue = thread_data->segment->overdue;
    pthread_mutex_unlock( &thread_data->segment->mtx );
    return time(NULL) > overdue;
}

static DispatcherHeartbeatData* dispatcher_heartbeat_segment_create(const char *gonggo_name, const LogContext *log_ctx) {
    char buff[PROXYLOGBUFLEN];
    int fd;
    DispatcherHeartbeatData *segment;
    char *gonggo_path;

    gonggo_path = heartbeat_path(gonggo_name);
    segment = NULL;
    fd = -1;
    do {
        fd = shm_open(gonggo_path, O_RDWR, S_IRUSR | S_IWUSR);
        if(fd==-1) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(log_ctx, "ERROR", "open dispatcher shared memory %s failed",
                gonggo_path, buff);
            break;
        }

        segment = (DispatcherHeartbeatData*)mmap(NULL, sizeof(DispatcherHeartbeatData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if( segment == MAP_FAILED ) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(log_ctx, "ERROR", "map dispatcher shared memory %s failed",
                gonggo_path, buff);
            segment = NULL;
            break;
        }
    } while(false);

    free(gonggo_path);
    if(fd>-1)
        close(fd);

    return segment;
}

static char* heartbeat_path(const char *gonggo_name) {
    char *p;

    p = (char*)malloc(strlen(gonggo_name) + strlen(HEARTBEAT_SUFFIX) + 2);
    sprintf(p, "/%s%s", gonggo_name, HEARTBEAT_SUFFIX);
    return p;
}
