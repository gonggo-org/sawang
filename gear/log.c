#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>

#include "define.h"
#include "log.h"
#include "globaldata.h"

#ifdef HAVE_ST_BIRTHTIME
#define birthtime(x) x.st_birthtime
#else
#define birthtime(x) x.st_ctime
#endif

static pid_t proxy_log_pid;
static const char *proxy_log_path;
static bool has_proxy_log_lock = false;
static pthread_mutex_t proxy_log_lock;

void proxy_log_context_init(pid_t pid, const char *path) {
    if(!has_proxy_log_lock) {
        pthread_mutexattr_t mtx_attr;
        pthread_mutexattr_init(&mtx_attr);
        pthread_mutexattr_setpshared(&mtx_attr, PTHREAD_PROCESS_PRIVATE);
        pthread_mutex_init(&proxy_log_lock, &mtx_attr);
        pthread_mutexattr_destroy(&mtx_attr);
        has_proxy_log_lock = true;
    }
    proxy_log_pid = pid;
    proxy_log_path = path;
}

void proxy_log_context_destroy() {
    if(has_proxy_log_lock) {
        pthread_mutex_destroy(&proxy_log_lock);
        has_proxy_log_lock = false;
    }
    proxy_log_pid = 0;
    proxy_log_path = NULL;
}

void proxy_log(const char *level, const char *fmt, ...) {
    time_t t;
	struct tm tm_now, tm_file;
	char filepath[100], tmstr[TMSTRBUFLEN], *buf;
	struct stat st;
	int flags, filenum, n;
	struct timespec *tspec;
    va_list args;
    size_t size;
    mode_t mode;

    if(has_proxy_log_lock) {
        pthread_mutex_lock(&proxy_log_lock);
    }

    flags = 0;
    t = time(NULL);
    tm_now = *localtime(&t);
    strftime(tmstr, TMSTRBUFLEN, "%a", &tm_now);
    sprintf(filepath, "%s/%s-%s.log", proxy_log_path, proxy_name, tmstr);

    mode = 0;
    if(stat(filepath, &st) == 0) {
        tspec = (struct timespec*)&birthtime(st);
		t = tspec->tv_sec + (tspec->tv_nsec/1000000000);
		tm_file = *localtime(&t);
		if( tm_now.tm_year == tm_file.tm_year && tm_now.tm_mon == tm_file.tm_mon && tm_now.tm_mday == tm_file.tm_mday)
			flags = O_APPEND;
		else
			flags = O_TRUNC | O_APPEND;
    } else {
        if(errno == ENOENT) {
            flags = O_CREAT | O_APPEND;
            mode = S_IRUSR | S_IWUSR;
        }
    }

    if(flags!=0) {
        filenum = open(filepath, flags | O_WRONLY, mode);
		if(filenum != -1) {
            buf = NULL;
            size = 0;

            va_start(args, fmt);
            n = vsnprintf(buf, size, fmt, args);
            va_end(args);

            size = (size_t) n + 1; //one extra byte for zero string terminator
            buf = malloc(size);

            if( buf != NULL ) {
                va_start(args, fmt);
                n = vsnprintf(buf, size, fmt, args);
                va_end(args);

                if( n > 0 ) {
                    strftime(tmstr, TMSTRBUFLEN, "%Y-%m-%d %H:%M:%S %Z", &tm_now);
                    write(filenum, tmstr, strlen(tmstr));
                    snprintf(tmstr, TMSTRBUFLEN, " [%d] ", proxy_log_pid);
                    write(filenum, tmstr, strlen(tmstr));
                    write(filenum, level, strlen(level));
                    write(filenum, ": ", 2);
                    write(filenum, buf, n);
                    write(filenum, "\n", 1);
                }

                free(buf);
            }

			close(filenum);
		}
    }

    if(has_proxy_log_lock) {
        pthread_mutex_unlock(&proxy_log_lock);
    }

    return;
}