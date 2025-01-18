#ifndef _DEFINE_H_
#define _DEFINE_H_

#define PROXYLOGBUFLEN 500
#define TMSTRBUFLEN 35
#define SHMPATHBUFLEN 51
#define PROXYNAMEBUFLEN 51
#define UUIDBUFLEN 37

/*SERVICE_*** must be the same as gonggo*/
#define SERVICE_HEADERS_KEY "headers"
#define SERVICE_PAYLOAD_KEY "payload"
#define SERVICE_PROXY_KEY "proxy"
#define SERVICE_SERVICE_KEY "service"
#define SERVICE_RID_KEY "rid"

/*SERVICE_STATUS_KEY must be the same as gonggo CLIENTREPLY_SERVICE_STATUS_KEY*/
#define SERVICE_STATUS_KEY "serviceStatus"

/*GONGGOSERVICE_REQUEST_DROP must be the same as gonggo*/
#define GONGGOSERVICE_REQUEST_DROP "gonggorequestdrop"

#endif //_DEFINE_H_