#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <pthread.h>

#include "log.h"

#ifdef HAVE_ST_BIRTHTIME
#define birthtime(x) x.st_birthtime
#else
#define birthtime(x) x.st_ctime
#endif

#define TMSTRBUFLEN 35

void proxy_log(const LogContext *ctx, const char *level, const char *fmt, ...) {
    time_t t;
	struct tm tm_now, tm_file;
	char filepath[100], tmstr[TMSTRBUFLEN], *buf;
	struct stat st;
	int flags, filenum, n;
	struct timespec *tspec;
    va_list args;
    size_t size;
    mode_t mode;

    if( ctx->mtx != NULL )
        pthread_mutex_lock(ctx->mtx);

    flags = 0;
    t = time(NULL);
    tm_now = *localtime(&t);
    strftime(tmstr, TMSTRBUFLEN, "%a", &tm_now);
    sprintf(filepath, "%s/%s-%s.log", ctx->path, ctx->proxy_name, tmstr);

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
                    snprintf(tmstr, TMSTRBUFLEN, " [%d] ", ctx->pid);
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

    if( ctx->mtx != NULL )
        pthread_mutex_unlock(ctx->mtx);

    return;
}
