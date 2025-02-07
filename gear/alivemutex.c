#define _GNU_SOURCE
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "define.h"
#include "log.h"
#include "alivemutex.h"
#include "globaldata.h"

static char *alive_mutex_path = NULL;
static AliveMutexShm *alive_mutex_shm = NULL;
static bool alive_mutex_shm_unlink = false;

bool alive_mutex_create(bool alive) {
    int fd;
    char buff[PROXYLOGBUFLEN];
    pthread_mutexattr_t mutexattr;

    asprintf(&alive_mutex_path, "/%s_alive", proxy_name);
    fd = shm_open(alive_mutex_path, O_RDWR, S_IRUSR | S_IWUSR);
    if( errno == ENOENT ) {
        fd = shm_open(alive_mutex_path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        if(fd>-1) {
            ftruncate(fd, sizeof(AliveMutexShm)); //set size
        }
    }

    if(fd==-1) {
        strerror_r(errno, buff, PROXYLOGBUFLEN);
        proxy_log("ERROR", "shm %s creation failed, %s", alive_mutex_path, buff);
        free(alive_mutex_path);
        alive_mutex_path = NULL;
        return false;
    }

    alive_mutex_shm = (AliveMutexShm*)mmap(NULL, sizeof(AliveMutexShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(alive_mutex_shm==MAP_FAILED) {
        strerror_r(errno, buff, PROXYLOGBUFLEN);
        proxy_log("ERROR", "shm %s map failed, %s", alive_mutex_path, buff);
        close(fd);
        alive_mutex_shm = NULL;
        free(alive_mutex_path);
        alive_mutex_path = NULL;
        return false;
    }

    close(fd);

    pthread_mutexattr_init(&mutexattr);
    pthread_mutexattr_setpshared(&mutexattr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutexattr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_NORMAL);
    if(pthread_mutex_init(&alive_mutex_shm->lock, &mutexattr) == EBUSY) {
        if(pthread_mutex_consistent(&alive_mutex_shm->lock) == 0) {
            pthread_mutex_unlock(&alive_mutex_shm->lock);
        }
    }
    pthread_mutexattr_destroy(&mutexattr);//mutexattr is no longer needed

    alive_mutex_shm->alive = alive;
    return true;
}

void alive_mutex_unlink_enable(void) {
    alive_mutex_shm_unlink = true;
}

void alive_mutex_lock(void) {
    pthread_mutex_lock(&alive_mutex_shm->lock);
}

void alive_mutex_die(void) {
    alive_mutex_shm->alive = false;
	pthread_mutex_unlock(&alive_mutex_shm->lock);
}

void alive_mutex_destroy(void) {
    if(alive_mutex_shm!=NULL) {
        pthread_mutex_destroy(&alive_mutex_shm->lock);
        munmap(alive_mutex_shm, sizeof(AliveMutexShm));
    }
    if(alive_mutex_path!=NULL) {
        if(alive_mutex_shm_unlink) {
            shm_unlink(alive_mutex_path);
        }
        free(alive_mutex_path);
        alive_mutex_path = NULL;
    }
}