ACLOCAL_AMFLAGS = -I m4 --install
EXTRA_DIST = m4/NOTES
AM_DISTCHECK_CONFIGURE_FLAGS = CPPFLAGS=-I../../cJSON

lib_LTLIBRARIES = libsawang.la
libsawang_la_SOURCES =
include gear/Makefile.am

libsawang_la_LDFLAGS = -version-info 1:0:0

AM_CFLAGS = $(DEPS_CFLAGS) -Wall -Werror
LIBS = $(DEPS_LIBS) -lpthread -lrt

pkginclude_HEADERS = /usr/local/include/cjson/cJSON.h \
    gear/callback.h \
	gear/confvar.h \
    gear/define.h \
    gear/log.h \
    gear/proxyuuid.h \
	gear/work.h
	
    