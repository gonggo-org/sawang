#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include "confvar.h"

static ConfVar* create(FILE *fp);
static ConfVar* parse(char *line);
static char* squeeze_space(char *stmt);

#define BUFLEN 50

ConfVar* confvar_validate(const char *file, char** error) {
    ConfVar* cv;
    char buff[BUFLEN];

    *error = NULL;
    do {
        cv = confvar_create(file);
        if( cv == NULL ) {
            *error = strdup("invalid configuration file content");
            break;
        }

    	if( confvar_absent(cv, buff, BUFLEN) > 0 ) {
            asprintf(error, "configuration file does not have key %s", buff);
            break;
        }

    } while(false);

    if(*error!=NULL && cv!=NULL) {
        confvar_destroy(cv);
        cv = NULL;
    }

    return cv;
}

ConfVar* confvar_create(const char *file) {
    FILE *fp;
    ConfVar* head;

    head = NULL;
    fp = fopen(file, "r");
    if(fp==NULL)
        perror("Error");
    else {
        head = create(fp);
        fclose(fp);
    }

    return head;
}

void confvar_destroy(ConfVar *head) {
    ConfVar *next;

    while( head != NULL ) {
        next = head->next;
        free(head->name);
        free(head->value);
        free(head);
        head = next;
    }

    return;
}

size_t confvar_absent(const ConfVar *head, char *absent_key, size_t buflen) {
    const char *mandatory[] = {CONF_PIDFILE, CONF_LOGPATH, CONF_SAWANG, CONF_GONGGO, NULL};
    const char **p;

    *absent_key = '\0';
    p = mandatory;
    while( *p != NULL ) {
        if( confvar_value(head, *p) == NULL ) {
            strncpy(absent_key, *p, buflen);
            break;
        }
        p++;
    }

    absent_key[buflen-1] = '\0';
    return strlen(absent_key);
}

const char* confvar_value(const ConfVar *head, const char *key) {
    const char *v;
    const ConfVar *p;

    v = NULL;
    p = head;
    while( p != NULL ) {
        if( strcmp(p->name, key) == 0 ) {
            v = p->value;
            break;
        }
        p = p->next;
    }

    return v;
}

bool confvar_long(const ConfVar *head, const char *key, long *value) {
    const char* s;
    char *endptr;
    long l;

    s = confvar_value(head, key);
    if(s==NULL)
        return false;
    l = strtol(s, &endptr, 10);
    if(errno == ERANGE)
        return false;
    if(value!=NULL)
        *value = l;
    return *endptr == '\0';
}

bool confvar_float(const ConfVar *head, const char *key, float *value) {
    const char* s;
    char *endptr;
    float f;

    s = confvar_value(head, key);
    if(s==NULL)
        return false;
    f = strtof(s, &endptr);
    if(errno == ERANGE)
        return false;
    if(value!=NULL)
        *value = f;
    return *endptr == '\0';
}

bool confvar_uint(const ConfVar *head, const char *key, unsigned int *value) {
    const char* s;
    char *endptr;
    unsigned int ui;

    s = confvar_value(head, key);
    if(s==NULL)
        return false;
    ui = strtoul(s, &endptr, 10);
    if(errno == ERANGE)
        return false;
    if(value!=NULL)
        *value = ui;
    return *endptr == '\0';    
}

static ConfVar* create(FILE *fp) {
    ConfVar *head, *p, *run;
    char *lineptr;
    size_t n;

    head = NULL;
    lineptr = NULL;
    n = 0;
    while( getline(&lineptr, &n, fp) > -1 ) {
        p = parse(lineptr);
        if( p != NULL ) {
            if( head == NULL )
                head = p;
            else
                run->next = p;
            run = p;
        }
    }
    if(lineptr!=NULL) {
        free(lineptr);
    }

    return head;
}

static ConfVar* parse(char *line) {
    char *t, *key, *val;
    ConfVar* cv;

    cv = NULL;

    t = strchr(line, '\n');
    if( t != NULL )
        *t = '\0';

    t = strchr(line, '=');
    if( t != NULL ) {
        *t = '\0';//replace newline with null string terminator
        key = squeeze_space(line);
        val = squeeze_space(t+1);
        if( strlen(val) > 0 ) {
            cv = (ConfVar*)malloc(sizeof(ConfVar));
            cv->name = strdup(key);
            cv->value = strdup(val);
            cv->next = NULL;
        }
    }

    return cv;
}

static char* squeeze_space(char *stmt) {
    char *p, *run;

    p = run = stmt;
    while( *p != '\0' && *p != '#' ) {
        if( *p != ' ' && *p != '\t' )
            *(run++) = *p;
        p++;
    }
    *run = '\0';

    return stmt;
}