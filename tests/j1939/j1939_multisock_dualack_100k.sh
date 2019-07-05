#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2019 Oleksij Rempel <entwicklung@pengutronix.de>

set -e

CAN0=${1:-can0}
CAN1=${2:-can1}

dd if=/dev/urandom of=/tmp/test_100k bs=100K count=1

dmesg -c > /dev/null

jcat ${CAN0}:0x90 -r > /dev/null &
jcat ${CAN1}:0x90 -r > /dev/null &

for i in `seq 1 100`;
do
	echo start round $i
	jcat -i /tmp/test_100k -R 100 ${CAN0}:0x80 :0x90,0x12300 &
done

killall jcat

if  dmesg | grep -i backtra; then
	echo test failed;
	exit 1;
fi
