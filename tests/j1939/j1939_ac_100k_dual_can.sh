#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2019 Oleksij Rempel <entwicklung@pengutronix.de>

# This test was written to reproduce following bug, where
# remote address was keeping local priv allocated. Even if no other
# local users was keeping the stack.
# To run this test DUT should have at least two CAN interfaces named can0
# and can1.
#
# =========================
# WARNING: held lock freed!
# 5.1.0-00164-g683bec30accc-dirty #410 Not tainted
# -------------------------
# jacd/10810 is freeing memory e8ced000-e8cedfff, with a lock still held there!
# ca540dc3 (&priv->lock#4){++--}, at: j1939_ac_recv+0x9c/0x1cc
# 6 locks held by jacd/10810:
#  #0: 8cae1b05 (sb_writers#6){.+.+}, at: vfs_write+0xb0/0x184
#  #1: 61f77435 (&sb->s_type->i_mutex_key#10){+.+.}, at: generic_file_write_iter+0x50/0x20c
#  #2: 724fe4e2 (rcu_read_lock){....}, at: get_mem_cgroup_from_mm+0x34/0x35c
#  #3: 724fe4e2 (rcu_read_lock){....}, at: netif_receive_skb_internal+0x78/0x3d4
#  #4: 724fe4e2 (rcu_read_lock){....}, at: can_receive+0x94/0x1d0
#  #5: ca540dc3 (&priv->lock#4){++--}, at: j1939_ac_recv+0x9c/0x1cc
#
# stack backtrace:
# CPU: 0 PID: 10810 Comm: jacd Not tainted 5.1.0-00164-g683bec30accc-dirty #410
# Hardware name: Freescale i.MX6 Quad/DualLite (Device Tree)
# Backtrace:
# [<c010feb8>] (dump_backtrace) from [<c01100e0>] (show_stack+0x20/0x24)
#  r7:c15cc630 r6:00000000 r5:60010193 r4:c15cc630
# [<c01100c0>] (show_stack) from [<c0cf3d84>] (dump_stack+0xa0/0xcc)
# [<c0cf3ce4>] (dump_stack) from [<c018ff6c>] (debug_check_no_locks_freed+0x110/0x12c)
#  r9:e8ced008 r8:e8cedfff r7:20010193 r6:e91cb180 r5:e8ced000 r4:e91cb7c8
# [<c018fe5c>] (debug_check_no_locks_freed) from [<c03087fc>] (kfree+0x2e8/0x430)
#  r8:60010113 r7:ebc950a0 r6:e8ced000 r5:c0b34cc4 r4:e8001700
# [<c0308514>] (kfree) from [<c0b34cc4>] (j1939_priv_put+0xa8/0xb0)
#  r9:e8ced008 r8:e9254200 r7:e8ced000 r6:e9872840 r5:e86f0000 r4:e8ced000
# [<c0b34c1c>] (j1939_priv_put) from [<c0b341f8>] (j1939_ecu_put+0x64/0x68)
#  r5:e8ced000 r4:e9254200
# [<c0b34194>] (j1939_ecu_put) from [<c0b3408c>] (j1939_ac_recv+0x160/0x1cc)
#  r5:00000000 r4:11223340
# [<c0b33f2c>] (j1939_ac_recv) from [<c0b34b5c>] (j1939_can_recv+0x118/0x144)
#  r10:e9bbb9fc r9:0000012b r8:e86f08c0 r7:e9872240 r6:e8ced008 r5:e8ced000
#  r4:e9872840 r3:e9bba000
# [<c0b34a44>] (j1939_can_recv) from [<c0b2bd28>] (can_rcv_filter+0xfc/0x21c)
#  r7:e9872240 r6:98eefffe r5:00000001 r4:e80311b0
# [<c0b2bc2c>] (can_rcv_filter) from [<c0b2c864>] (can_receive+0x130/0x1d0)
#  r9:0000012b r8:00000000 r7:e86f0000 r6:c15b00c0 r5:e8951040 r4:e9872240
# [<c0b2c734>] (can_receive) from [<c0b2c990>] (can_rcv+0x8c/0x94)

set -e

CAN0=${1:-can0}
CAN1=${2:-can1}

dmesg -c > /dev/null

echo "generate random data for the test"
dd if=/dev/urandom of=/tmp/test_100k bs=100K count=1

echo "start jacd and jcat on ${CAN0}"
jacd -r 100,80-120 -c /tmp/11223344.jacd 11223344 ${CAN0} &
PID_JACD0=$!
echo $PID_JACD0
sleep 2
jcat ${CAN0}:,,0x11223344 -r > /tmp/blup &
PID_JCAT0=$!
echo $PID_JCAT0

echo "start jacd and jcat on ${CAN1}"
jacd -r 100,80-120 -c /tmp/11223340.jacd 11223340 ${CAN1} &
sleep 2
jcat -i /tmp/test_100k ${CAN1}:,,0x11223340 :,,0x11223344
sleep 3

echo "kill all users on ${CAN0}"
# At this step all local user will be removed. All address cache should
# be dropped as well.
kill $PID_JACD0
kill $PID_JCAT0
echo "kill all users on ${CAN1}"
# If stack is buggy, kernel will explode somewhere here.
killall jacd

if dmesg | grep -i backtra; then
	echo test failed;
	exit 1;
fi
