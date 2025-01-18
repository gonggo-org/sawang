#ifndef _SAWANG_LOG_H_
#define _SAWANG_LOG_H_

#include <pthread.h>

#include "define.h"

extern void proxy_log_context_init(pid_t pid, const char *path);
extern void proxy_log_context_destroy();
extern void proxy_log(const char *level, const char *fmt, ...);

#endif //_SAWANG_LOG_H_