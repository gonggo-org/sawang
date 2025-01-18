#ifndef _PROXYCHANNEL_H_
#define _PROXYCHANNEL_H_

#include <stdbool.h>

#include "callback.h"

extern bool proxy_channel_context_init(ProxyPayloadParse f_payload_parse);
extern void proxy_channel_shm_unlink_enable(void);
extern void proxy_channel_context_destroy(void);
extern void* proxy_channel(void *arg);
extern void proxy_channel_waitfor_started(void);
extern bool proxy_channel_isstarted(void);
extern void proxy_channel_stop(void);

#endif //_PROXYCHANNEL_H_