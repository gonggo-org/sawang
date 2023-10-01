#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include "heartbeat.h"
#include "exit.h"

#define HEARTBEAT_SUFFIX "_heartbeat"

static HeartbeatData* heartbeat_segment_create(const char *shm_path, const LogContext *log_ctx, float timeout, long heartbeat_wait);
static time_t heartbeat_overdue(float timeout, long heartbeat_wait);
static char* heartbeat_path(const char *sawang_name);

void heartbeat_thread_data_init(HeartbeatThreadData *data) {
    data->period = 0;
    data->timeout = 0;
    data->log_ctx = NULL;
    data->segment = NULL;
    data->stop = false;
    data->started = false;
}

bool heartbeat_thread_data_setup(HeartbeatThreadData *data,
    const LogContext *log_ctx, const char *sawang_name, long period, float timeout)
{
    data->segment = heartbeat_segment_create(sawang_name, log_ctx, period, timeout);
    if( data->segment==NULL )
        return false;
    data->period = period;
    data->timeout = timeout;
    data->log_ctx = log_ctx;
    data->stop = false;
    data->started = false;
    return true;
}

void heartbeat_thread_data_destroy(HeartbeatThreadData *data, const char *sawang_name) {
    char *sawang_path;

    if(data->segment!=NULL) {
        pthread_mutex_destroy( &data->segment->mtx );
        pthread_cond_destroy( &data->segment->wakeup );

        munmap(data->segment, sizeof(HeartbeatData));
        if(sawang_name!=NULL) {
            sawang_path = heartbeat_path(sawang_name);
            shm_unlink(sawang_path);
            free(sawang_path);
        }
    }
}

void* heartbeat_thread(void *arg) {
    HeartbeatThreadData *td = (HeartbeatThreadData*)arg;
    struct timespec ts;

    td->started = true;
    proxy_log(td->log_ctx, "INFO", "heartbeat thread starts");

    while( !td->stop ) {
        usleep(1000); //1000us = 1ms

        if( pthread_mutex_lock( &td->segment->mtx )==EOWNERDEAD ) {
            proxy_log(td->log_ctx, "ERROR", "dispatcher left heartbeat mutex inconsistent");
            sawang_kill();
            break;
        }

        td->segment->overdue = heartbeat_overdue(td->timeout, td->period);

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += td->period; //wait n seconds from now
        if( pthread_cond_timedwait( &td->segment->wakeup, &td->segment->mtx, &ts )==EOWNERDEAD ) {
            proxy_log(td->log_ctx, "ERROR", "dispatcher left heartbeat mutex inconsistent");
            sawang_kill();
            break;
        }

        pthread_mutex_unlock( &td->segment->mtx );
    }

    proxy_log(td->log_ctx, "INFO", "heartbeat thread exits");

    pthread_exit(NULL);
}

void heartbeat_thread_stop(HeartbeatThreadData* thread_data) {
    thread_data->stop = true;
    pthread_mutex_lock( &thread_data->segment->mtx );
    pthread_cond_signal( &thread_data->segment->wakeup );
    pthread_mutex_unlock( &thread_data->segment->mtx );
}

static HeartbeatData* heartbeat_segment_create(const char *sawang_name, const LogContext *log_ctx, float timeout, long heartbeat_wait) {
    char buff[PROXYLOGBUFLEN];
    int fd;
    HeartbeatData* segment;
    pthread_mutexattr_t mutexattr;
    pthread_condattr_t condattr;
    char *sawang_path;

    sawang_path = heartbeat_path(sawang_name);
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

        ftruncate(fd, sizeof(HeartbeatData));

        segment = (HeartbeatData*)mmap(NULL, sizeof(HeartbeatData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if( segment == MAP_FAILED ) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(log_ctx, "ERROR", "map shared memory %s failed, %s",
                sawang_path, buff);
            segment = NULL;
            break;
        }

        segment->overdue = heartbeat_overdue(timeout, heartbeat_wait);

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
        pthread_cond_init( &segment->wakeup, &condattr );
        pthread_condattr_destroy( &condattr );//condattr is no longer needed
    } while(false);

    free(sawang_path);
    if(fd>-1)
        close(fd);

    return segment;
}

static time_t heartbeat_overdue(float timeout, long heartbeat_wait) {
    time_t offset;

    offset = timeout * heartbeat_wait;
    return time(NULL) + offset;
}

static char* heartbeat_path(const char *sawang_name) {
    char *p;

    p = (char*)malloc(strlen(sawang_name) + strlen(HEARTBEAT_SUFFIX) + 2);
    sprintf(p, "/%s%s", sawang_name, HEARTBEAT_SUFFIX);
    return p;
}
