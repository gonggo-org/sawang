#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "define.h"
#include "error.h"
#include "confvar.h"
#include "log.h"
#include "respondtable.h"
#include "replyqueue.h"
#include "parsequeue.h"
#include "proxychannel.h"
#include "proxysubscribe.h"
#include "gonggoalive.h"
#include "proxyactivator.h"
#include "proxycomm.h"
#include "proxyuuid.h"
#include "alivemutex.h"
#include "callback.h"

volatile bool proxy_exit = false;
const char *gonggo_name = NULL;
const char *proxy_name = NULL;

static void handler(int signal, siginfo_t *info, void *context);
static void clean_up(void);
static void threads_stop(pthread_t t_proxy_channel, pthread_t t_proxy_subscribe, pthread_t t_gonggo_alive, pthread_t t_proxy_comm);

int work(pid_t pid, const ConfVar *cv_head, 
	ProxyPayloadParse f_payload_parse, 
	ProxyStart f_start, ProxyRun f_run, ProxyMultiRespondClear f_multirespond_clear, ProxyStop f_stop) 
{
	struct sigaction action;	
	char buff[PROXYLOGBUFLEN];
    pthread_t t_proxy_channel, t_proxy_subscribe, t_gonggo_alive, t_proxy_comm;    	
    pthread_attr_t thread_attr;
	
	proxy_uuid_init();

	gonggo_name = confvar_value(cv_head, CONF_GONGGO); 
    proxy_name = confvar_value(cv_head, CONF_SAWANG);
	
	proxy_log_context_init(pid, confvar_value(cv_head, CONF_LOGPATH));

	if(f_payload_parse==NULL) {
		proxy_log("ERROR", "f_payload_parse is NULL");
		return ERROR_START;
	}

	if(f_run==NULL) {
		proxy_log("ERROR", "f_run is NULL");
		return ERROR_START;
	}

    memset(&action, 0, sizeof(action));
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = handler;
    if(sigaction(SIGTERM, &action, NULL)==-1) {
        strerror_r(errno, buff, PROXYLOGBUFLEN);
        proxy_log("ERROR", "sigaction failed %s", buff);
        clean_up();
        return ERROR_TERMHANDLER;
    }

////tables:BEGIN
	respond_table_create();
	reply_queue_create();
	parse_queue_create();
////tables:END    

	//create proxy alive shared memory
	if(!alive_mutex_create(true)) {
		clean_up();
		return ERROR_START;
	}	

////thread context initialization:BEGIN	    
    if(!proxy_channel_context_init(f_payload_parse)) {
		clean_up();
		return ERROR_START;
	}
	
    if(!proxy_subscribe_context_init()){
		proxy_channel_shm_unlink_enable();
        proxy_channel_context_destroy();
		clean_up();
		return ERROR_START;
	}

	if(!gonggo_alive_context_init()) {
		proxy_channel_shm_unlink_enable();
		proxy_channel_context_destroy();
		proxy_subscribe_shm_unlink_enable();
		proxy_subscribe_context_destroy();
		clean_up();
		return ERROR_START;
	}

	proxy_comm_context_init(f_start, f_run, f_multirespond_clear, f_stop);
////thread context initialization:END

    pthread_attr_init(&thread_attr);
    pthread_attr_setdetachstate(&thread_attr, PTHREAD_CREATE_JOINABLE);
	bool started = false;
	do {
		if(pthread_create(&t_proxy_channel, &thread_attr, proxy_channel, NULL)!=0) {
			proxy_log("ERROR", "cannot start server, %s", "proxy channel thread creation is failed");
			break;
		}
		proxy_channel_waitfor_started();

		if(pthread_create(&t_proxy_subscribe, &thread_attr, proxy_subscribe, NULL)!=0) {
			proxy_log("ERROR", "cannot start server, %s", "proxy subscribe thread creation is failed");
			break;
		}
		proxy_subscribe_waitfor_started();

		if(pthread_create(&t_gonggo_alive, &thread_attr, gonggo_alive, NULL)!=0) {
			proxy_log("ERROR", "cannot start server, %s", "gonggo alive thread creation is failed");
			break;
		}
		gonggo_alive_waitfor_started();

		if(pthread_create(&t_proxy_comm, &thread_attr, proxy_comm, NULL)!=0) {
			proxy_log("ERROR", "cannot start server, %s", "proxy communication thread creation is failed");
			break;
		}
		proxy_comm_waitfor_started();

		alive_mutex_lock();
		started = proxy_activate();
	} while(false);
	pthread_attr_destroy(&thread_attr);//destroy thread-attribute

    if(!started)  {
		alive_mutex_die();	
		proxy_exit = true;
		threads_stop(t_proxy_channel, t_proxy_subscribe, t_gonggo_alive, t_proxy_comm);
		clean_up();
        return ERROR_START;
	}

	proxy_log("INFO", "proxy %s started", proxy_name);

	while(!proxy_exit) {
        pause();
	}

    proxy_log("INFO", "proxy %s is stopping", proxy_name);

	alive_mutex_die();
    threads_stop(t_proxy_channel, t_proxy_subscribe, t_gonggo_alive, t_proxy_comm);
    proxy_log("INFO", "proxy %s is stopped", proxy_name);
    clean_up();

    return 0;
}

static void handler(int signal, siginfo_t *info, void *context) {
    if(signal==SIGTERM) {
        proxy_exit = true;
	}
}

static void clean_up(void) {
    proxy_log_context_destroy();
	proxy_uuid_destroy();
	respond_table_destroy();
	reply_queue_destroy();
	parse_queue_destroy();
 	alive_mutex_destroy();
}

static void threads_stop(pthread_t t_proxy_channel, pthread_t t_proxy_subscribe, pthread_t t_gonggo_alive, pthread_t t_proxy_comm) 
{	
	if(proxy_channel_isstarted()) { 
		proxy_log("INFO", "proxy_channel thread stopping");
		proxy_channel_stop(); 
		proxy_log("INFO", "proxy_channel thread stopping done");
	}
	if(proxy_subscribe_isstarted()) { 
		proxy_log("INFO", "proxy_subscribe thread stopping");
		proxy_subscribe_stop(); 
		proxy_log("INFO", "proxy_subscribe thread stopping done");
	}
	if(gonggo_alive_isstarted()) { 
		proxy_log("INFO", "gonggo_alive thread stopping");
		gonggo_alive_stop(t_gonggo_alive); 
		proxy_log("INFO", "gonggo_alive thread stopping done");
	}
	if(proxy_comm_isstarted()) { 
		proxy_log("INFO", "proxy_comm thread stopping");
		proxy_comm_stop(); 
		proxy_log("INFO", "proxy_comm thread stopping done");
	}

	if(proxy_channel_isstarted()) { 
		proxy_log("INFO", "proxy_channel thread joining");
		pthread_join(t_proxy_channel, NULL); 
		proxy_log("INFO", "proxy_channel thread joining done");
	}
	if(proxy_subscribe_isstarted()) { 
		proxy_log("INFO", "proxy_subscribe thread joining");
		pthread_join(t_proxy_subscribe, NULL); 
		proxy_log("INFO", "proxy_subscribe thread joining done");
	}
	if(gonggo_alive_isstarted()) { 
		proxy_log("INFO", "gonggo_alive thread joining");
		pthread_join(t_gonggo_alive, NULL); 
		proxy_log("INFO", "gonggo_alive thread joining done");
	}
	if(proxy_comm_isstarted()) { 
		proxy_log("INFO", "proxy_comm thread joining");
		pthread_join(t_proxy_comm, NULL); 
		proxy_log("INFO", "proxy_comm thread joining done");
	}

	proxy_channel_context_destroy();
    proxy_subscribe_context_destroy();
	gonggo_alive_context_destroy();
	proxy_comm_context_destroy();
}