#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "log.h"
#include "proxy.h"
#include "util.h"
#include "replyqueue.h"
#include "proxyuuid.h"
#include "globaldata.h"

#define SUBSCRIBE_SUFFIX "_subscribe"

//property
static char *proxy_subscribe_path = NULL;
static volatile bool proxy_subscribe_started = false;
static bool proxy_subscribe_end = false;
static bool proxy_subscribe_shm_unlink = false;
static ProxySubscribeShm *proxy_subscribe_shm = NULL;    
static pthread_mutex_t proxy_subscribe_lock;
static pthread_cond_t proxy_subscribe_wakeup;

//function
static char* proxy_subscribe_path_create(void);
static bool proxy_subscribe_shm_create(const char *proxy_path);
static void proxy_subscribe_shm_idle(void);
static enum ProxySubscribeState proxy_subscribe_exchange(const ReplyQueueTask *task);
static size_t proxy_subscribe_create_answer(const char *path, const char *task);

bool proxy_subscribe_context_init(void) 
{
    pthread_mutexattr_t mutexattr;
    pthread_condattr_t condattr;

    proxy_subscribe_path = proxy_subscribe_path_create();
    if(!proxy_subscribe_shm_create(proxy_subscribe_path)) {
        free(proxy_subscribe_path);
        proxy_subscribe_path = NULL;
        return false;
    }

    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_PRIVATE);
    pthread_mutexattr_setrobust(&mutexattr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_NORMAL);
    pthread_mutex_init(&proxy_subscribe_lock, &mutexattr);
    pthread_mutexattr_destroy(&mutexattr);//mutexattr is no longer needed

    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_PRIVATE);
    pthread_cond_init(&proxy_subscribe_wakeup, &condattr);
    pthread_condattr_destroy(&condattr);//condattr is no longer neededs

    return true;
}

void proxy_subscribe_shm_unlink_enable(void) {
    proxy_subscribe_shm_unlink = true;
}

void proxy_subscribe_context_destroy(void) {
    if(proxy_subscribe_shm!=NULL) {
        if(proxy_subscribe_shm_unlink) {
            pthread_mutex_destroy(&proxy_subscribe_shm->lock);

            proxy_cond_reset(&proxy_subscribe_shm->dispatcher_wakeup);
            pthread_cond_destroy(&proxy_subscribe_shm->dispatcher_wakeup);

            proxy_cond_reset(&proxy_subscribe_shm->proxy_wakeup);
            pthread_cond_destroy(&proxy_subscribe_shm->proxy_wakeup);
        }
        munmap(proxy_subscribe_shm, sizeof(ProxySubscribeShm));        
        proxy_subscribe_shm = NULL;           
    }
    if(proxy_subscribe_path!=NULL) {
        if(proxy_subscribe_shm_unlink) {
            shm_unlink(proxy_subscribe_path);
        }
        free(proxy_subscribe_path);
        proxy_subscribe_path = NULL;
    }
    pthread_mutex_destroy(&proxy_subscribe_lock);
    pthread_cond_destroy(&proxy_subscribe_wakeup);
}

void* proxy_subscribe(void *arg) {
    enum ProxySubscribeState state;
    GQueue *failed_task;
    ReplyQueueTask *task;
    bool alive = true;
 
    proxy_log("INFO", "proxy %s subscribe thread is started", proxy_name);

    proxy_subscribe_started = true;

    failed_task = g_queue_new();
    pthread_mutex_lock(&proxy_subscribe_lock);
    while(alive && !proxy_subscribe_end && proxy_subscribe_shm->state!=SUBSCRIBE_TERMINATION) {
        proxy_subscribe_shm_idle();//set state to subscribe_IDLE
        while( (task = reply_queue_pop_head())!=NULL ) {
            if(pthread_mutex_lock(&proxy_subscribe_shm->lock) == EOWNERDEAD) {
                pthread_mutex_consistent(&proxy_subscribe_shm->lock);//resurrection
                alive = false;
            } else {
                state = proxy_subscribe_exchange(task);
                alive = state!= SUBSCRIBE_TERMINATION;
            }
            pthread_mutex_unlock(&proxy_subscribe_shm->lock);
            
            if(!alive) {
                reply_queue_task_destroy(task);                
                break;
            }

            if(state!=SUBSCRIBE_DONE) {
                g_queue_push_tail(failed_task, task);
            } else {
                reply_queue_task_destroy(task);
            }
            proxy_subscribe_shm_idle();//set state to subscribe_IDLE
        }
        if(!g_queue_is_empty(failed_task)) {
            reply_queue_push_head(failed_task);
        }
        if(alive) {
            pthread_cond_wait(&proxy_subscribe_wakeup, &proxy_subscribe_lock);
        }
    }
    pthread_mutex_unlock(&proxy_subscribe_lock);
    g_queue_free_full(failed_task, (GDestroyNotify)reply_queue_task_destroy);

    proxy_log("INFO", "proxy %s subscribe thread is stopped", proxy_name);
    pthread_exit(NULL);
}

void proxy_subscribe_waitfor_started(void) {
	while(!proxy_subscribe_started) {
		usleep(1000);
	}
}

bool proxy_subscribe_isstarted(void) {
    return proxy_subscribe_started;
}

void proxy_subscribe_awake(void) {
    pthread_mutex_lock(&proxy_subscribe_lock);
    pthread_cond_signal(&proxy_subscribe_wakeup);
    pthread_mutex_unlock(&proxy_subscribe_lock);
}

void proxy_subscribe_stop(void) {
    if(pthread_mutex_lock(&proxy_subscribe_shm->lock)==EOWNERDEAD) {
        pthread_mutex_consistent(&proxy_subscribe_shm->lock);        
    }
    proxy_subscribe_shm->state = SUBSCRIBE_TERMINATION;
    pthread_cond_signal(&proxy_subscribe_shm->proxy_wakeup);
    pthread_mutex_unlock(&proxy_subscribe_shm->lock);

    pthread_mutex_lock(&proxy_subscribe_lock);
    proxy_subscribe_end = true;
    pthread_cond_signal(&proxy_subscribe_wakeup);
    pthread_mutex_unlock(&proxy_subscribe_lock);
}

static char* proxy_subscribe_path_create(void) {
    char *shm_path;

    shm_path = (char*)malloc(strlen(proxy_name) + strlen(SUBSCRIBE_SUFFIX) + 2);
    sprintf(shm_path, "/%s%s", proxy_name, SUBSCRIBE_SUFFIX);
    return shm_path;
}

static bool proxy_subscribe_shm_create(const char *proxy_path) {
    int fd;
    char buff[PROXYLOGBUFLEN];
    pthread_mutexattr_t mutexattr;
    pthread_condattr_t condattr;

    fd = shm_open(proxy_path, O_RDWR, S_IRUSR | S_IWUSR);
    if( errno == ENOENT ) {
        fd = shm_open(proxy_path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if(fd>-1) {
            ftruncate(fd, sizeof(ProxySubscribeShm)); //set size
        }
    }

    if(fd==-1) {
        strerror_r(errno, buff, PROXYLOGBUFLEN);
        proxy_log("ERROR", "shm %s creation failed, %s", proxy_path, buff);
        return false;
    }

    proxy_subscribe_shm = (ProxySubscribeShm*)mmap(NULL, sizeof(ProxySubscribeShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);

    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutexattr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_NORMAL);
    if(pthread_mutex_init(&proxy_subscribe_shm->lock, &mutexattr) == EBUSY) {
        if(pthread_mutex_consistent(&proxy_subscribe_shm->lock) == 0) {
            pthread_mutex_unlock(&proxy_subscribe_shm->lock);
        }
    }
    pthread_mutexattr_destroy(&mutexattr);//mutexattr is no longer needed

    pthread_condattr_init(&condattr);
    pthread_condattr_setpshared(&condattr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&proxy_subscribe_shm->dispatcher_wakeup, &condattr);
    pthread_cond_init(&proxy_subscribe_shm->proxy_wakeup, &condattr);
    pthread_condattr_destroy(&condattr);//condattr is no longer needed

    proxy_subscribe_shm->state = SUBSCRIBE_INIT;
    proxy_subscribe_shm->aid[0] = 0;
    proxy_subscribe_shm->payload_buff_length = 0;
    proxy_subscribe_shm->remove_request = false;

    return true;
}

static void proxy_subscribe_shm_idle(void) {
    proxy_subscribe_shm->state = SUBSCRIBE_IDLE;
    proxy_subscribe_shm->aid[0] = 0;
    proxy_subscribe_shm->payload_buff_length = 0;
    proxy_subscribe_shm->remove_request = false;
}

static enum ProxySubscribeState proxy_subscribe_exchange(const ReplyQueueTask *task) {
    char *answer_path;
    enum ProxySubscribeState state;

    proxy_uuid_generate(proxy_subscribe_shm->aid);
    answer_path = (char*)malloc(strlen(proxy_subscribe_shm->aid) + 2);
    sprintf(answer_path, "/%s", proxy_subscribe_shm->aid);

    state = SUBSCRIBE_FAILED;
    do {
        proxy_subscribe_shm->payload_buff_length = proxy_subscribe_create_answer(answer_path, task->task);
        if(proxy_subscribe_shm->payload_buff_length<1) {
            break;
        }

        proxy_subscribe_shm->remove_request = !task->multiple_respond;
        proxy_subscribe_shm->state = SUBSCRIBE_ANSWER;
        pthread_cond_signal(&proxy_subscribe_shm->dispatcher_wakeup);

        if(pthread_cond_wait(&proxy_subscribe_shm->proxy_wakeup, &proxy_subscribe_shm->lock)==EOWNERDEAD){
            pthread_mutex_consistent(&proxy_subscribe_shm->lock);
            state = SUBSCRIBE_TERMINATION;
        } else {
            state = proxy_subscribe_shm->state;
        }
        shm_unlink(answer_path);
    } while(false);


    free(answer_path);
    return state;
}

static size_t proxy_subscribe_create_answer(const char *path, const char *task)
{
    char buff[PROXYLOGBUFLEN], *shm_buff;
    int fd = -1;
    size_t buff_len;

    do {
        fd = shm_open(path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if(fd==-1) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log("ERROR", "proxy %s subscribe shared memory creation failed, %s", proxy_name, buff);
            buff_len = 0;
            break;
        }
        buff_len = strlen(task) + 1;
        ftruncate(fd, buff_len);

        shm_buff = (char*)mmap(NULL, buff_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if(shm_buff==MAP_FAILED) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log("ERROR", "proxy %s subscribe shared memory map failed, %s", proxy_name, buff);
            shm_unlink(path);
            buff_len = 0;
            break;
        }

        strcpy(shm_buff, task);
        munmap(shm_buff, buff_len);
    } while(false);

    if(fd!=-1) {
        close(fd);
    }

    return buff_len;
}