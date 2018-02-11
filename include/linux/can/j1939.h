/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * j1939.h
 *
 * Copyright (c) 2010-2011 EIA Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _UAPI_CAN_J1939_H_
#define _UAPI_CAN_J1939_H_

#include <linux/types.h>
#include <linux/socket.h>
#include <linux/can.h>

#define J1939_IDLE_ADDR	0xfe
#define J1939_NO_ADDR 0xff
#define J1939_NO_NAME 0
#define J1939_NO_PGN 0x40000

/* J1939 Parameter Group Number
 *
 * bit 0-7	: PDU Specific (PS)
 * bit 8-15	: PDU Format (PF)
 * bit 16	: Data Page (DP)
 * bit 17	: Reserved (R)
 * bit 19-31	: set to zero
 */
typedef __u32 pgn_t;

/* J1939 Priority
 *
 * bit 0-2	: Priority (P)
 * bit 3-7	: set to zero
 */
typedef __u8 priority_t;

/* J1939 NAME
 *
 * bit 0-20	: Identity Number
 * bit 21-31	: Manufacturer Code
 * bit 32-34	: ECU Instance
 * bit 35-39	: Function Instance
 * bit 40-47	: Function
 * bit 48	: Reserved
 * bit 49-55	: Vehicle System
 * bit 56-59	: Vehicle System Instance
 * bit 60-62	: Industry Group
 * bit 63	: Arbitrary Address Capable
 */
typedef __u64 name_t;

/* J1939 socket options */
#define SOL_CAN_J1939 (SOL_CAN_BASE + CAN_J1939)
enum {
	SO_J1939_FILTER = 1,	/* set filters */
	SO_J1939_PROMISC = 2,	/* set/clr promiscuous mode */
	SO_J1939_RECV_OWN = 3,
	SO_J1939_SEND_PRIO = 4,
};

enum {
	SCM_J1939_DEST_ADDR = 1,
	SCM_J1939_DEST_NAME = 2,
	SCM_J1939_PRIO = 3,
};

struct j1939_filter {
	name_t name;
	name_t name_mask;
	__u8 addr;
	__u8 addr_mask;
	pgn_t pgn;
	pgn_t pgn_mask;
};

#define J1939_FILTER_MAX 512 /* maximum number of j1939_filter set via setsockopt() */

#endif /* !_UAPI_CAN_J1939_H_ */
