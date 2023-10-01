#ifndef _SAWANG_CONFVAR_H_
#define _SAWANG_CONFVAR_H_

#define CONF_PIDFILE "pidfile"
#define CONF_LOGPATH "logpath"
#define CONF_SAWANG "sawang"
#define CONF_HEARTBEATPERIOD "heartbeatperiod"
#define CONF_HEARTBEATTIMEOUT "heartbeattimeout"
#define CONF_GONGGO "gonggo"

#include <stdbool.h>

typedef struct ConfVar
{
	char *name;
	char *value;
	struct ConfVar *next;
} ConfVar;

extern ConfVar* confvar_validate(const char *file, char** error);
extern ConfVar* confvar_create(const char *file);
extern void confvar_destroy(ConfVar *head);
extern size_t confvar_absent(const ConfVar *head, char *absent_key, size_t buflen);
extern const char* confvar_value(const ConfVar *head, const char *key);
extern bool confvar_long(const ConfVar *head, const char *key, long *value);
extern bool confvar_float(const ConfVar *head, const char *key, float *value);

#endif //_SAWANG_CONVAR_H_
