/*
 *  $Id$
 */

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
 * Send feedback to <socketcan-users@lists.berlios.de>
 *
 */

int parse_canframe(char *cs, struct can_frame *cf);
/*
 * Transfers a valid ASCII string decribing a CAN frame into struct can_frame.
 *
 * <can_id>#{R|data}
 *
 * can_id can have 3 (standard frame format) or 8 (extended frame format)
 *        hexadecimal chars
 *
 * data has 0 to 8 hex-values that can (optionally) be seperated by '.'
 *
 * Examples:
 *
 * 123# -> standard CAN-Id = 0x123, dlc = 0
 * 12345678# -> exended CAN-Id = 0x12345678, dlc = 0
 * 123#R -> standard CAN-Id = 0x123, dlc = 0, RTR-frame
 * 7A1#r -> standard CAN-Id = 0x7A1, dlc = 0, RTR-frame
 *
 * 123#00 -> standard CAN-Id = 0x123, dlc = 1, data[0] = 0x00
 * 123#1122334455667788 -> standard CAN-Id = 0x123, dlc = 8
 * 123#11.22.33.44.55.66.77.88 -> standard CAN-Id = 0x123, dlc = 8
 * 123#11.2233.44556677.88 -> standard CAN-Id = 0x123, dlc = 8
 * 32345678#112233 -> error frame with CAN_ERR_FLAG (0x2000000) set
 *
 * Simple facts on this compact ASCII CAN frame representation:
 *
 * - 3 digits: standard frame format
 * - 8 digits: extendend frame format OR error frame
 * - 8 digits with CAN_ERR_FLAG (0x2000000) set: error frame
 * - an error frame is never a RTR frame
 * 
 */

void fprint_canframe(FILE *stream , struct can_frame *cf, char *eol, int sep);
void sprint_canframe(char *buf , struct can_frame *cf, int sep);
/*
 * Creates a CAN frame hexadecimal output in compact format.
 * The CAN data[] is seperated by '.' when sep != 0.
 *
 * 12345678#112233 -> exended CAN-Id = 0x12345678, dlc = 3, data, sep = 0
 * 12345678#R -> exended CAN-Id = 0x12345678, RTR
 * 123#11.22.33.44.55.66.77.88 -> standard CAN-Id = 0x123, dlc = 8, sep = 1
 * 32345678#112233 -> error frame with CAN_ERR_FLAG (0x2000000) set
 *
 * Examples:
 *
 * fprint_canframe(stdout, &frame, "\n", 0); // with eol to STDOUT
 * fprint_canframe(stderr, &frame, NULL, 0); // no eol to STDERR
 *
 */

void fprint_long_canframe(FILE *stream , struct can_frame *cf, char *eol, int ascii);
void sprint_long_canframe(char *buf , struct can_frame *cf, int ascii);
/*
 * Creates a CAN frame hexadecimal output in user readable format.
 *
 * 12345678  [3] 11 22 33 -> exended CAN-Id = 0x12345678, dlc = 3, data
 * 12345678  [0] remote request -> exended CAN-Id = 0x12345678, RTR
 * 14B0DC51  [8] 4A 94 E8 2A EC 58 55 62   'J..*.XUb' -> (with ASCII output)
 * 20001111  [7] C6 23 7B 32 69 98 3C      ERRORFRAME -> (CAN_ERR_FLAG set)
 *
 * Examples:
 *
 * fprint_long_canframe(stdout, &frame, "\n", 0); // with eol to STDOUT
 * fprint_long_canframe(stderr, &frame, NULL, 0); // no eol to STDERR
 *
 */
