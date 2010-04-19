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

banner "autoreconf"

autoreconf --force --install --symlink -Wall || exit $?

banner "Finished"
