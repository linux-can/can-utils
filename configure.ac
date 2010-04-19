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


#
# check for SOCKETCAN specific defines and headers
#
AC_ARG_WITH(linux-headers,
    AS_HELP_STRING([--with-linux-headers=/path/to/kernel/include], [path to linux kernel headers]),
    [case "${withval}" in
	(""|y|ye|yes|n|no)
	    AC_MSG_ERROR([You must supply a PATH as argument to --with-linux-headers]) ;;
	(*)
	    LINUX_HEADERS_DIR="$(cd "${withval}" && pwd)"
	    if test $? -ne 0; then
	        AC_MSG_ERROR(["${withval}" seems to be an invalid path!])
	    fi ;;
    esac]
)

maybe_linux_headers_dir="../kernel/2.6/include"
if test -z "${LINUX_HEADERS_DIR}" -a -e "${maybe_linux_headers_dir}"; then
    LINUX_HEADERS_DIR="$(cd "${maybe_linux_headers_dir}" && pwd)"
fi
if test -n "${LINUX_HEADERS_DIR}"; then
    linux_CFLAGS="-I${LINUX_HEADERS_DIR}"
fi
CPPFLAGS="${CPPFLAGS} ${linux_CFLAGS}"
AC_SUBST(linux_CFLAGS)


AC_CHECK_HEADERS([ \
	linux/can/gw.h \
	linux/can/isotp.h \
	],[],[],
[
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
])

AM_CONDITIONAL(CONFIG_GW, [test "${ac_cv_header_linux_can_gw_h}" = "yes"])
AM_CONDITIONAL(CONFIG_ISOTP, [test "${ac_cv_header_linux_can_isotp_h}" = "yes"])


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
