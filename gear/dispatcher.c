#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */

#include "dispatcher.h"

static char* gonggo_path_create(const char *gonggo_name);
static bool activation_conversation(ActivationData *pshm, const LogContext * log_ctx, const char *sawang_name, long heartbeat, long *dispatcher_heartbeat);
static void deactivation_conversation(ActivationData *pshm, const LogContext * log_ctx, const char *sawang_name);

bool dispatcher_activate(const char *gonggo_name, const char *sawang_name,
    long heartbeat, long *dispatcher_heartbeat, const LogContext * log_ctx)
{
    char *gonggo_path;
    int fd, ttl;
    char buff[PROXYLOGBUFLEN];
    bool done, success, inconsistent;
    ActivationData *pshm;
    struct timespec ts;

    if( strlen(sawang_name) > (SHMPATHBUFLEN-1) ) {
        proxy_log(log_ctx, "ERROR", "proxy name %s is too long", sawang_name);
        return false;
    }

    gonggo_path = gonggo_path_create(gonggo_name);
    success = false;
    fd = -1;
    do {
        fd = shm_open(gonggo_path, O_RDWR, S_IRUSR | S_IWUSR);
        if(fd==-1) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(log_ctx, "ERROR", "open dispatcher shared memory %s failed",
                gonggo_path, buff);
            break;
        }

        pshm = (ActivationData*)mmap(NULL, sizeof(ActivationData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if( pshm == MAP_FAILED ) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(log_ctx, "ERROR", "map dispatcher shared memory %s failed",
                gonggo_path, buff);
            break;
        }

        ttl = 5;
        done = false;
        while( !done && ttl-- > 0 ) {
            //can not determine whether dispatcher or other proxy died which put mutex in inconsistent state
            while( pthread_mutex_lock( &pshm->mtx )==EOWNERDEAD )
                pthread_mutex_consistent( &pshm->mtx );

            inconsistent = false;
            if( pshm->state != ACTIVATION_IDLE ) {
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 5; //wait 5 seconds from now
                if( pthread_cond_timedwait( &pshm->cond_idle, &pshm->mtx, &ts )==EOWNERDEAD ) {
                    //can not determine whether dispatcher or other proxy died which put mutex in inconsistent state
                    inconsistent = true;
                    do {
                        pthread_mutex_consistent( &pshm->mtx );
                    } while(pthread_mutex_lock( &pshm->mtx )==EOWNERDEAD);
                }
            }

            if( !inconsistent && pshm->state == ACTIVATION_IDLE ) {
                success = activation_conversation(pshm, log_ctx, sawang_name, heartbeat, dispatcher_heartbeat);
                done = true;
            } else
                pthread_mutex_unlock( &pshm->mtx );

            if( !done ) {
                if( ttl < 1 ) {
                    proxy_log(log_ctx, "ERROR", "proxy activation is failed, dispatcher is busy");
                    break;
                }
                usleep(1000); //1000us = 1ms
            }
        }//while( !done && ttl-- > 0 ) {

        munmap(pshm, sizeof(ActivationData));
    } while(false);

    free(gonggo_path);
    if(fd>-1)
        close(fd);

    return success;
}

void dispatcher_deactivate(const char *gonggo_name, const char *sawang_name, const LogContext * log_ctx) {
    char *gonggo_path;
    int fd, ttl;
    char buff[PROXYLOGBUFLEN];
    bool done, inconsistent;
    ActivationData *pshm;
    struct timespec ts;

    if( strlen(sawang_name) > (SHMPATHBUFLEN-1) ) {
        proxy_log(log_ctx, "ERROR", "proxy name %s is too long", sawang_name);
        return;
    }

    gonggo_path = gonggo_path_create(gonggo_name);
    fd = -1;
    do {
        fd = shm_open(gonggo_path, O_RDWR, S_IRUSR | S_IWUSR);
        if(fd==-1) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(log_ctx, "ERROR", "open dispatcher shared memory %s failed",
                gonggo_path, buff);
            break;
        }

        pshm = (ActivationData*)mmap(NULL, sizeof(ActivationData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if( pshm == MAP_FAILED ) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log(log_ctx, "ERROR", "map dispatcher shared memory %s failed",
                gonggo_path, buff);
            break;
        }

        ttl = 5;
        done = false;
        while( !done && ttl-- > 0 ) {
            //can not determine whether dispatcher or other proxy died which put mutex in inconsistent state
            while( pthread_mutex_lock( &pshm->mtx )==EOWNERDEAD )
                pthread_mutex_consistent( &pshm->mtx );

            inconsistent = false;
            if( pshm->state != ACTIVATION_IDLE ) {
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 5; //wait 5 seconds from now
                if( pthread_cond_timedwait( &pshm->cond_idle, &pshm->mtx, &ts )==EOWNERDEAD ) {
                //can not determine whether dispatcher or other proxy died which put mutex in inconsistent state
                    inconsistent = true;
                    do {
                        pthread_mutex_consistent( &pshm->mtx );
                    } while(pthread_mutex_lock( &pshm->mtx )==EOWNERDEAD);
                }
            }

            if( !inconsistent && pshm->state == ACTIVATION_IDLE ) {
                deactivation_conversation(pshm, log_ctx, sawang_name);
                done = true;
            } else
                pthread_mutex_unlock( &pshm->mtx );

            if( !done ) {
                if( ttl < 1 ) {
                    proxy_log(log_ctx, "ERROR", "proxy deactivation is failed, dispatcher is busy");
                    break;
                }
                usleep(1000); //1000us = 1ms
            }
        }

        munmap(pshm, sizeof(ActivationData));
    } while(false);

    free(gonggo_path);
    if(fd>-1)
        close(fd);

    proxy_log(log_ctx, "INFO", "dispatcher_deactivate exits");
    return;
}

static char* gonggo_path_create(const char *gonggo_name) {
    char *shm_path;

    shm_path = (char*)malloc(strlen(gonggo_name) + 2);
    sprintf(shm_path, "/%s", gonggo_name);
    return shm_path;
}

static bool activation_conversation(ActivationData *pshm, const LogContext * log_ctx, const char *sawang_name, long heartbeat, long *dispatcher_heartbeat) {
    bool success, answered;
    struct timespec ts;
    int ttl;

    proxy_log(log_ctx, "INFO", "proxy %s activation in dispatcher is started", sawang_name);

    success = false;
    do {
        pshm->state = ACTIVATION_REQUEST;
        strcpy(pshm->sawang_name, sawang_name);
        pshm->proxy_heartbeat = heartbeat;
        pthread_cond_signal(&pshm->cond_dispatcher_wakeup);
        pthread_mutex_unlock(&pshm->mtx);
        usleep(1000);

        answered = false;
        ttl = 5;
        while( !answered && ttl-- > 0 ) {
            if( pthread_mutex_lock(&pshm->mtx)==EOWNERDEAD ) {
                proxy_log(log_ctx, "ERROR", "dispatcher left activation mutex inconsistent");
                break;
            }

            if( pshm->state == ACTIVATION_REQUEST ) {
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 10; //wait 10 seconds from now
                if( pthread_cond_timedwait( &pshm->cond_proxy_wakeup, &pshm->mtx, &ts )==EOWNERDEAD ) {
                    pthread_mutex_consistent( &pshm->mtx );
                    proxy_log(log_ctx, "ERROR", "dispatcher left activation mutex inconsistent");
                    break;
                }
            }

            if( pshm->state != ACTIVATION_REQUEST ) {
                if(pshm->state == ACTIVATION_SUCCESS) {
                    success = true;
                    *dispatcher_heartbeat = pshm->dispatcher_heartbeat;
                    proxy_log(log_ctx, "INFO", "proxy %s is successfully activated in dispatcher", sawang_name);
                } else if(pshm->state == ACTIVATION_FAILED)
                    proxy_log(log_ctx, "ERROR", "dispatcer failed to activate proxy %s", sawang_name);
                else
                    proxy_log(log_ctx, "ERROR", "dispatcer failed to activate proxy %s with unexpected state %d", sawang_name, pshm->state);

                pshm->state = ACTIVATION_DONE;
                pthread_cond_signal(&pshm->cond_dispatcher_wakeup);
                answered = true;
            }

            pthread_mutex_unlock(&pshm->mtx);
            usleep(1000);
        }//while( !answered && ttl-- > 0 ) {

        if( !answered )
            proxy_log(log_ctx, "ERROR", "proxy %s activation is not answered by dispatcher", sawang_name);
    } while(false);

    return success;
}

static void deactivation_conversation(ActivationData *pshm, const LogContext * log_ctx, const char *sawang_name) {
    bool answered;
    struct timespec ts;
    int ttl;

    proxy_log(log_ctx, "INFO", "proxy %s deactivation in dispatcher is started", sawang_name);

    do {
        pshm->state = DEACTIVATION_REQUEST;
        strcpy(pshm->sawang_name, sawang_name);
        pthread_cond_signal(&pshm->cond_dispatcher_wakeup);
        pthread_mutex_unlock(&pshm->mtx);
        usleep(1000);

        answered = false;
        ttl = 5;
        while( !answered && ttl-- > 0 ) {
            if( pthread_mutex_lock(&pshm->mtx)==EOWNERDEAD ) {
                proxy_log(log_ctx, "ERROR", "dispatcher left activation mutex inconsistent");
                break;
            }

            if( pshm->state == DEACTIVATION_REQUEST ) {
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 10; //wait 10 seconds from now
                if( pthread_cond_timedwait( &pshm->cond_proxy_wakeup, &pshm->mtx, &ts )==EOWNERDEAD ) {
                    pthread_mutex_consistent( &pshm->mtx );
                    proxy_log(log_ctx, "ERROR", "dispatcher left activation mutex inconsistent");
                    break;
                }
            }

            if( pshm->state != DEACTIVATION_REQUEST ) {
                if(pshm->state == DEACTIVATION_NOTEXISTS)
                    proxy_log(log_ctx, "INFO", "proxy %s was already deactivated in dispatcher before", sawang_name);
                else if(pshm->state == DEACTIVATION_SUCCESS)
                    proxy_log(log_ctx, "INFO", "proxy %s is successfully deactivated in dispatcher", sawang_name);
                else
                    proxy_log(log_ctx, "ERROR", "proxy %s deactivation in dispatcher is failed with unexpected state %d", sawang_name, pshm->state);

                pshm->state = DEACTIVATION_DONE;
                pthread_cond_signal(&pshm->cond_dispatcher_wakeup);
                answered = true;
            }

            pthread_mutex_unlock(&pshm->mtx);
            usleep(1000);
        }//while( !answered && ttl-- > 0 )

        if( !answered )
            proxy_log(log_ctx, "ERROR", "proxy %s deactivation is not answered by dispatcher", sawang_name);
    } while(false);

    proxy_log(log_ctx, "INFO", "proxy %s deactivation in dispatcher is stopped", sawang_name);
    return;
}
