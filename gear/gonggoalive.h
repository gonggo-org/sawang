#ifndef _GONGGOALIVE_H_
#define _GONGGOALIVE_H_

#include <pthread.h>
#include <stdbool.h>

#include "proxy.h"

/*GonggoAliveMutexShm must match gonggo AliveMutexShm*/
typedef struct GonggoAliveMutexShm {
    pthread_mutex_t lock;
    bool alive;    
} GonggoAliveMutexShm;

extern bool gonggo_alive_context_init(void);
extern void gonggo_alive_context_destroy(void);
extern void* gonggo_alive(void *arg);
extern void gonggo_alive_waitfor_started(void);
extern bool gonggo_alive_isstarted(void);
extern void gonggo_alive_stop(pthread_t t);

#endif //_GONGGOALIVE_H_