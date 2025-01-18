#ifndef _PROXYSERVICESTATUS_H_
#define _PROXYSERVICESTATUS_H_

/*ProxyServiceStatus must match gonggo ProxyServiceStatus*/
enum ProxyServiceStatus {
    PROXYSERVICESTATUS_ALIVE_START = 1,
    PROXYSERVICESTATUS_ALIVE_TERMINATION = 2,
    PROXYSERVICESTATUS_MULTIRESPOND_CLEAR_SUCCESS = 3,
    PROXYSERVICESTATUS_MULTIRESPOND_CLEAR_PAYLOAD_MISSING = 4,
    PROXYSERVICESTATUS_MULTIRESPOND_CLEAR_PAYLOAD_RID_MISSING = 5,
    PROXYSERVICESTATUS_MULTIRESPOND_CLEAR_PAYLOAD_RID_INVALID = 6
};

#endif //_PROXYSERVICESTATUS_H_