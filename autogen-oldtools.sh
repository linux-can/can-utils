#!/bin/bash

#
# usage:
#
# banner <target name>
#
banner() {

	echo
	TG=`echo $1 | sed -e "s,/.*/,,g"`
	LINE=`echo $TG |sed -e "s/./-/g"`
	echo $LINE
	echo $TG
	echo $LINE
	echo
}


ACLOCAL=${ACLOCAL:=aclocal}
AUTOHEADER=${AUTOHEADER:=autoheader}
AUTOMAKE=${AUTOMAKE:=automake}
AUTOCONF=${AUTOCONF:=autoconf}

banner "running libtoolize"
libtoolize --force
[ $? != 0 ] && exit

banner "running aclocal"
$ACLOCAL -I config/m4
[ $? != 0 ] && exit

banner "running autoheader"
$AUTOHEADER
[ $? != 0 ] && exit

banner "running automake"
$AUTOMAKE --gnu --add-missing -Wall
[ $? != 0 ] && exit

banner "running autoconf"
$AUTOCONF -Wall
[ $? != 0 ] && exit

banner "Finished"
