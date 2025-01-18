#ifndef _UTIL_H_
#define _UTIL_H_

#include <glib.h>

extern void proxy_cond_reset(pthread_cond_t *cond);
extern gboolean str_equal(const char *s1, const char *s2);
extern char* str_dup(const char *s, gpointer data);

#endif //_UTIL_H_