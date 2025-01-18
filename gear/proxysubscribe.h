#ifndef _PROXYSUBSCRIBE_H_
#define _PROXYSUBSCRIBE_H_

extern bool proxy_subscribe_context_init(void);
extern void proxy_subscribe_shm_unlink_enable(void);
extern void proxy_subscribe_context_destroy(void);
extern void* proxy_subscribe(void *arg);
extern void proxy_subscribe_waitfor_started(void);
extern bool proxy_subscribe_isstarted(void);
extern void proxy_subscribe_awake(void);
extern void proxy_subscribe_stop(void);

#endif //_PROXYSUBSCRIBE_H_