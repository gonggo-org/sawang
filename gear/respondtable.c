#include "respondtable.h"
#include "util.h"

#ifdef GLIBSHIM
#include "glibshim.h"
#endif //GLIBSHIM

static bool respond_table_has_lock = false;
static pthread_mutex_t respond_table_lock;

static bool respond_table_has_table = false;
static GHashTable *singleshot_table = NULL;
static GHashTable *multirespond_table = NULL;

//map request-uuid to SingleShotTableContext
static GHashTable *respond_table_which(enum RespondTableType which);
static void respond_table_value_destroy(GPtrArray* arr);
//return true on new entry
static bool respond_table_set_do(GHashTable *table, const char *task_key, const char* request_uuid);
static bool respond_table_drop_do(GHashTable *table, const char *task_key, const char* request_uuid, guint *remaining);

void respond_table_create(void) {
    pthread_mutexattr_t mtx_attr;

    if(!respond_table_has_lock) {
        pthread_mutexattr_init(&mtx_attr);
        pthread_mutexattr_setpshared(&mtx_attr, PTHREAD_PROCESS_PRIVATE);
        pthread_mutexattr_settype(&mtx_attr, PTHREAD_MUTEX_NORMAL);
        pthread_mutex_init(&respond_table_lock, &mtx_attr);
        pthread_mutexattr_destroy(&mtx_attr);
        respond_table_has_lock = true;
    }

    if(!respond_table_has_table) {
        singleshot_table = g_hash_table_new_full(g_str_hash, g_str_equal, (GDestroyNotify)free, (GDestroyNotify)respond_table_value_destroy);
        multirespond_table = g_hash_table_new_full(g_str_hash, g_str_equal, (GDestroyNotify)free, (GDestroyNotify)respond_table_value_destroy);
        respond_table_has_table = true;
    }
}

char *respond_table_request_uuid_dup(const char *s, gpointer data) {
    return strdup(s);
}

void respond_table_destroy(void) {
    if(respond_table_has_table) {
        g_hash_table_destroy(singleshot_table);   
        singleshot_table = NULL;
        g_hash_table_destroy(multirespond_table);
        multirespond_table = NULL;
        respond_table_has_table = false;
    }
    if(respond_table_has_lock) {
        pthread_mutex_destroy(&respond_table_lock);
        respond_table_has_lock = false;
    }
}

//return true on new entry
bool respond_table_set(enum RespondTableType which, const char *task_key, const char* request_uuid) {
    GHashTable *table;
    bool new_entry = false;

    if((table = respond_table_which(which))!=NULL) {
        pthread_mutex_lock(&respond_table_lock);
        new_entry = respond_table_set_do(table, task_key, request_uuid);
        pthread_mutex_unlock(&respond_table_lock);
    }
    return new_entry;
}

bool respond_table_drop(enum RespondTableType which, const char *task_key, const char* request_uuid, guint *remaining) {
    GHashTable *table;
    bool exists = false;

    if((table = respond_table_which(which))!=NULL){
        pthread_mutex_lock(&respond_table_lock);
        exists = respond_table_drop_do(table, task_key, request_uuid, remaining);
        pthread_mutex_unlock(&respond_table_lock);
    }
    return exists;
}

GPtrArray* respond_table_request_dup(enum RespondTableType which, const char *task_key) {
    GHashTable *table;
    GPtrArray *arr, *ret = NULL;

    if((table = respond_table_which(which))!=NULL) {
        pthread_mutex_lock(&respond_table_lock);
        arr = (GPtrArray*)g_hash_table_lookup(table, task_key);
        ret = arr!=NULL ? g_ptr_array_copy(arr, (GCopyFunc)str_dup, NULL) : NULL;
        pthread_mutex_unlock(&respond_table_lock);
    }
    return ret;
}

char *respond_table_dup_task_key(enum RespondTableType which, const char* request_uuid) {
    char *task_key = NULL;
    GHashTable *table;
    GHashTableIter iter;
    char *tk;
    GPtrArray* arr;

    if((table = respond_table_which(which))!=NULL) {
        pthread_mutex_lock(&respond_table_lock);
        g_hash_table_iter_init(&iter, table);
        while (g_hash_table_iter_next(&iter, (gpointer*)&tk, (gpointer*)&arr)) {
            if(g_ptr_array_find_with_equal_func(arr, request_uuid, (GEqualFunc)str_equal, NULL)) {
                task_key = strdup(tk);
                break;
            }
        }
        pthread_mutex_unlock(&respond_table_lock);
    }
    return task_key;
}

void respond_table_remove(enum RespondTableType which, const char *task_key) {
    GHashTable *table;

    if((table = respond_table_which(which))!=NULL) {
        pthread_mutex_lock(&respond_table_lock);
        g_hash_table_remove(table, task_key);
        pthread_mutex_unlock(&respond_table_lock);
    }
}

bool respond_table_task_exists(enum RespondTableType which, const char *task_key) {
    GHashTable *table;
    bool exists = false;

    if((table = respond_table_which(which))!=NULL) {
        pthread_mutex_lock(&respond_table_lock);
        exists = g_hash_table_lookup(table, task_key)!=NULL;
        pthread_mutex_unlock(&respond_table_lock);
    }
    return exists;
}

bool respond_table_request_exists(enum RespondTableType which, const char *task_key, const char *request_uuid) {
    GHashTable *table;
    GPtrArray *arr;
    bool exists = false;

    if((table = respond_table_which(which))!=NULL) {
        pthread_mutex_lock(&respond_table_lock);
        if( (arr = (GPtrArray*)g_hash_table_lookup(table, task_key))!=NULL ) {
            exists = g_ptr_array_find_with_equal_func(arr, request_uuid, (GEqualFunc)str_equal, NULL);
        }
        pthread_mutex_unlock(&respond_table_lock);
    }
    return exists;
}

static GHashTable *respond_table_which(enum RespondTableType which) {
    GHashTable *t;
    switch(which) {
        case RESPONDTABLE_SINGLESHOT:
            t = singleshot_table;
            break;
        case RESPONDTABLE_MULTIRESPOND:
            t = multirespond_table;
            break;
        default:
            t = NULL;
            break;
    }
    return t;
}

static void respond_table_value_destroy(GPtrArray* arr) {
    if(arr!=NULL) {
        g_ptr_array_free(arr, true);
    }
}

static bool respond_table_set_do(GHashTable *table, const char *task_key, const char* request_uuid) {
    GPtrArray* arr;
    bool new_entry = true;

    arr = (GPtrArray*)g_hash_table_lookup(table, task_key);
    if(arr==NULL) {
        arr = g_ptr_array_new_full(1, (GDestroyNotify)free);
        g_hash_table_insert(table, strdup(task_key), arr); 
    } else {
        new_entry = !g_ptr_array_find_with_equal_func(arr, request_uuid, (GEqualFunc)str_equal, NULL);
    }
    if(new_entry) {
        g_ptr_array_add(arr, strdup(request_uuid));
    }
    return new_entry;
}

static bool respond_table_drop_do(GHashTable *table, const char *task_key, const char* request_uuid, guint *remaining) {
    GPtrArray* arr;
    guint idx, count;
    bool exists = false;

    if(remaining!=NULL) {
        *remaining = 0;
    }
    arr = (GPtrArray*)g_hash_table_lookup(table, task_key);
    if( (exists = g_ptr_array_find_with_equal_func(arr, request_uuid, (GEqualFunc)str_equal, &idx)) ) {
        g_ptr_array_remove_index(arr, idx);
        count = arr->len;
        if(remaining!=NULL) {
            *remaining = count;
        }                
        if(count<1) {
            g_hash_table_remove(table, task_key);
        }
    }
    return exists;
}