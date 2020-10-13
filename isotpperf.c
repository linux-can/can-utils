/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * isotpperf.c - ISO15765-2 protocol performance visualisation
 *
 * Copyright (c) 2014 Volkswagen Group Electronic Research
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

#include <libgen.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/sockios.h>

#define NO_CAN_ID 0xFFFFFFFFU
#define PERCENTRES 2 /* resolution in percent for bargraph */
#define NUMBAR (100/PERCENTRES) /* number of bargraph elements */

void print_usage(char *prg)
{
	fprintf(stderr, "%s - ISO15765-2 protocol performance visualisation.\n", prg);
	fprintf(stderr, "\nUsage: %s [options] <CAN interface>\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -s <can_id>  (source can_id. Use 8 digits for extended IDs)\n");
	fprintf(stderr, "         -d <can_id>  (destination can_id. Use 8 digits for extended IDs)\n");
	fprintf(stderr, "         -x <addr>    (extended addressing mode)\n");
	fprintf(stderr, "         -X <addr>    (extended addressing mode (rx addr))\n");
	fprintf(stderr, "\nCAN IDs and addresses are given and expected in hexadecimal values.\n");
	fprintf(stderr, "\n");
}

/* substitute math.h function log10(value)+1 */
unsigned int getdigits(unsigned int value)
{
	int  digits = 1;

	while (value > 9) {
		digits++;
		value /= 10;
	}
	return digits;
}

int main(int argc, char **argv)
{
	fd_set rdfs;
	int s;
	int running = 1;
	struct sockaddr_can addr;
	struct can_filter rfilter[2];
	struct canfd_frame frame;
	int canfd_on = 1;
	int nbytes, i, ret;
	canid_t src = NO_CAN_ID;
	canid_t dst = NO_CAN_ID;
	int ext = 0;
	int extaddr = 0;
	int rx_ext = 0;
	int rx_extaddr = 0;
	int datidx = 0;
	unsigned char bs = 0;
	unsigned char stmin = 0;
	unsigned char brs = 0;
	unsigned char ll_dl = 0;
	unsigned long fflen = 0;
	unsigned fflen_digits = 0;
	unsigned long rcvlen = 0;
	unsigned long percent = 0;
	struct timeval start_tv, end_tv, diff_tv, timeo;
	unsigned int n_pci;
	unsigned int sn, last_sn = 0;
	int opt;

	while ((opt = getopt(argc, argv, "s:d:x:X:?")) != -1) {
		switch (opt) {
		case 's':
			src = strtoul(optarg, (char **)NULL, 16);
			if (strlen(optarg) > 7)
				src |= CAN_EFF_FLAG;
			break;

		case 'd':
			dst = strtoul(optarg, (char **)NULL, 16);
			if (strlen(optarg) > 7)
				dst |= CAN_EFF_FLAG;
			break;

		case 'x':
			ext = 1;
			extaddr = strtoul(optarg, (char **)NULL, 16) & 0xFF;
			break;

		case 'X':
			rx_ext = 1;
			rx_extaddr = strtoul(optarg, (char **)NULL, 16) & 0xFF;
			break;

		case '?':
			print_usage(basename(argv[0]));
			exit(0);
			break;

		default:
			fprintf(stderr, "Unknown option %c\n", opt);
			print_usage(basename(argv[0]));
			exit(1);
			break;
		}
	}

	if ((argc - optind) != 1 || src == NO_CAN_ID || dst == NO_CAN_ID) {
		print_usage(basename(argv[0]));
		exit(0);
	}

	if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		perror("socket");
		return 1;
	}

	/* try to switch the socket into CAN FD mode */
	setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canfd_on, sizeof(canfd_on));

	/* set single CAN ID raw filters for src and dst frames */
	if (src & CAN_EFF_FLAG) {
		rfilter[0].can_id   = src & (CAN_EFF_MASK | CAN_EFF_FLAG);
		rfilter[0].can_mask = (CAN_EFF_MASK|CAN_EFF_FLAG|CAN_RTR_FLAG);
	} else {
		rfilter[0].can_id   = src & CAN_SFF_MASK;
		rfilter[0].can_mask = (CAN_SFF_MASK|CAN_EFF_FLAG|CAN_RTR_FLAG);
	}

	if (dst & CAN_EFF_FLAG) {
		rfilter[1].can_id   = dst & (CAN_EFF_MASK | CAN_EFF_FLAG);
		rfilter[1].can_mask = (CAN_EFF_MASK|CAN_EFF_FLAG|CAN_RTR_FLAG);
	} else {
		rfilter[1].can_id   = dst & CAN_SFF_MASK;
		rfilter[1].can_mask = (CAN_SFF_MASK|CAN_EFF_FLAG|CAN_RTR_FLAG);
	}

	setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, &rfilter, sizeof(rfilter));

	addr.can_family = AF_CAN;
	addr.can_ifindex = if_nametoindex(argv[optind]);

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	while (running) {

		FD_ZERO(&rdfs);
		FD_SET(s, &rdfs);

		/* timeout for ISO TP transmissions */
		timeo.tv_sec  = 1;
		timeo.tv_usec = 0;

		if ((ret = select(s+1, &rdfs, NULL, NULL, &timeo)) < 0) {
			running = 0;
			continue;
		}

		/* detected timeout of already started transmission */
		if (rcvlen && !(FD_ISSET(s, &rdfs))) {
			printf("\r%-*s",78, " (transmission timed out)");
			fflush(stdout);
			fflen = rcvlen = 0;
			continue;
		}

		nbytes = read(s, &frame, sizeof(frame));
		if (nbytes < 0) {
			perror("read");
			ret = nbytes;
			running = 0;
			continue;
		}
		if (nbytes != CAN_MTU && nbytes != CANFD_MTU) {
			fprintf(stderr, "read: incomplete CAN frame %zu %d\n", sizeof(frame), nbytes);
			ret = nbytes;
			running = 0;
			continue;
		}

		if (rcvlen) {
			/* make sure to process only the detected PDU CAN frame type */
			if (canfd_on && (nbytes != CANFD_MTU))
				continue;
			if (!canfd_on && (nbytes != CAN_MTU))
				continue;
		}

			/* check extended address if provided */
			if (ext && extaddr != frame.data[0])
				continue;

			/* only get flow control information from dst CAN ID */
			if (frame.can_id == dst) {
				/* check extended address if provided */
				if (rx_ext && frame.data[0] != rx_extaddr)
					continue;

				n_pci = frame.data[rx_ext];
				/* check flow control PCI only */
				if ((n_pci & 0xF0) != 0x30)
					continue;

				bs = frame.data[rx_ext + 1];
				stmin = frame.data[rx_ext + 2];
			}

			/* data content starts and index datidx */
			datidx = 0;

			n_pci = frame.data[ext];
			switch (n_pci & 0xF0) {

			case 0x00:
				/* SF */
				if (n_pci & 0xF) {
					fflen = rcvlen = n_pci & 0xF;
					datidx = ext+1;
				} else {
					fflen = rcvlen = frame.data[ext + 1];
					datidx = ext+2;
				}

				/* ignore incorrect SF PDUs */
				if (frame.len < rcvlen + datidx)
					fflen = rcvlen = 0;

				/* get number of digits for printing */
				fflen_digits = getdigits(fflen);

				/* get CAN FD bitrate & LL_DL setting information */
				brs = frame.flags & CANFD_BRS;
				ll_dl = frame.len;
				if (ll_dl < 8)
					ll_dl = 8;

				ioctl(s, SIOCGSTAMP, &start_tv);

				/* determine CAN frame mode for this PDU */
				if (nbytes == CAN_MTU)
					canfd_on = 0;
				else
					canfd_on = 1;

				break;

			case 0x10:
				/* FF */
				fflen = ((n_pci & 0x0F)<<8) + frame.data[ext+1];
				if (fflen)
					datidx = ext+2;
				else {
					fflen = (frame.data[ext+2]<<24) +
						(frame.data[ext+3]<<16) +
						(frame.data[ext+4]<<8) +
						frame.data[ext+5];
					datidx = ext+6;
				}

				/* to increase the time resolution we multiply fflen with 1000 later */
				if (fflen >= (UINT32_MAX / 1000)) {
					printf("fflen %lu is more than ~4.2 MB - ignoring PDU\n", fflen);
					fflush(stdout);
					fflen = rcvlen = 0;
					continue;
				}
				rcvlen = frame.len - datidx;
				last_sn = 0;

				/* get number of digits for printing */
				fflen_digits = getdigits(fflen);

				/* get CAN FD bitrate & LL_DL setting information */
				brs = frame.flags & CANFD_BRS;
				ll_dl = frame.len;

				ioctl(s, SIOCGSTAMP, &start_tv);

				/* determine CAN frame mode for this PDU */
				if (nbytes == CAN_MTU)
					canfd_on = 0;
				else
					canfd_on = 1;

				break;

			case 0x20:
				/* CF */
				if (rcvlen) {
					sn = n_pci & 0x0F;
					if (sn == ((last_sn + 1) & 0xF)) {
						last_sn = sn;
						datidx = ext+1;
						rcvlen += frame.len - datidx;
					}
				}
				break;

			default:
				break;
			}

			/* PDU reception in process */
			if (rcvlen) {
				if (rcvlen > fflen)
					rcvlen = fflen;

				percent = (rcvlen * 100 / fflen);
				printf("\r %3lu%% ", percent);

				printf("|");

				if (percent > 100)
					percent = 100;

				for (i=0; i < NUMBAR; i++){
					if (i < (int)(percent/PERCENTRES))
						printf("X");
					else
						printf(".");
				}
				printf("| %*lu/%lu ", fflen_digits, rcvlen, fflen);
			}

			/* PDU complete */
			if (rcvlen && rcvlen >= fflen) {

				printf("\r%s %02d%c (BS:%2hhu # ", canfd_on?"CAN-FD":"CAN2.0", ll_dl, brs?'*':' ', bs);
				if (stmin < 0x80)
					printf("STmin:%3hhu msec)", stmin);
				else if (stmin > 0xF0 && stmin < 0xFA)
					printf("STmin:%3u usec)", (stmin & 0xF) * 100);
				else
					printf("STmin: invalid   )");

				printf(" : %lu byte in ", fflen);

				/* calculate time */
				ioctl(s, SIOCGSTAMP, &end_tv);
				diff_tv.tv_sec  = end_tv.tv_sec  - start_tv.tv_sec;
				diff_tv.tv_usec = end_tv.tv_usec - start_tv.tv_usec;
				if (diff_tv.tv_usec < 0)
					diff_tv.tv_sec--, diff_tv.tv_usec += 1000000;
				if (diff_tv.tv_sec < 0)
					diff_tv.tv_sec = diff_tv.tv_usec = 0;

				/* check devisor to be not zero */
				if (diff_tv.tv_sec * 1000 + diff_tv.tv_usec / 1000){
					printf("%lu.%06lus ", diff_tv.tv_sec, diff_tv.tv_usec);
					printf("=> %lu byte/s", (fflen * 1000) /
					       (diff_tv.tv_sec * 1000 + diff_tv.tv_usec / 1000));
				} else
					printf("(no time available)     ");

				printf("\n");
				/* wait for next PDU */
				fflen = rcvlen = 0;
			}
			fflush(stdout);
	}

	close(s);

	return ret;
}
