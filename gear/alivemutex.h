#ifndef _ALIVEMUTEX_H_
#define _ALIVEMUTEX_H_

#include <pthread.h>
#include <stdbool.h>

typedef struct AliveMutexShm {
    pthread_mutex_t lock;
    bool alive;    
} AliveMutexShm;

extern bool alive_mutex_create(bool alive);
extern void alive_mutex_unlink_enable(void);
extern void alive_mutex_lock(void);
extern void alive_mutex_die(void);
extern void alive_mutex_destroy(void);

#endif //_ALIVEMUTEX_H_