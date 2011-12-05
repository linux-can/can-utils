/*
 *  $Id$
 */

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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <time.h>

#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <linux/can.h>
#include <linux/can/isotp.h>
#include "terminal.h"

#define NO_CAN_ID 0xFFFFFFFFU

#define FORMAT_HEX 1
#define FORMAT_ASCII 2
#define FORMAT_DEFAULT (FORMAT_ASCII | FORMAT_HEX)

void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s [options] <CAN interface>\n", prg);
	fprintf(stderr, "Options: -s <can_id> (source can_id. Use 8 digits for extended IDs)\n");
	fprintf(stderr, "         -d <can_id> (destination can_id. Use 8 digits for extended IDs)\n");
	fprintf(stderr, "         -x <addr>   (extended addressing mode.)\n");
	fprintf(stderr, "         -c          (color mode)\n");
	fprintf(stderr, "         -t <type>   (timestamp: (a)bsolute/(d)elta/(z)ero/(A)bsolute w date)\n");
	fprintf(stderr, "         -f <format> (1 = HEX, 2 = ASCII, 3 = HEX & ASCII - default: %d)\n", FORMAT_DEFAULT);
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
			printf("(%ld.%06ld) ", tv->tv_sec, tv->tv_usec);
			break;

		case 'A': /* absolute with date */
		{
			struct tm tm;
			char timestring[25];

			tm = *localtime(&tv->tv_sec);
			strftime(timestring, 24, "%Y-%m-%d %H:%M:%S", &tm);
			printf("(%s.%06ld) ", timestring, tv->tv_usec);
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
			printf("(%ld.%06ld) ", diff.tv_sec, diff.tv_usec);

			if (timestamp == 'd')
				*last_tv = *tv; /* update for delta calculation */
		}
		break;

		default: /* no timestamp output */
			break;
		}
	}

	/* the source socket gets pdu data from the detination id */
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
			if ((buffer[i] > 0x1F) && (buffer[i] < 0x7F))
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
	int s, t;
	struct sockaddr_can addr;
	struct ifreq ifr;
	static struct can_isotp_options opts;
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

	while ((opt = getopt(argc, argv, "s:d:x:h:ct:f:?")) != -1) {
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

		case 'f':
			format = (atoi(optarg) & (FORMAT_ASCII | FORMAT_HEX));
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
		exit(1);
	}
  
	if ((s = socket(PF_CAN, SOCK_DGRAM, CAN_ISOTP)) < 0) {
		perror("socket");
		exit(1);
	}

	if ((t = socket(PF_CAN, SOCK_DGRAM, CAN_ISOTP)) < 0) {
		perror("socket");
		exit(1);
	}

	opts.flags |= CAN_ISOTP_LISTEN_MODE;

	setsockopt(s, SOL_CAN_ISOTP, CAN_ISOTP_OPTS, &opts, sizeof(opts));
	setsockopt(t, SOL_CAN_ISOTP, CAN_ISOTP_OPTS, &opts, sizeof(opts));

	addr.can_family = AF_CAN;
	strcpy(ifr.ifr_name, argv[optind]);
	ioctl(s, SIOCGIFINDEX, &ifr);
	addr.can_ifindex = ifr.ifr_ifindex;

	addr.can_addr.tp.tx_id = dst;
	addr.can_addr.tp.rx_id = src;

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(s);
		exit(1);
	}

	addr.can_addr.tp.tx_id = src;
	addr.can_addr.tp.rx_id = dst;

	if (bind(t, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(s);
		exit(1);
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
				return -1;
			}
			if (nbytes > 4095)
				return -1;
			printbuf(buffer, nbytes, color?1:0, timestamp, format,
				 &tv, &last_tv, src, s, ifr.ifr_name, head);
		}

		if (FD_ISSET(t, &rdfs)) {
			nbytes = read(t, buffer, 4096);
			if (nbytes < 0) {
				perror("read socket t");
				return -1;
			}
			if (nbytes > 4095)
				return -1;
			printbuf(buffer, nbytes, color?2:0, timestamp, format,
				 &tv, &last_tv, dst, t, ifr.ifr_name, head);
		}
	}

	close(s);

	return 0;
}
