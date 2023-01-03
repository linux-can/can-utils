#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# Copyright (c) 2023 Pengutronix,
#               Marc Kleine-Budde <kernel@pengutronix.de>

set -x
set -e

DEV=${1:-can0}
SPI=${2:-$(ethtool -i ${DEV}|sed -ne "s/bus-info: //p")}

modprobe -r mcp251xfd
modprobe mcp251xfd

sleep 2

ip link set ${DEV} down

sleep 2
rm -vf /var/log/devcoredump-*.dump

ip link set ${DEV} up type can bitrate 1000000 dbitrate 4000000 fd on restart-ms 1000 berr-reporting off listen-only off loopback on

ethtool -g ${DEV} || true
ethtool -c ${DEV} || true

cangen ${DEV} -Di -L8 -I2 -p 10 -g 200 -n 3

cat /sys/kernel/debug/regmap/${SPI}-crc/registers > data/registers-canfd.dump

ip link set ${DEV} down
sleep 2
cp -av /var/log/devcoredump-*.dump data


ip link set ${DEV} up type can bitrate 1000000 fd off restart-ms 1000 berr-reporting off listen-only off loopback on

rm -vf /var/log/devcoredump-*.dump

ethtool -g ${DEV} || true
ethtool -c ${DEV} || true

cangen ${DEV} -Di -L8 -I2 -p 10 -g 200 -n 7

cat /sys/kernel/debug/regmap/${SPI}-crc/registers > data/registers-classic-can.dump

ip link set ${DEV} down
sleep 2
cp -av /var/log/devcoredump-*.dump data
