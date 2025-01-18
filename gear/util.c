#define _GNU_SOURCE
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>

void proxy_cond_reset(pthread_cond_t *cond) {
////pthread_cond_destroy hangs when thread waiting on the condition-signal is killed in rough way
////the workaround is reset __wrefs to 0
    cond->__data.__wrefs = 0;//in case 
}

gboolean str_equal(const char *s1, const char *s2) {
    return strcmp(s1, s2) == 0;
}

char* str_dup(const char *s, gpointer data) {
    return s!=NULL ? strdup(s) : NULL;
}