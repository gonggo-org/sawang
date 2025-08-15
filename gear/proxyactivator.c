#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "define.h"
#include "log.h"
#include "proxy.h"
#include "globaldata.h"

#define ACTIVATION_IDLE_TTL 5
#define ACTIVATION_IDLE_TIMEOUT_SEC 5
#define ACTIVATION_REQUEST_RESPOND_TIMEOUT_SEC 10

static ProxyActivationShm* proxy_activation_get_map(bool activation);

bool proxy_activate(void) {    
    ProxyActivationShm *shm = NULL;
    struct timespec ts;

    if( strlen(proxy_name) > (PROXYNAMEBUFLEN-1) ) {
        proxy_log("ERROR", "proxy name %s is way too long for activation (maximum %d character)", proxy_name, PROXYNAMEBUFLEN-1);
        return false;
    }

    shm = proxy_activation_get_map(true);
    if(shm==NULL) {
        return false;
    }

////successfully hold lock with state ACTIVATION_IDLE
////send ACTIVATION_REQUEST to dispatcher
    proxy_log("INFO", "%s sends ACTIVATION_REQUEST to %s", proxy_name, gonggo_name);
    strcpy(shm->proxy_name, proxy_name);
    shm->pid = getpid();
    shm->state = ACTIVATION_REQUEST;
    pthread_cond_signal(&shm->dispatcher_wakeup);
    pthread_mutex_unlock(&shm->lock);
    usleep(1000);

////wait for dispatcher respond    
    if( pthread_mutex_lock(&shm->lock)==EOWNERDEAD ) {
        proxy_log("ERROR", "%s is out of service for proxy %s activation, ACTIVATION_REQUEST is not answered", gonggo_name, proxy_name);
        munmap(shm, sizeof(ProxyActivationShm));
        return false;
    }

    if(shm->state == ACTIVATION_REQUEST) {//if still unchanged then wait
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += ACTIVATION_REQUEST_RESPOND_TIMEOUT_SEC; //wait n seconds from now
        if( pthread_cond_timedwait( &shm->proxy_wakeup, &shm->lock, &ts )==EOWNERDEAD ) {
            proxy_log("ERROR", "%s is out of service for proxy %s activation, ACTIVATION_REQUEST is not answered", gonggo_name, proxy_name);
            munmap(shm, sizeof(ProxyActivationShm));
            return false;
        }
    }

    if(shm->state == ACTIVATION_PROXY_DYING || shm->state == ACTIVATION_FAILED) {
        proxy_log("ERROR", "%s answers proxy %s ACTIVATION_REQUEST with %s", gonggo_name, proxy_name,
            shm->state == ACTIVATION_PROXY_DYING ? "ACTIVATION_PROXY_DYING" : "ACTIVATION_FAILED");
        pthread_mutex_unlock(&shm->lock);
        munmap(shm, sizeof(ProxyActivationShm));
        return false;
    }

    if(shm->state != ACTIVATION_SUCCESS) {
        proxy_log("ERROR", "%s answers proxy %s ACTIVATION_REQUEST with unexpected state %d", gonggo_name, proxy_name, shm->state);
        pthread_mutex_unlock(&shm->lock);
        munmap(shm, sizeof(ProxyActivationShm));
        return false;
    }

////respond dispatcher with ACTIVATION_DONE
    proxy_log("INFO", "%s answers proxy %s ACTIVATION_REQUEST with ACTIVATION_SUCCESS", gonggo_name, proxy_name);
    proxy_log("INFO", "%s sends ACTIVATION_DONE to %s", proxy_name, gonggo_name);
    shm->state = ACTIVATION_DONE;
    pthread_cond_signal(&shm->dispatcher_wakeup);
    pthread_mutex_unlock(&shm->lock);

    munmap(shm, sizeof(ProxyActivationShm));
    return true;
}

static ProxyActivationShm* proxy_activation_get_map(bool activation) {
    ProxyActivationShm* map = NULL;
    char *gonggo_path;
    char buff[PROXYLOGBUFLEN];
    int fd, status;
    const char *sact = activation ? "activation" : "deactivation";
    struct timespec ts;

    gonggo_path = (char*)malloc(strlen(gonggo_name) + 2);
    sprintf(gonggo_path, "/%s", gonggo_name);

    fd = shm_open(gonggo_path, O_RDWR, S_IRUSR | S_IWUSR);
    if(fd==-1) {
        strerror_r(errno, buff, PROXYLOGBUFLEN);
        proxy_log("ERROR", "open gonggo shared memory %s for proxy %s %s is failed, %s", gonggo_path, proxy_name, sact, buff);
        free(gonggo_path);
        return NULL;
    }

    map = (ProxyActivationShm*)mmap(NULL, sizeof(ProxyActivationShm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if( map == MAP_FAILED ) {
        strerror_r(errno, buff, PROXYLOGBUFLEN);
        proxy_log("ERROR", "map gonggo shared memory %s for proxy %s %s is failed, %s", gonggo_path, proxy_name, sact, buff);
        free(gonggo_path);
        close(fd);        
        return NULL;
    }

    free(gonggo_path);
    close(fd);

    if(pthread_mutex_lock(&map->lock)==EOWNERDEAD) {
        proxy_log("ERROR", "%s is out of service for proxy %s %s", gonggo_name, proxy_name, sact);
        munmap(map, sizeof(ProxyActivationShm));
        return NULL;
    }

    int ttl = ACTIVATION_IDLE_TTL;
    status = 0;
    while(ttl-->0 && map->state != ACTIVATION_IDLE) {
    ////put in loop for racing with other proxies
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += ACTIVATION_IDLE_TIMEOUT_SEC; //wait n seconds from now
        status = pthread_cond_timedwait( &map->proxy_wakeup, &map->lock, &ts);
        if(status==EOWNERDEAD) {
            proxy_log("ERROR", "%s is out of service for proxy %s %s", gonggo_name, proxy_name, sact);
            munmap(map, sizeof(ProxyActivationShm));
            return NULL;
        } else if(status==ETIMEDOUT && ttl<1) {
            proxy_log("ERROR", "%s is busy for proxy %s %s", gonggo_name, proxy_name, sact);
            munmap(map, sizeof(ProxyActivationShm));
            return NULL;
        }
    }

    if(map->state != ACTIVATION_IDLE) {
        proxy_log("ERROR", "%s is busy for proxy %s %s", gonggo_name, proxy_name, sact);
        munmap(map, sizeof(ProxyActivationShm));
        if(status==0) {
            pthread_mutex_unlock(&map->lock);      
        }
        return false;
    }

    return map;
}


