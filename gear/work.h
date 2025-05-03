#ifndef _WORK_H_
#define _WORK_H_

#include "confvar.h"
#include "callback.h"

extern int work(pid_t pid, const ConfVar *cv_head, 
    ProxyPayloadParse f_payload_parse, 
    ProxyStart f_start, ProxyRun f_run, ProxyMultiRespondClear f_multirespond_clear, ProxyStop f_stop,
    ProxyRest f_rest);

#endif //_WORK_H_