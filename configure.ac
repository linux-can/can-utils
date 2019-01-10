# -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.
AC_PREREQ([2.59])

AC_INIT([can-utils],[trunk],[linux-can@vger.kernel.org])
AC_CONFIG_HEADERS([include/libsocketcan_config.h])
AC_CONFIG_SRCDIR([lib.c])
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
#
# Adjust CMakeLists.txt, too.
#
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
AC_PROG_INSTALL
LT_INIT(win32-dll)

AM_INIT_AUTOMAKE([foreign no-exeext dist-bzip2])
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

#
# Checks for header files.
#
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_HEADER_TIME
AC_CHECK_HEADER_STDBOOL
AC_CHECK_HEADERS([ \
	fcntl.h \
	inttypes.h \
	limits.h \
	locale.h \
	locale.h \
	netdb.h \
	netinet/in.h \
	stddef.h \
	stdint.h \
	stdlib.h \
	string.h \
	syslog.h \
	termios.h \
	unistd.h \
	utime.h \
	\
	arpa/inet.h \
	netinet/in.h \
	\
	sys/ioctl.h \
	sys/param.h \
	sys/socket.h \
	sys/time.h \
	sys/un.h \
])


#
# Checks for typedefs, structures, and compiler characteristics.
#
AC_C_CONST
AC_C_INLINE
AC_SYS_LARGEFILE
AC_TYPE_INT8_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T


#
# Checks for library functions.
#
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_MKTIME
AC_FUNC_STAT
AC_FUNC_STRTOD
AC_FUNC_UTIME_NULL
AC_CHECK_FUNCS([ \
	alarm \
	gethostbyaddr \
	gethostbyname \
	gethostname \
	gettimeofday \
	localtime_r \
	memset \
	memset \
	mkdir \
	select \
	setlocale \
	socket \
	strchr \
	strerror \
	strstr \
	strtol \
	strtoul \
	strtoull \
	utime \
])

# glibc versions before 2.17 needs to link with -lrt for clock_nanosleep
AC_SEARCH_LIBS([clock_nanosleep], [rt])

AC_CHECK_DECL(SO_RXQ_OVFL,,
    [AC_DEFINE([SO_RXQ_OVFL], [40], [SO_RXQ_OVFL])]
)
AC_CHECK_DECL(PF_CAN,,
    [AC_DEFINE([PF_CAN], [29], [PF_CAN])]
)
AC_CHECK_DECL(AF_CAN,,
    [AC_DEFINE([AF_CAN], [PF_CAN], [AF_CAN])]
)
AC_CHECK_DECL(N_SLCAN,,
    [AC_DEFINE([N_SLCAN], [17], [N_SLCAN])]
)
AC_DEFINE(_GNU_SOURCE, [], [Define _GNU_SOURCE])


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


#
# Error logging
#
AC_MSG_CHECKING([whether to enable error logging])
AC_ARG_ENABLE(error-log,
    AS_HELP_STRING([--enable-error-log], [enable error log @<:@default=yes@:>@]),
	[case "$enableval" in
	y | yes) CONFIG_ERROR_LOG=yes ;;
        *) CONFIG_ERROR_LOG=no ;;
    esac],
    [CONFIG_ERROR_LOG=yes])
AC_MSG_RESULT([${CONFIG_ERROR_LOG}])
if test "${CONFIG_ERROR_LOG}" = "no"; then
    AC_DEFINE(DISABLE_ERROR_LOG, 1, [disable error logging])
fi


AC_CONFIG_FILES([
	GNUmakefile
	config/libsocketcan.pc
	])
AC_OUTPUT
