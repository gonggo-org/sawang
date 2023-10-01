ACLOCAL_AMFLAGS = -I m4 --install
EXTRA_DIST = m4/NOTES
AM_DISTCHECK_CONFIGURE_FLAGS = CPPFLAGS=-I../../cJSON

lib_LTLIBRARIES = libsawang.la
libsawang_la_SOURCES =
include cJSON/Makefile.am
include gear/Makefile.am

libsawang_la_LDFLAGS = -version-info 1:0:0

AM_CFLAGS = $(DEPS_CFLAGS) -I./cJSON -Wall -Werror=implicit-function-declaration
LIBS = $(DEPS_LIBS) -lpthread -lrt

pkginclude_HEADERS = cJSON/cJSON.h \
	gear/confvar.h \
	gear/work.h \
	gear/subscribe_struct.h \
	gear/dispatcher_heartbeat_struct.h \
	gear/log.h \
	gear/define.h
