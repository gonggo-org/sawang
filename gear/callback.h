#ifndef _CALLBACK_H_
#define _CALLBACK_H_

#include <stdbool.h>
#include <glib.h>

#include "cJSON.h"
#include "define.h"
#include "confvar.h"

enum ProxyPayloadParseResult {
    PARSE_INVALID = 1,
    PARSE_SINGLESHOT = 2,
    PARSE_MULTIRESPOND = 3
};

typedef struct ProxyCommData {
    const ConfVar *cv_head;
} ProxyCommData;

typedef struct ProxyReplyArg {
    char *service;
    cJSON *payload; 
} ProxyReplyArg;

typedef void (*ProxyReply) (const ProxyReplyArg *arg, cJSON *headers, cJSON *payload);
typedef void (*ProxyFree) (ProxyReplyArg *arg);

/*
 * To be implemented by customized proxy as required by work(...) defined in work.h
 * 1. ProxyPayloadParse: runs in proxychannel thread on client request parsing.
 * 2. ProxyStart: runs in proxy proxycomm thread start. A function to initialize resources.
 * 3. ProxyRun: runs in proxycomm loop.
 * 4. ProxyMultiRespondClear: runs in proxycomm loop to clear a multirespond service.
 * 5. ProxyStop: run in proxycomm thread stop. A function to destroy resources.
 */
typedef enum ProxyPayloadParseResult (*ProxyPayloadParse) (
    const char *service_name, 
    const cJSON *payload, 
    cJSON **normalized_payload, 
    bool *unsubscribe,
    unsigned int *invalid_status);
typedef void (*ProxyStart) (const ProxyCommData *pcd);
typedef void (*ProxyRun) (ProxyReplyArg *arg, ProxyReply f_proxy_reply, ProxyFree f_proxy_free);
typedef void (*ProxyMultiRespondClear) (ProxyReplyArg *arg, ProxyFree f_proxy_free);
typedef void (*ProxyStop) (void);

#endif //_CALLBACK_H_
