#ifndef _PROXYUUID_H_
#define _PROXYUUID_H_

#include "define.h"

extern void proxy_uuid_init(void);
extern void proxy_uuid_destroy(void);
extern void proxy_uuid_generate(char buff[UUIDBUFLEN]);

#endif //_PROXYUUID_H_