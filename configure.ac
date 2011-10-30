# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
AC_PREREQ(2.59)

AC_INIT([can-utils], [trunk], [socketcan-core@lists.berlios.de])
AC_CONFIG_SRCDIR([lib.c])
AC_CONFIG_MACRO_DIR([config/m4])
AC_CONFIG_AUX_DIR([config/autoconf])
AC_CANONICAL_BUILD
AC_CANONICAL_HOST

#AM_MAINTAINER_MODE

CFLAGS="${CFLAGS} -Wall"

#
# Checks for programs.
#
AC_PROG_CC
#AM_MISSING_PROG(PERL, perl, $missing_dir)
# libtool, old:
AC_LIBTOOL_WIN32_DLL
#AC_LIBTOOL_TAGS([])
AC_PROG_LIBTOOL
# libtool, new:
# LT_INIT(win32-dll)

AM_INIT_AUTOMAKE([foreign no-exeext dist-bzip2])


#
# Checks for header files.
#
AC_CHECK_HEADERS([ \
	fcntl.h \
	limits.h \
	locale.h \
	stdint.h \
	stdlib.h \
	string.h \
	syslog.h \
	termios.h \
	unistd.h \
	\
	netinet/in.h \
	\
	sys/ioctl.h \
	sys/socket.h \
	sys/time.h \
])


#
# Checks for typedefs, structures, and compiler characteristics.
#
AC_C_INLINE
AC_TYPE_PID_T
AC_TYPE_UINT64_T


#
# Checks for library functions.
#
AC_FUNC_FORK
AC_FUNC_MKTIME
AC_CHECK_FUNCS([ \
	alarm \
	gettimeofday \
	localtime_r \
	memset \
	select \
	setlocale \
	socket \
	strchr \
	strerror \
	strstr \
	strtoul \
])


AC_CHECK_DECL(SO_RXQ_OVFL,,
    [AC_DEFINE([SO_RXQ_OVFL], [40], [SO_RXQ_OVFL])]
)
AC_CHECK_DECL(PF_CAN,,
    [AC_DEFINE([PF_CAN], [29], [PF_CAN])]
)
AC_CHECK_DECL(AF_CAN,,
    [AC_DEFINE([AF_CAN], [PF_CAN], [AF_CAN])]
)


#
# Debugging
#
AC_MSG_CHECKING([whether to enable debugging])
AC_ARG_ENABLE(debug,
    AS_HELP_STRING([--enable-debug], [enable debugging [[default=no]]]),
	[case "$enableval" in
	(y | yes) CONFIG_DEBUG=yes ;;
        (*) CONFIG_DEBUG=no ;;
    esac],
    [CONFIG_DEBUG=no])
AC_MSG_RESULT([${CONFIG_DEBUG}])
if test "${CONFIG_DEBUG}" = "yes"; then
    CFLAGS="${CFLAGS} -Wsign-compare -Wfloat-equal -Wformat-security -g -O1"
    AC_DEFINE(DEBUG, 1, [debugging])
else
    CFLAGS="${CFLAGS} -O2"
fi


AC_CONFIG_FILES([
	GNUmakefile
	])
AC_OUTPUT
