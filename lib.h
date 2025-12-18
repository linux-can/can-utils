/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * lib.h - library include for command line tools
 *
 * Copyright (c) 2002-2007 Volkswagen Group Electronic Research
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * The provided data structures and external interfaces from this code
 * are not restricted to be used by modules with a GPL compatible license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * Send feedback to <linux-can@vger.kernel.org>
 *
 */

#ifndef CAN_UTILS_LIB_H
#define CAN_UTILS_LIB_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#include <linux/can.h>

#ifdef DEBUG
#define pr_debug(fmt, args...) printf(fmt, ##args)
#else
__attribute__((format (printf, 1, 2)))
static inline int pr_debug(const char* fmt, ...) {return 0;}
#endif

/* CAN CC/FD/XL frame union */
union cfu {
	struct can_frame cc;
	struct canfd_frame fd;
	struct canxl_frame xl;
};

/*
 * The buffer size for ASCII CAN frame string representations
 * covers also the 'long' CAN frame output from sprint_long_canframe()
 * including (swapped) binary represetations, timestamps, netdevice names,
 * lengths and error message details as the CAN XL data is cropped to 64
 * byte (the 'long' CAN frame output is only for display on terminals).
 */
#define AFRSZ 6300 /* 3*2048 (data) + 22 (timestamp) + 18 (netdev) + ID/HDR */

/* CAN DLC to real data length conversion helpers especially for CAN FD */

/* get data length from raw data length code (DLC) */
unsigned char can_fd_dlc2len(unsigned char dlc);

/* map the sanitized data length to an appropriate data length code */
unsigned char can_fd_len2dlc(unsigned char len);

unsigned char asc2nibble(char c);
/*
 * Returns the decimal value of a given ASCII hex character.
 *
 * While 0..9, a..f, A..F are valid ASCII hex characters.
 * On invalid characters the value 16 is returned for error handling.
 */

int hexstring2data(char *arg, unsigned char *data, int maxdlen);
/*
 * Converts a given ASCII hex string to a (binary) byte string.
 *
 * A valid ASCII hex string consists of an even number of up to 16 chars.
 * Leading zeros '00' in the ASCII hex string are interpreted.
 *
 * Examples:
 *
 * "1234"   => data[0] = 0x12, data[1] = 0x34
 * "001234" => data[0] = 0x00, data[1] = 0x12, data[2] = 0x34
 *
 * Return values:
 * 0 = success
 * 1 = error (in length or the given characters are no ASCII hex characters)
 *
 * Remark: The not written data[] elements are initialized with zero.
 *
 */

int parse_canframe(char *cs, union cfu *cu);
/*
 * Transfers a valid ASCII string describing a CAN frame into the CAN union
 * containing CAN CC/FD/XL structs.
 *
 * CAN CC frames (aka Classical CAN, CAN 2.0B)
 * - string layout <can_id>#{R{len}|data}{_len8_dlc}
 * - {data} has 0 to 8 hex-values that can (optionally) be separated by '.'
 * - {len} can take values from 0 to 8 and can be omitted if zero
 * - {_len8_dlc} can take hex values from '_9' to '_F' when len is CAN_MAX_DLEN
 * - return value on successful parsing: CAN_MTU
 *
 * CAN FD frames
 * - string layout <can_id>##<flags>{data}
 * - <flags> a single ASCII Hex value (0 .. F) which defines canfd_frame.flags
 * - {data} has 0 to 64 hex-values that can (optionally) be separated by '.'
 * - return value on successful parsing: CANFD_MTU
 *
 * CAN XL frames
 * - string layout <vcid><prio>#<flags>:<sdt>:<af>#{data}
 * - <vcid> a two ASCII Hex value (00 .. FF) which defines the VCID
 * - <prio> a three ASCII Hex value (000 .. 7FF) which defines the 11 bit PRIO
 * - <flags> a two ASCII Hex value (00 .. FF) which defines canxl_frame.flags
 * - <sdt> a two ASCII Hex value (00 .. FF) which defines canxl_frame.sdt
 * - <af> a 8 digit ASCII Hex value which defines the 32 bit canxl_frame.af
 * - {data} has 1 to 2048 hex-values that can (optionally) be separated by '.'
 * - return value on successful parsing: CANXL_MTU
 *
 * Return value on detected problems: 0
 *
 * <can_id> can have 3 (standard frame format) or 8 (extended frame format)
 * hexadecimal chars
 *
 *
 * Examples:
 *
 * 123# -> standard CAN-Id = 0x123, len = 0
 * 12345678# -> extended CAN-Id = 0x12345678, len = 0
 * 123#R -> standard CAN-Id = 0x123, len = 0, RTR-frame
 * 123#R0 -> standard CAN-Id = 0x123, len = 0, RTR-frame
 * 123#R7 -> standard CAN-Id = 0x123, len = 7, RTR-frame
 * 123#R8_9 -> standard CAN-Id = 0x123, len = 8, dlc = 9, RTR-frame
 * 7A1#r -> standard CAN-Id = 0x7A1, len = 0, RTR-frame
 *
 * 123#00 -> standard CAN-Id = 0x123, len = 1, data[0] = 0x00
 * 123#1122334455667788 -> standard CAN-Id = 0x123, len = 8
 * 123#1122334455667788_E -> standard CAN-Id = 0x123, len = 8, dlc = 14
 * 123#11.22.33.44.55.66.77.88 -> standard CAN-Id = 0x123, len = 8
 * 123#11.2233.44556677.88 -> standard CAN-Id = 0x123, len = 8
 * 32345678#112233 -> error frame with CAN_ERR_FLAG (0x2000000) set
 *
 * 123##0112233 -> CAN FD frame standard CAN-Id = 0x123, flags = 0, len = 3
 * 123##1112233 -> CAN FD frame, flags = CANFD_BRS, len = 3
 * 123##2112233 -> CAN FD frame, flags = CANFD_ESI, len = 3
 * 123##3 -> CAN FD frame, flags = (CANFD_ESI | CANFD_BRS), len = 0
 *     ^^
 *     CAN FD extension to handle the canfd_frame.flags content
 *
 * 45123#81:00:12345678#11223344.556677 -> CAN XL frame with len = 7,
 *   VCID = 0x45, PRIO = 0x123, flags = 0x81, sdt = 0x00, af = 0x12345678
 *
 * Simple facts on this compact ASCII CAN frame representation:
 *
 * - 3 digits: standard frame format
 * - 8 digits: extendend frame format OR error frame
 * - 8 digits with CAN_ERR_FLAG (0x2000000) set: error frame
 * - an error frame is never a RTR frame
 * - CAN FD frames do not have a RTR bit
 */

int snprintf_canframe(char *buf, size_t size, union cfu *cu, int sep);
/*
 * Creates a CAN frame hexadecimal output in compact format.
 * The CAN data[] is separated by '.' when sep != 0.
 *
 * A CAN XL frame is detected when CANXL_XLF is set in the struct
 * cu.canxl_frame.flags. Otherwise the type of the CAN frame (CAN CC/FD)
 * is specified by the dual-use struct cu.canfd_frame.flags element:
 * w/o  CAN FD flags (== 0) -> CAN CC frame (aka Classical CAN, CAN2.0B)
 * with CAN FD flags (!= 0) -> CAN FD frame (with CANFD_[FDF/BRS/ESI])
 *
 * 12345678#112233 -> extended CAN-Id = 0x12345678, len = 3, data, sep = 0
 * 123#1122334455667788_E -> standard CAN-Id = 0x123, len = 8, dlc = 14, data, sep = 0
 * 12345678#R -> extended CAN-Id = 0x12345678, RTR, len = 0
 * 12345678#R5 -> extended CAN-Id = 0x12345678, RTR, len = 5
 * 123#11.22.33.44.55.66.77.88 -> standard CAN-Id = 0x123, dlc = 8, sep = 1
 * 32345678#112233 -> error frame with CAN_ERR_FLAG (0x2000000) set
 * 123##0112233 -> CAN FD frame standard CAN-Id = 0x123, flags = 0, len = 3
 * 123##2112233 -> CAN FD frame, flags = CANFD_ESI, len = 3
 * 45123#81:00:12345678#11223344.556677 -> CAN XL frame with len = 7,
 *   VCID = 0x45, PRIO = 0x123, flags = 0x81, sdt = 0x00, af = 0x12345678
 *
 */

#define CANLIB_VIEW_ASCII	0x1
#define CANLIB_VIEW_BINARY	0x2
#define CANLIB_VIEW_SWAP	0x4
#define CANLIB_VIEW_ERROR	0x8
#define CANLIB_VIEW_INDENT_SFF	0x10
#define CANLIB_VIEW_LEN8_DLC	0x20

#define SWAP_DELIMITER '`'

int snprintf_long_canframe(char *buf, size_t size, union cfu *cu, int view);
/*
 * Creates a CAN frame hexadecimal output in user readable format.
 *
 * A CAN XL frame is detected when CANXL_XLF is set in the struct
 * cu.canxl_frame.flags. Otherwise the type of the CAN frame (CAN CC/FD)
 * is specified by the dual-use struct cu.canfd_frame.flags element:
 * w/o  CAN FD flags (== 0) -> CAN CC frame (aka Classical CAN, CAN2.0B)
 * with CAN FD flags (!= 0) -> CAN FD frame (with CANFD_[FDF/BRS/ESI])
 *
 * 12345678   [3]  11 22 33 -> extended CAN-Id = 0x12345678, len = 3, data
 * 12345678   [0]  remote request -> extended CAN-Id = 0x12345678, RTR
 * 14B0DC51   [8]  4A 94 E8 2A EC 58 55 62   'J..*.XUb' -> (with ASCII output)
 * 321   {B}  11 22 33 44 55 66 77 88 -> Classical CAN with raw '{DLC}' value B
 * 20001111   [7]  C6 23 7B 32 69 98 3C      ERRORFRAME -> (CAN_ERR_FLAG set)
 * 12345678  [03]  11 22 33 -> CAN FD with extended CAN-Id = 0x12345678, len = 3
 *      123 [0003] (45|81:00:12345678) 11 22 33 -> CAN XL frame with VCID 0x45
 *
 * 123   [3]  11 22 33         -> CANLIB_VIEW_INDENT_SFF == 0
 *      123   [3]  11 22 33    -> CANLIB_VIEW_INDENT_SFF == set
 *
 * There are no binary or ASCII view modes for CAN XL and the number of displayed
 * data bytes is limited to 64 to fit terminal output use-cases.
 */

int snprintf_can_error_frame(char *buf, size_t len, const struct canfd_frame *cf,
			     const char *sep);
/*
 * Creates a CAN error frame output in user readable format.
 */

/**
 * timespec_diff_ms - calculate timespec difference in milliseconds
 * @ts1: first timespec
 * @ts2: second timespec
 *
 * Return negative difference if in the past.
 */
int64_t timespec_diff_ms(struct timespec *ts1, struct timespec *ts2);

/**
 * timespec_add_ms - add milliseconds to timespec
 * @ts: timespec
 * @milliseconds: milliseconds to add
 */
void timespec_add_ms(struct timespec *ts, uint64_t milliseconds);

#endif
