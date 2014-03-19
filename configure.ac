# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
AC_PREREQ([2.69])

AC_INIT([libsocketcan], [0.0.10], [bugs@pengutronix.de])
AC_CONFIG_HEADERS([include/libsocketcan_config.h])
AC_CONFIG_SRCDIR([src/libsocketcan.c])
AC_CONFIG_MACRO_DIR([config/m4])
AC_CONFIG_AUX_DIR([config/autoconf])
AC_CANONICAL_BUILD
AC_CANONICAL_HOST

CFLAGS="${CFLAGS} -Wall"

#
# libtool library versioning stuff
#
# Library code modified:	    REVISION++
# Interfaces changed/added/removed: CURRENT++	REVISION=0
# Interfaces added:		    AGE++
# Interfaces removed:		    AGE=0
LT_CURRENT=4
LT_REVISION=1
LT_AGE=2
AC_SUBST(LT_CURRENT)
AC_SUBST(LT_REVISION)
AC_SUBST(LT_AGE)


#
# Checks for programs.
#
AC_PROG_CC
LT_INIT(win32-dll)

AM_INIT_AUTOMAKE([foreign no-exeext dist-bzip2])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])


#
# Checks for libraries.
#

AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([	\
	arpa/inet.h	\
	limits.h	\
	netdb.h		\
	netinet/in.h	\
	stddef.h	\
	stdlib.h	\
	string.h	\
	sys/param.h	\
	sys/socket.h	\
	sys/time.h	\
	sys/un.h	\
	unistd.h	\
	utime.h		\
	net/if.h	\
	])

#
# Checks for typedefs, structures, and compiler characteristics.
#
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_HEADER_TIME


#
# Checks for library functions.
#
AC_FUNC_MEMCMP
AC_FUNC_STAT
AC_FUNC_UTIME_NULL
AC_CHECK_FUNCS([gethostbyaddr gethostbyname gethostname gettimeofday memset mkdir socket utime])


#
# Debugging
#
AC_MSG_CHECKING([whether to enable debugging])
AC_ARG_ENABLE(debug,
    AS_HELP_STRING([--enable-debug], [enable debugging @<:@default=no@:>@]),
	[case "$enableval" in
	y | yes) CONFIG_DEBUG=yes ;;
        *) CONFIG_DEBUG=no ;;
    esac],
    [CONFIG_DEBUG=no])
AC_MSG_RESULT([${CONFIG_DEBUG}])
if test "${CONFIG_DEBUG}" = "yes"; then
    CFLAGS="${CFLAGS} -Werror -Wsign-compare -Wfloat-equal -Wformat-security -g -O1"
    AC_DEFINE(DEBUG, 1, [debugging])
else
    CFLAGS="${CFLAGS} -O2"
fi


AC_CONFIG_FILES([
	GNUmakefile
	config/libsocketcan.pc
	config/GNUmakefile
	include/GNUmakefile
	src/GNUmakefile
	])
AC_OUTPUT

