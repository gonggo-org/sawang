#ifndef _PROXYCOMM_H_
#define _PROXYCOMM_H_

#include <stdbool.h>

#include "callback.h"

extern void proxy_comm_context_init(ProxyStart f_start, ProxyRun f_run, ProxyMultiRespondClear f_multirespond_clear, ProxyStop f_stop); 
extern void proxy_comm_context_destroy(void);
extern void* proxy_comm(void *arg);
extern void proxy_comm_waitfor_started(void);
extern bool proxy_comm_isstarted(void);
extern void proxy_comm_awake(void);
extern void proxy_comm_stop(void);

#endif //_PROXYCOMM_H_