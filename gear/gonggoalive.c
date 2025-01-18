#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>

#include "globaldata.h"
#include "log.h"
#include "define.h"
#include "gonggoalive.h"
#include "proxychannel.h"
#include "proxysubscribe.h"
#include "alivemutex.h"

//property
static volatile bool gonggo_alive_started = false;
static bool gonggo_dead = false;

//function
static GonggoAliveMutexShm *gonggo_alive_shm = NULL; 
static GonggoAliveMutexShm *gonggo_alive_create_shm(void);
static void gonggo_alive_cleanup(void *arg);

bool gonggo_alive_context_init(void) {
    gonggo_alive_shm = gonggo_alive_create_shm();
    if(gonggo_alive_shm==NULL) {
        return false;
    }        
    return true;
}

void gonggo_alive_context_destroy(void) {
    if(gonggo_alive_shm!=NULL) {
        munmap(gonggo_alive_shm, sizeof(GonggoAliveMutexShm));
        gonggo_alive_shm = NULL;   
    }
}

void* gonggo_alive(void *arg) {
    int status;
    int oldstate, oldtype;
    
    pthread_cleanup_push(gonggo_alive_cleanup, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &oldstate);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

    proxy_log("INFO", "proxy %s gonggo alive thread is started", proxy_name);

    gonggo_alive_started = true;

    while(!gonggo_dead) {    
        status = pthread_mutex_lock(&gonggo_alive_shm->lock);
        gonggo_dead = status==EOWNERDEAD || (status==0 && !gonggo_alive_shm->alive);
        if(gonggo_dead) {
            if(gonggo_alive_shm->alive) {//indicating gonggo dies not gracefully
                proxy_channel_shm_unlink_enable();
                proxy_subscribe_shm_unlink_enable();
                alive_mutex_unlink_enable();
            }
            proxy_exit = true;
            kill(getpid(), SIGTERM);
        }
        if(status==0) {
            pthread_mutex_unlock(&gonggo_alive_shm->lock);                
            if(!gonggo_dead) {
                usleep(1000); //let gonggo regain the lock
            }
        }
    }

    pthread_cleanup_pop(1);
    proxy_log("INFO", "proxy %s gonggo alive thread is stopped", proxy_name);
    pthread_exit(NULL);
}

void gonggo_alive_waitfor_started(void) {
	while(!gonggo_alive_started) {
		usleep(1000);
	}
}

bool gonggo_alive_isstarted(void) {
    return gonggo_alive_started;
}

void gonggo_alive_stop(pthread_t t) {
    if(!gonggo_dead) {
        pthread_cancel(t);
    }
}

static void gonggo_alive_cleanup(void *arg) {
    proxy_log("INFO", "proxy %s gonggo alive cleanup", proxy_name);
    pthread_mutex_unlock(&gonggo_alive_shm->lock);
}

static GonggoAliveMutexShm* gonggo_alive_create_shm() {
    int fd = -1, status;
    GonggoAliveMutexShm* map = NULL;
    char buff[PROXYLOGBUFLEN];
    char *path;
    
    asprintf(&path, "/%s_alive", gonggo_name);
    do {
        fd = shm_open(path, O_RDWR, S_IRUSR | S_IWUSR);
        if(fd==-1) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log("ERROR", "shared memory %s access failed, %s", path, buff);
            break;
        }    

        map = (GonggoAliveMutexShm*)mmap(NULL, sizeof(GonggoAliveMutexShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if(map == MAP_FAILED) {
            strerror_r(errno, buff, PROXYLOGBUFLEN);
            proxy_log("ERROR", "shared memory %s access failed, %s", path, buff);
            map = NULL;
            break;
        }
    } while(false);

    free(path);
    if(fd!=-1) {
        close(fd);  
    }

////check if gonggo alive
    status = pthread_mutex_trylock(&map->lock);
    if(status==0) {
        pthread_mutex_unlock(&map->lock);
    } else if(status!=EBUSY || !map->alive) {
        munmap(map, sizeof(GonggoAliveMutexShm));
        map = NULL;
        proxy_log("ERROR", "%s is not alive", gonggo_name);
    }
    
    return map;
}