#define _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "error.h"
#include "channel.h"
#include "dispatcher.h"
#include "dispatcher_heartbeat.h"
#include "heartbeat.h"
#include "work.h"

volatile bool sawang_exit = false;
volatile bool dispatcher_deactivate_on_exit = true;

static void handler(int signal, siginfo_t *info, void *context);
static void cleanup(LogContext *log_ctx, const char *sawang_name,
    SubscribeThreadData *subscribe_thread_data, ChannelThreadData *channel_thread_data,
    DispatcherHeartbeatThreadData *dispatcher_heartbeat_thread_data,
    HeartbeatThreadData *heartbeat_thread_data,
    DispatcherHeartbeatLock* dispatcher_heartbeat_lock, SubscribeJob *subscribe_job,
    pthread_mutex_t* mtx_subscribe_reply);

int work(pid_t pid, const ConfVar *cv_head,
    bool (*init_task)(const LogContext*, void*),
    void (*stop_task)(const LogContext*, void*),
    bool (*valid_task)(const char *),
    void (*task)(const SubscribeJobData*, void*),
    void (*dead_request_handler)(const char*, const LogContext*, void*),
    void *user_data,
    SubscribeReply *subscribe_reply)
{
    struct sigaction action;
    const char *sawang_name, *gonggo_name;
    char buff[PROXYLOGBUFLEN];
    pthread_mutexattr_t mtx_attr;
    pthread_mutex_t mtx_log, mtx_subscribe_reply;
    pthread_condattr_t cond_attr;
    pthread_attr_t attr;
    pthread_t t_channel_thread, t_subscribe_thread, t_dispatcher_heartbeat_thread, t_heartbeat_thread;
    SubscribeThreadData subscribe_thread_data;
    ChannelThreadData channel_thread_data;
    DispatcherHeartbeatThreadData dispatcher_heartbeat_thread_data;
    HeartbeatThreadData heartbeat_thread_data;
    SubscribeJob subscribe_job;
    long heartbeat_period;
    float heartbeat_timeout;
    long dispatcher_heartbeat;
    DispatcherHeartbeatLock dispatcher_heartbeat_lock;

    sawang_name = confvar_value(cv_head, CONF_SAWANG);
    LogContext log_context = {
        .pid = pid,
        .proxy_name = sawang_name,
        .path = confvar_value(cv_head, CONF_LOGPATH),
        .mtx = NULL
    };

    confvar_long(cv_head, CONF_HEARTBEATPERIOD, &heartbeat_period);
    confvar_float(cv_head, CONF_HEARTBEATTIMEOUT, &heartbeat_timeout);

    sawang_exit = false;
    dispatcher_deactivate_on_exit = true;

    memset(&action, 0, sizeof(action));
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = handler;
    if(sigaction(SIGTERM, &action, NULL)==-1) {
        strerror_r(errno, buff, PROXYLOGBUFLEN);
        proxy_log(&log_context, "ERROR", "sigaction failed %s", buff);
        return ERROR_TERMHANDLER;
    }

    pthread_mutexattr_init(&mtx_attr);
    pthread_mutexattr_setpshared(&mtx_attr, PTHREAD_PROCESS_PRIVATE);
    pthread_mutex_init(&mtx_log, &mtx_attr);
    pthread_mutex_init(&mtx_subscribe_reply, &mtx_attr);
    pthread_mutex_init(&subscribe_job.mtx, &mtx_attr);
    pthread_mutex_init(&dispatcher_heartbeat_lock.mtx, &mtx_attr);
    pthread_mutexattr_destroy(&mtx_attr);

    pthread_condattr_init ( &cond_attr );
    pthread_condattr_setpshared( &cond_attr, PTHREAD_PROCESS_PRIVATE);
    pthread_cond_init(&subscribe_job.wakeup, &cond_attr);
    pthread_cond_init(&dispatcher_heartbeat_lock.wakeup, &cond_attr);
    pthread_condattr_destroy( &cond_attr );

    log_context.mtx = &mtx_log;

    if(init_task!=NULL && !init_task(&log_context, user_data)) {
        proxy_log(&log_context, "ERROR", "init_task is failed");
        pthread_mutex_destroy(log_context.mtx);
        return ERROR_START;
    }

    subscribe_job.list = NULL;

    subscribe_thread_data_init( &subscribe_thread_data );
    channel_thread_data_init( &channel_thread_data );
    dispatcher_heartbeat_thread_data_init( &dispatcher_heartbeat_thread_data );
    heartbeat_thread_data_init( &heartbeat_thread_data );

    pthread_attr_init(&attr);//inisialisasi attribut thread

    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    if( !subscribe_thread_data_setup( &subscribe_thread_data,
        &log_context, sawang_name, &subscribe_job, &mtx_subscribe_reply,
        task, dead_request_handler, user_data )
    ){
        pthread_attr_destroy(&attr);//destroy thread-attribute
        proxy_log(&log_context, "ERROR", "subscribe thread data setup is failed");
        cleanup(&log_context, sawang_name,
            &subscribe_thread_data, &channel_thread_data,
            &dispatcher_heartbeat_thread_data,
            &heartbeat_thread_data,
            &dispatcher_heartbeat_lock, &subscribe_job,
            &mtx_subscribe_reply);
        return ERROR_START;
    }
    if( pthread_create( &t_subscribe_thread, &attr, subscribe_thread, &subscribe_thread_data ) != 0 ) {
        pthread_attr_destroy(&attr);//destroy thread-attribute
        proxy_log(&log_context, "ERROR", "cannot start subscribe thread");
        cleanup(&log_context, sawang_name,
            &subscribe_thread_data, &channel_thread_data,
            &dispatcher_heartbeat_thread_data,
            &heartbeat_thread_data,
            &dispatcher_heartbeat_lock, &subscribe_job,
            &mtx_subscribe_reply);
        return ERROR_START;
    }
    while( !subscribe_thread_data.started )
        usleep(1000);

    if( !channel_thread_data_setup(&channel_thread_data,
        &log_context, sawang_name, &subscribe_job, valid_task)
    ) {
        subscribe_thread_stop(&subscribe_thread_data);
        pthread_join(t_subscribe_thread, NULL);

        pthread_attr_destroy(&attr);//destroy thread-attribute
        proxy_log(&log_context, "ERROR", "channel thread data setup is failed");
        cleanup(&log_context, sawang_name,
            &subscribe_thread_data, &channel_thread_data,
            &dispatcher_heartbeat_thread_data,
            &heartbeat_thread_data,
            &dispatcher_heartbeat_lock, &subscribe_job,
            &mtx_subscribe_reply);
        return ERROR_START;
    }
    if( pthread_create( &t_channel_thread, &attr, channel_thread, &channel_thread_data ) != 0 ) {
        subscribe_thread_stop(&subscribe_thread_data);
        pthread_join(t_subscribe_thread, NULL);

        pthread_attr_destroy(&attr);//destroy thread-attribute
        proxy_log(&log_context, "ERROR", "cannot start channel thread");
        cleanup(&log_context, sawang_name,
            &subscribe_thread_data, &channel_thread_data,
            &dispatcher_heartbeat_thread_data,
            &heartbeat_thread_data,
            &dispatcher_heartbeat_lock, &subscribe_job,
            &mtx_subscribe_reply);
        return ERROR_START;
    }
    while( !channel_thread_data.started )
        usleep(1000);

    gonggo_name  = confvar_value(cv_head, CONF_GONGGO);
    if( !dispatcher_heartbeat_thread_data_setup( &dispatcher_heartbeat_thread_data,
        &log_context, gonggo_name,
        -1,//wait for dispatcher_activate will fill the value
        &dispatcher_heartbeat_lock)
    ) {
        subscribe_thread_stop(&subscribe_thread_data);
        channel_thread_stop(&channel_thread_data);
        pthread_join(t_subscribe_thread, NULL);
        pthread_join(t_channel_thread, NULL);

        pthread_attr_destroy(&attr);//destroy thread-attribute
        proxy_log(&log_context, "ERROR", "dispatcher heartbeat thread data setup is failed");
        cleanup(&log_context, sawang_name,
            &subscribe_thread_data, &channel_thread_data,
            &dispatcher_heartbeat_thread_data,
            &heartbeat_thread_data,
            &dispatcher_heartbeat_lock, &subscribe_job,
            &mtx_subscribe_reply);
        return ERROR_START;
    }

    if( dispatcher_heartbeat_overdue(&dispatcher_heartbeat_thread_data) ) {
        subscribe_thread_stop(&subscribe_thread_data);
        channel_thread_stop(&channel_thread_data);
        pthread_join(t_subscribe_thread, NULL);
        pthread_join(t_channel_thread, NULL);

        pthread_attr_destroy(&attr);//destroy thread-attribute
        proxy_log(&log_context, "ERROR", "dispatcher heartbeat is overdue");
        cleanup(&log_context, sawang_name,
            &subscribe_thread_data, &channel_thread_data,
            &dispatcher_heartbeat_thread_data,
            &heartbeat_thread_data,
            &dispatcher_heartbeat_lock, &subscribe_job,
            &mtx_subscribe_reply);
        return ERROR_START;
    }

    if( pthread_create( &t_dispatcher_heartbeat_thread, &attr, dispatcher_heartbeat_thread, &dispatcher_heartbeat_thread_data ) != 0 ) {
        subscribe_thread_stop(&subscribe_thread_data);
        channel_thread_stop(&channel_thread_data);
        pthread_join(t_subscribe_thread, NULL);
        pthread_join(t_channel_thread, NULL);

        pthread_attr_destroy(&attr);//destroy thread-attribute
        proxy_log(&log_context, "ERROR", "cannot start dispatcher heartbeat thread");
        cleanup(&log_context, sawang_name,
            &subscribe_thread_data, &channel_thread_data,
            &dispatcher_heartbeat_thread_data,
            &heartbeat_thread_data,
            &dispatcher_heartbeat_lock, &subscribe_job,
            &mtx_subscribe_reply);
        return ERROR_START;
    }
    while( !dispatcher_heartbeat_thread_data.started )
        usleep(1000);

    if( !heartbeat_thread_data_setup(&heartbeat_thread_data,
        &log_context, sawang_name, heartbeat_period, heartbeat_timeout)
    ) {
        subscribe_thread_stop(&subscribe_thread_data);
        channel_thread_stop(&channel_thread_data);
        dispatcher_heartbeat_thread_stop(&dispatcher_heartbeat_thread_data);
        pthread_join(t_subscribe_thread, NULL);
        pthread_join(t_channel_thread, NULL);
        pthread_join(t_dispatcher_heartbeat_thread, NULL);

        pthread_attr_destroy(&attr);//destroy thread-attribute
        proxy_log(&log_context, "ERROR", "cannot start heartbeat thread");
        cleanup(&log_context, sawang_name,
            &subscribe_thread_data, &channel_thread_data,
            &dispatcher_heartbeat_thread_data,
            &heartbeat_thread_data,
            &dispatcher_heartbeat_lock, &subscribe_job,
            &mtx_subscribe_reply);
        return ERROR_START;
    }
    if( pthread_create( &t_heartbeat_thread, &attr, heartbeat_thread, &heartbeat_thread_data ) != 0 ) {
        subscribe_thread_stop(&subscribe_thread_data);
        channel_thread_stop(&channel_thread_data);
        dispatcher_heartbeat_thread_stop(&dispatcher_heartbeat_thread_data);
        pthread_join(t_subscribe_thread, NULL);
        pthread_join(t_channel_thread, NULL);
        pthread_join(t_dispatcher_heartbeat_thread, NULL);

        pthread_attr_destroy(&attr);//destroy thread-attribute
        proxy_log(&log_context, "ERROR", "cannot start heartbeat thread");
        cleanup(&log_context, sawang_name,
            &subscribe_thread_data, &channel_thread_data,
            &dispatcher_heartbeat_thread_data,
            &heartbeat_thread_data,
            &dispatcher_heartbeat_lock, &subscribe_job,
            &mtx_subscribe_reply);
        return ERROR_START;
    }
    while( !heartbeat_thread_data.started )
        usleep(1000);

    subscribe_reply_setup(subscribe_reply, &subscribe_thread_data, &dispatcher_heartbeat_thread_data);

    if( !dispatcher_activate(gonggo_name, sawang_name,
        heartbeat_thread_data.period, &dispatcher_heartbeat, &log_context)
    ) {
        subscribe_thread_stop(&subscribe_thread_data);
        channel_thread_stop(&channel_thread_data);
        dispatcher_heartbeat_thread_stop(&dispatcher_heartbeat_thread_data);
        heartbeat_thread_stop(&heartbeat_thread_data);

        pthread_join(t_subscribe_thread, NULL);
        pthread_join(t_channel_thread, NULL);
        pthread_join(t_dispatcher_heartbeat_thread, NULL);
        pthread_join(t_heartbeat_thread, NULL);

        pthread_attr_destroy(&attr);//destroy thread-attribute
        cleanup(&log_context, sawang_name,
            &subscribe_thread_data, &channel_thread_data,
            &dispatcher_heartbeat_thread_data,
            &heartbeat_thread_data,
            &dispatcher_heartbeat_lock, &subscribe_job,
            &mtx_subscribe_reply);
        return ERROR_START;
    }

    dispatcher_heartbeat_thread_resume(&dispatcher_heartbeat_thread_data, dispatcher_heartbeat);

    pthread_attr_destroy(&attr);//destroy thread-attribute

    proxy_log(&log_context, "INFO", "%s server started", sawang_name);

    while( !sawang_exit )
        pause();

    proxy_log(&log_context, "INFO", "%s server is about to stop", sawang_name);

    if( stop_task!=NULL )
        stop_task(&log_context, user_data);

    if( dispatcher_deactivate_on_exit ) {
        proxy_log(&log_context, "INFO", "dispatcher deactivation started");
        dispatcher_deactivate(gonggo_name, sawang_name, &log_context);
    }

    proxy_log(&log_context, "INFO", "stopping subscribe and channel threads");

    subscribe_thread_stop(&subscribe_thread_data);
    channel_thread_stop(&channel_thread_data);
    dispatcher_heartbeat_thread_stop(&dispatcher_heartbeat_thread_data);
    heartbeat_thread_stop(&heartbeat_thread_data);

    pthread_join(t_subscribe_thread, NULL);
    proxy_log(&log_context, "INFO", "subscribe thread stoppped");

    pthread_join(t_channel_thread, NULL);
    proxy_log(&log_context, "INFO", "channel thread stoppped");

    pthread_join(t_dispatcher_heartbeat_thread, NULL);
    proxy_log(&log_context, "INFO", "dispatcher heartbeat thread stoppped");

    pthread_join(t_heartbeat_thread, NULL);
    proxy_log(&log_context, "INFO", "heartbeat thread stoppped");

    cleanup(&log_context, sawang_name,
        &subscribe_thread_data, &channel_thread_data,
        &dispatcher_heartbeat_thread_data,
        &heartbeat_thread_data,
        &dispatcher_heartbeat_lock, &subscribe_job, &mtx_subscribe_reply);
    proxy_log(&log_context, "INFO", "%s server stopped", sawang_name);

    return 0;
}

static void handler(int signal, siginfo_t *info, void *context) {
    if(signal==SIGTERM)
        sawang_exit = true;
}

static void cleanup(LogContext *log_ctx, const char *sawang_name,
    SubscribeThreadData *subscribe_thread_data, ChannelThreadData *channel_thread_data,
    DispatcherHeartbeatThreadData *dispatcher_heartbeat_thread_data,
    HeartbeatThreadData *heartbeat_thread_data,
    DispatcherHeartbeatLock* dispatcher_heartbeat_lock, SubscribeJob *subscribe_job,
    pthread_mutex_t* mtx_subscribe_reply)
{
    proxy_log(log_ctx, "INFO", "stop server");

    pthread_mutex_destroy(log_ctx->mtx);
    pthread_mutex_destroy(mtx_subscribe_reply);

    pthread_mutex_destroy(&dispatcher_heartbeat_lock->mtx);
    pthread_cond_destroy(&dispatcher_heartbeat_lock->wakeup);

    pthread_mutex_destroy(&subscribe_job->mtx);
    pthread_cond_destroy(&subscribe_job->wakeup);
    if( subscribe_job->list != NULL ) {
        g_slist_foreach(subscribe_job->list, job_list_item_destroy, NULL);
        g_slist_free(subscribe_job->list);
    }

    subscribe_thread_data_destroy(subscribe_thread_data, sawang_name);
    channel_thread_data_destroy(channel_thread_data);
    dispatcher_heartbeat_thread_data_destroy(dispatcher_heartbeat_thread_data);
    heartbeat_thread_data_destroy(heartbeat_thread_data, sawang_name);

    return;
}
