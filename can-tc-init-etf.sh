#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2022 Pengutronix, Marc Kleine-Budde <kernel@pengutronix.de>
#
# This script requires a kernel compiled with the following options:
#
# CONFIG_NET_SCH_PRIO
# CONFIG_NET_SCH_ETF
# CONFIG_NET_CLS_BASIC
# CONFIG_NET_CLS_FW
# CONFIG_NET_EMATCH
# CONFIG_NET_EMATCH_CANID
#

set -e

IFACE=${1:-can0}
MARK=${2:-1}

clear() {
    tc -batch - <<EOF

qdisc replace dev ${IFACE} root pfifo_fast

EOF
}

show() {
    tc -batch - <<EOF

qdisc show dev ${IFACE}
filter show dev ${IFACE}

EOF
}

prio_etf_mark() {
    tc -batch - <<EOF

qdisc replace dev ${IFACE} parent root handle 100 prio \
	bands 3

qdisc replace dev ${IFACE} handle 1001 parent 100:1 etf clockid CLOCK_TAI \
	delta 200000

qdisc replace dev ${IFACE} handle 1002 parent 100:3 pfifo_fast

filter add dev ${IFACE} parent 100: prio 1 \
	handle ${MARK} fw flowid 100:1

filter add dev ${IFACE} parent 100: prio 2 \
	basic match canid (sff 0x0:0x0 eff 0x0:0x0) flowid 100:2

EOF
}

clear
prio_etf_mark
show
