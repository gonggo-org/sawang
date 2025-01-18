#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <uuid/uuid.h>

#include "define.h"

static bool proxy_uuid_has_lock = false;
static pthread_mutex_t proxy_uuid_lock;

void proxy_uuid_init(void) {
    pthread_mutexattr_t mutexattr;

    if(!proxy_uuid_has_lock) {
        pthread_mutexattr_init(&mutexattr);
        pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_PRIVATE);
        pthread_mutexattr_setrobust(&mutexattr, PTHREAD_MUTEX_ROBUST);
        pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_NORMAL);
        pthread_mutex_init(&proxy_uuid_lock, &mutexattr);
        pthread_mutexattr_destroy(&mutexattr);//mutexattr is no longer needed
        proxy_uuid_has_lock = true;
    }
}

void proxy_uuid_destroy(void) {
    if(proxy_uuid_has_lock) {
        pthread_mutex_destroy(&proxy_uuid_lock);
        proxy_uuid_has_lock = false;
    }
}

void proxy_uuid_generate(char buff[UUIDBUFLEN]) {
    uuid_t binuuid;
 
    pthread_mutex_lock(&proxy_uuid_lock);
    uuid_generate(binuuid); //uuid_generate_random is not thread safe
    uuid_unparse_lower(binuuid, buff);
    pthread_mutex_unlock(&proxy_uuid_lock);
}