#ifndef _SAWANG_LOG_H_
#define _SAWANG_LOG_H_

#include <pthread.h>

#define PROXYLOGBUFLEN 500

typedef struct LogContext {
    pid_t pid;
    const char *proxy_name;
    const char *path;
    pthread_mutex_t *mtx;
} LogContext;

extern void proxy_log(const LogContext *ctx, const char *level, const char *fmt, ...);

#endif //_SAWANG_LOG_H_
