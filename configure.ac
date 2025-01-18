# Initialize AC
AC_INIT([sawang], [1.0.0], [abdulyadi.datatrans@gmail.com])

# Set build directory
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
# AC_CONFIG_SRCDIR([gear])

#needed for shared library
AM_PROG_AR([ar])
LT_INIT
AM_PROG_LIBTOOL

AC_MSG_NOTICE([LIBDIR is "$libdir"])
AC_MSG_NOTICE([HOST is "$host"])

# Initialize Automake
AM_INIT_AUTOMAKE([subdir-objects -Wall -Werror foreign])

# glib-2.0 library dependency
PKG_CHECK_MODULES([DEPS], [glib-2.0 uuid])

# Set C compiler
AC_PROG_CC

AC_CONFIG_FILES([Makefile])
AC_OUTPUT