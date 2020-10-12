/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * isotpsniffer.c - dump ISO15765-2 datagrams using PF_CAN isotp protocol 
 *
 * Copyright (c) 2008 Volkswagen Group Electronic Research
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

#include <ctype.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "terminal.h"
#include <linux/can.h>
#include <linux/can/isotp.h>
#include <linux/sockios.h>

#define NO_CAN_ID 0xFFFFFFFFU

#define FORMAT_HEX 1
#define FORMAT_ASCII 2
#define FORMAT_DEFAULT (FORMAT_ASCII | FORMAT_HEX)

void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s [options] <CAN interface>\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -s <can_id>  (source can_id. Use 8 digits for extended IDs)\n");
	fprintf(stderr, "         -d <can_id>  (destination can_id. Use 8 digits for extended IDs)\n");
	fprintf(stderr, "         -x <addr>    (extended addressing mode)\n");
	fprintf(stderr, "         -X <addr>    (extended addressing mode - rx addr)\n");
	fprintf(stderr, "         -c           (color mode)\n");
	fprintf(stderr, "         -t <type>    (timestamp: (a)bsolute/(d)elta/(z)ero/(A)bsolute w date)\n");
	fprintf(stderr, "         -f <format>  (1 = HEX, 2 = ASCII, 3 = HEX & ASCII - default: %d)\n", FORMAT_DEFAULT);
	fprintf(stderr, "         -L <mtu>:<tx_dl>:<tx_flags>  (link layer options for CAN FD)\n");
	fprintf(stderr, "         -h <len>    (head: print only first <len> bytes)\n");
	fprintf(stderr, "\nCAN IDs and addresses are given and expected in hexadecimal values.\n");
	fprintf(stderr, "\n");
}

void printbuf(unsigned char *buffer, int nbytes, int color, int timestamp,
	      int format, struct timeval *tv, struct timeval *last_tv,
	      canid_t src, int socket, char *candevice, int head)
{
	int i;

	if (color == 1)
		printf("%s", FGRED);

	if (color == 2)
		printf("%s", FGBLUE);

	if (timestamp) {
		ioctl(socket, SIOCGSTAMP, tv);

		switch (timestamp) {

		case 'a': /* absolute with timestamp */
			printf("(%lu.%06lu) ", tv->tv_sec, tv->tv_usec);
			break;

		case 'A': /* absolute with date */
		{
			struct tm tm;
			char timestring[25];

			tm = *localtime(&tv->tv_sec);
			strftime(timestring, 24, "%Y-%m-%d %H:%M:%S", &tm);
			printf("(%s.%06lu) ", timestring, tv->tv_usec);
		}
		break;

		case 'd': /* delta */
		case 'z': /* starting with zero */
		{
			struct timeval diff;

			if (last_tv->tv_sec == 0)   /* first init */
				*last_tv = *tv;
			diff.tv_sec  = tv->tv_sec  - last_tv->tv_sec;
			diff.tv_usec = tv->tv_usec - last_tv->tv_usec;
			if (diff.tv_usec < 0)
				diff.tv_sec--, diff.tv_usec += 1000000;
			if (diff.tv_sec < 0)
				diff.tv_sec = diff.tv_usec = 0;
			printf("(%lu.%06lu) ", diff.tv_sec, diff.tv_usec);

			if (timestamp == 'd')
				*last_tv = *tv; /* update for delta calculation */
		}
		break;

		default: /* no timestamp output */
			break;
		}
	}

	/* the source socket gets pdu data from the destination id */
	printf(" %s  %03X  [%d]  ", candevice, src & CAN_EFF_MASK, nbytes);
	if (format & FORMAT_HEX) {
		for (i=0; i<nbytes; i++) {
			printf("%02X ", buffer[i]);
			if (head && i+1 >= head) {
				printf("... ");
				break;
			}
		}
		if (format & FORMAT_ASCII)
			printf(" - ");
	}
	if (format & FORMAT_ASCII) {
		printf("'");
		for (i=0; i<nbytes; i++) {
			if (isprint(buffer[i]))
				printf("%c", buffer[i]);
			else
				printf(".");
			if (head && i+1 >= head)
				break;
		}
		printf("'");
		if (head && i+1 >= head)
			printf(" ... ");
	}

	if (color)
		printf("%s", ATTRESET);

	printf("\n");
	fflush(stdout);
}

int main(int argc, char **argv)
{
	fd_set rdfs;
	int s = -1, t = -1;
	struct sockaddr_can addr;
	char if_name[IFNAMSIZ];
	static struct can_isotp_options opts;
	static struct can_isotp_ll_options llopts;
	int r = 0;
	int opt, quit = 0;
	int color = 0;
	int head = 0;
	int timestamp = 0;
	int format = FORMAT_DEFAULT;
	canid_t src = NO_CAN_ID;
	canid_t dst = NO_CAN_ID;
	extern int optind, opterr, optopt;
	static struct timeval tv, last_tv;

	unsigned char buffer[4096];
	int nbytes;

	while ((opt = getopt(argc, argv, "s:d:x:X:h:ct:f:L:?")) != -1) {
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
			opts.flags |= CAN_ISOTP_EXTEND_ADDR;
			opts.ext_address = strtoul(optarg, (char **)NULL, 16) & 0xFF;
			break;

		case 'X':
			opts.flags |= CAN_ISOTP_RX_EXT_ADDR;
			opts.rx_ext_address = strtoul(optarg, (char **)NULL, 16) & 0xFF;
			break;

		case 'f':
			format = (atoi(optarg) & (FORMAT_ASCII | FORMAT_HEX));
			break;

		case 'L':
			if (sscanf(optarg, "%hhu:%hhu:%hhu",
						&llopts.mtu,
						&llopts.tx_dl,
						&llopts.tx_flags) != 3) {
				printf("unknown link layer options '%s'.\n", optarg);
				print_usage(basename(argv[0]));
				exit(1);
			}
			break;

		case 'h':
			head = atoi(optarg);
			break;

		case 'c':
			color = 1;
			break;

		case 't':
			timestamp = optarg[0];
			if ((timestamp != 'a') && (timestamp != 'A') &&
			    (timestamp != 'd') && (timestamp != 'z')) {
				printf("%s: unknown timestamp mode '%c' - ignored\n",
				       basename(argv[0]), optarg[0]);
				timestamp = 0;
			}
			break;

		case '?':
			print_usage(basename(argv[0]));
			goto out;

		default:
			fprintf(stderr, "Unknown option %c\n", opt);
			print_usage(basename(argv[0]));
			goto out;
		}
	}

	if ((argc - optind) != 1 || src == NO_CAN_ID || dst == NO_CAN_ID) {
		print_usage(basename(argv[0]));
		r = 1;
		goto out;
	}
  
	if ((opts.flags & CAN_ISOTP_RX_EXT_ADDR) && (!(opts.flags & CAN_ISOTP_EXTEND_ADDR))) {
		print_usage(basename(argv[0]));
		r = 1;
		goto out;
	}

	if ((s = socket(PF_CAN, SOCK_DGRAM, CAN_ISOTP)) < 0) {
		perror("socket");
		r = 1;
		goto out;
	}

	if ((t = socket(PF_CAN, SOCK_DGRAM, CAN_ISOTP)) < 0) {
		perror("socket");
		r = 1;
		goto out;
	}

	opts.flags |= CAN_ISOTP_LISTEN_MODE;

	strncpy(if_name, argv[optind], IFNAMSIZ - 1);
	if_name[IFNAMSIZ - 1] = '\0';

	addr.can_family = AF_CAN;
	addr.can_ifindex = if_nametoindex(if_name);

	setsockopt(s, SOL_CAN_ISOTP, CAN_ISOTP_OPTS, &opts, sizeof(opts));

	addr.can_addr.tp.tx_id = src;
	addr.can_addr.tp.rx_id = dst;

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		r = 1;
		goto out;
	}

	if (opts.flags & CAN_ISOTP_RX_EXT_ADDR) {
		/* flip extended address info due to separate rx ext addr */
		__u8 tmpext;

		tmpext = opts.ext_address;
		opts.ext_address = opts.rx_ext_address;
		opts.rx_ext_address = tmpext;
	}

	if ((setsockopt(t, SOL_CAN_ISOTP, CAN_ISOTP_OPTS, &opts, sizeof(opts))) < 0) {
		perror("setsockopt");
		r = 1;
		goto out;
	}

	if ((llopts.mtu) && (setsockopt(t, SOL_CAN_ISOTP, CAN_ISOTP_LL_OPTS, &llopts, sizeof(llopts))) < 0) {
		perror("setsockopt");
		r = 1;
		goto out;
	}

	addr.can_addr.tp.tx_id = dst;
	addr.can_addr.tp.rx_id = src;

	if (bind(t, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		r = 1;
		goto out;
	}

	while (!quit) {

		FD_ZERO(&rdfs);
		FD_SET(s, &rdfs);
		FD_SET(t, &rdfs);
		FD_SET(0, &rdfs);

		if ((nbytes = select(t+1, &rdfs, NULL, NULL, NULL)) < 0) {
			perror("select");
			continue;
		}

		if (FD_ISSET(0, &rdfs)) {
			getchar();
			quit = 1;
			printf("quit due to keyboard input.\n");
		}

		if (FD_ISSET(s, &rdfs)) {
			nbytes = read(s, buffer, 4096);
			if (nbytes < 0) {
				perror("read socket s");
				r = 1;
				goto out;
			}
			if (nbytes > 4095) {
				r = 1;
				goto out;
			}
			printbuf(buffer, nbytes, color?2:0, timestamp, format,
				 &tv, &last_tv, dst, s, if_name, head);
		}

		if (FD_ISSET(t, &rdfs)) {
			nbytes = read(t, buffer, 4096);
			if (nbytes < 0) {
				perror("read socket t");
				r = 1;
				goto out;
			}
			if (nbytes > 4095) {
				r = 1;
				goto out;
			}
			printbuf(buffer, nbytes, color?1:0, timestamp, format,
				 &tv, &last_tv, src, t, if_name, head);
		}
	}

out:
	if (s != -1)
		close(s);
	if (t != -1)
		close(t);

	return r;
}
