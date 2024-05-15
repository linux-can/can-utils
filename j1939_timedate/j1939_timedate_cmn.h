// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2024 Oleksij Rempel <linux@rempel-privat.de>

#ifndef _J1939_TIMEDATE_H_
#define _J1939_TIMEDATE_H_

#include <stdint.h>
#include <endian.h>
#include <stdbool.h>
#include <sys/epoll.h>

#include <linux/can.h>
#include <linux/kernel.h>
#include "../libj1939.h"
#include "../lib.h"

/* SAE J1939-71:2002 - 5.3 pgn54528 - Time/Date Adjust - TDA - */
#define J1939_PGN_TDA				0x0d500 /* 54528 */
/* SAE J1939-71:2002 - 5.3 pgn65254 - Time/Date - TD - */
#define J1939_PGN_TD				0x0fee6 /* 65254 */

#define J1939_PGN_REQUEST_PGN			0x0ea00 /* 59904 */

/* ISO 11783-3:2018 - 5.4.5 Acknowledgment */
#define ISOBUS_PGN_ACK				0x0e800 /* 59392 */

#define J1939_TIMEDATE_PRIO_DEFAULT		6

#define J1939_TIMEDATE_MAX_TRANSFER_LENGH	8

struct j1939_timedate_stats {
	int err;
	uint32_t tskey_sch;
	uint32_t tskey_ack;
	uint32_t send;
};

struct j1939_timedate_msg {
	uint8_t buf[J1939_TIMEDATE_MAX_TRANSFER_LENGH];
	size_t buf_size;
	ssize_t len; /* length of received message */
	struct sockaddr_can peername;
	socklen_t peer_addr_len;
	int sock;
};

struct j1939_timedate_err_msg {
	struct sock_extended_err *serr;
	struct scm_timestamping *tss;
	struct j1939_timedate_stats *stats;
};

/*
 * struct time_date_packet - Represents the PGN 65254 Time/Date packet
 *
 * @seconds: Seconds since the last minute (0-59) with a scaling factor,
 *           meaning each increment represents 0.25 seconds.
 * @minutes: Minutes since the last hour (0-59) with no scaling.
 * @hours: Hours since midnight (0-23) with no scaling.
 * @month: Current month (1-12) with no scaling.
 * @day: Day of the month with a scaling factor, each increment represents 0.25
 *       day.
 * @year: Year offset since 1985, each increment represents one year.
 * @local_minute_offset: Offset in minutes from UTC, can range from -125 to 125
 *                       minutes.
 * @local_hour_offset: Offset in hours from UTC, can range from -125 to 125
 *                     hours.
 *
 * This structure defines each component of the Time/Date as described in
 * PGN 65254, using each byte to represent different components of the standard
 * UTC time and optionally adjusted local time based on offsets.
 */
struct j1939_time_date_packet {
	uint8_t seconds;
	uint8_t minutes;
	uint8_t hours;
	uint8_t month;
	uint8_t day;
	uint8_t year;
	int8_t local_minute_offset;
	int8_t local_hour_offset;
};

#endif /* !_J1939_TIMEDATE_H_ */
