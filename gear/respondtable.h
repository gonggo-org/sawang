#ifndef _RESPONDTABLE_H_
#define _RESPONDTABLE_H_

#include <stdbool.h>
#include <glib.h>

enum RespondTableType {
    RESPONDTABLE_SINGLESHOT = 1,
    RESPONDTABLE_MULTIRESPOND = 2
};

//map task_key to request_UUID array
//where task_key is unformatted json {"service":"test", "payload":{"key1":"value1", "key2":"value2"}}
extern void respond_table_create(void);
extern char *respond_table_request_uuid_dup(const char *s, gpointer data);
extern void respond_table_destroy(void);
extern bool respond_table_set(enum RespondTableType which, const char *task_key, const char* request_uuid);//return true on new entry
extern bool respond_table_drop(enum RespondTableType which, const char *task_key, const char* request_uuid, guint *remaining);
extern GPtrArray *respond_table_request_dup(enum RespondTableType which, const char *task_key);
extern char *respond_table_dup_task_key(enum RespondTableType which, const char* request_uuid);
extern void respond_table_remove(enum RespondTableType which, const char *task_key);
extern bool respond_table_task_exists(enum RespondTableType which, const char *task_key);
extern bool respond_table_request_exists(enum RespondTableType which, const char *task_key, const char *request_uuid);

#endif //_RESPONDTABLE_H_