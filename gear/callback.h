#ifndef _CALLBACK_H_
#define _CALLBACK_H_

#include <stdbool.h>
#include <glib.h>

#include "cJSON.h"
#include "define.h"

enum ProxyPayloadParseResult {
    PARSE_INVALID = 1,
    PARSE_SINGLESHOT = 2,
    PARSE_MULTIRESPOND = 3
};

typedef enum ProxyPayloadParseResult (*ProxyPayloadParse) (
    const char *service_name, 
    const cJSON *payload, 
    cJSON **normalized_payload, 
    bool *unsubscribe,
    unsigned int *invalid_status);

typedef struct ProxyReplyArg {
    char *service;
    cJSON *payload; 
} ProxyReplyArg;

typedef void (*ProxyReply) (const ProxyReplyArg *arg, cJSON *headers, cJSON *payload);
typedef void (*ProxyFree) (ProxyReplyArg *arg);
typedef void (*ProxyStart) (void);
typedef void (*ProxyRun) (ProxyReplyArg *arg, ProxyReply f_proxy_reply, ProxyFree f_proxy_free);
typedef void (*ProxyMultiRespondClear) (ProxyReplyArg *arg, ProxyFree f_proxy_free);
typedef void (*ProxyStop) (void);

#endif //_CALLBACK_H_