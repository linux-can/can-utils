/*
 *  $Id$
 */

/*
 * canbusload.c
 *
 * Copyright (c) 2002-2008 Volkswagen Group Electronic Research
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <libgen.h>
#include <time.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <net/if.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include "terminal.h"

#define MAXSOCK 16    /* max. number of CAN interfaces given on the cmdline */

#define PERCENTRES 5 /* resolution in percent for bargraph */
#define NUMBAR (100/PERCENTRES) /* number of bargraph elements */

extern int optind, opterr, optopt;

static struct {
	char devname[IFNAMSIZ+1];
	unsigned int bitrate;
	unsigned int recv_frames;
	unsigned int recv_bits_total;
	unsigned int recv_bits_payload;
} stat[MAXSOCK+1];

static int  max_devname_len; /* to prevent frazzled device name output */ 
static int  max_bitrate_len;
static int  currmax;
static unsigned char redraw;
static unsigned char timestamp;
static unsigned char color;
static unsigned char bargraph;
static unsigned char ignore_bitstuffing;
static char *prg;

void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s [options] <CAN interface>+\n", prg);
	fprintf(stderr, "  (use CTRL-C to terminate %s)\n\n", prg);
	fprintf(stderr, "Options: -t (show current time on the first line)\n");
	fprintf(stderr, "         -c (colorize lines)\n");
	fprintf(stderr, "         -b (show bargraph in %d%% resolution)\n", PERCENTRES);
	fprintf(stderr, "         -r (redraw the terminal - similar to top)\n");
	fprintf(stderr, "         -i (ignore bitstuffing estimation in bandwith calculation)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Up to %d CAN interfaces with mandatory bitrate can be specified on the \n", MAXSOCK);
	fprintf(stderr, "commandline in the form: <ifname>@<bitrate>\n\n");
	fprintf(stderr, "The bitrate is mandatory as it is needed to know the CAN bus bitrate to\n");
	fprintf(stderr, "calcultate the bus load percentage based on the received CAN frames.\n");
	fprintf(stderr, "Due to the bitstuffing estimation the calculated busload may exceed 100%%.\n");
	fprintf(stderr, "For each given interface the data is presented in one line which contains:\n\n");
	fprintf(stderr, "(interface) (received CAN frames) (used bits total) (used bits for payload)\n");
	fprintf(stderr, "\nExample:\n");
	fprintf(stderr, "\nuser$> canbusload can0@100000 can1@500000 can2@500000 can3@500000 -r -t -b -c\n\n");
	fprintf(stderr, "%s 2008-05-27 15:18:49\n", prg);
	fprintf(stderr, " can0@100000  805  74491  36656  74%%  |XXXXXXXXXXXXXX......|\n");
	fprintf(stderr, " can1@500000  796  75140  37728  15%%  |XXX.................|\n");
	fprintf(stderr, " can2@500000    0      0      0   0%%  |....................|\n");
	fprintf(stderr, " can3@500000   47   4633   2424   0%%  |....................|\n");
	fprintf(stderr, "\n");
}

void sigterm(int signo)
{
	exit(0);
}

void printstats(int signo)
{
	int i, j, percent;

	if (redraw)
		printf("%s", CSR_HOME);

	if (timestamp) {
		time_t currtime;
		struct tm now;

		if (time(&currtime) == (time_t)-1) {
			perror("time");
			exit(1);
		}

		localtime_r(&currtime, &now);

		printf("%s %04d-%02d-%02d %02d:%02d:%02d\n",
		       prg,
		       now.tm_year + 1900,
		       now.tm_mon + 1,
		       now.tm_mday,
		       now.tm_hour,
		       now.tm_min,
		       now.tm_sec);
	}

	for (i=0; i<currmax; i++) {

		if (color) {
			if (i%2)
				printf("%s", FGRED);
			else
				printf("%s", FGBLUE);
		}

		if (stat[i].bitrate)
			percent = (stat[i].recv_bits_total*100)/stat[i].bitrate;
		else
			percent = 0;

		printf(" %*s@%-*d %4d %6d %6d %3d%%",
		       max_devname_len, stat[i].devname,
		       max_bitrate_len, stat[i].bitrate,
		       stat[i].recv_frames,
		       stat[i].recv_bits_total,
		       stat[i].recv_bits_payload,
		       percent);

		if (bargraph) {

			printf("  |");

			if (percent > 100)
				percent = 100;

			for (j=0; j < NUMBAR; j++){
				if (j < percent/PERCENTRES)
					printf("X");
				else
					printf(".");
			}
	    
			printf("|");
		}
	
		if (color)
			printf("%s", ATTRESET);

		printf("\n");

		stat[i].recv_frames = 0;
		stat[i].recv_bits_total = 0;
		stat[i].recv_bits_payload = 0;
	}

	printf("\n");
	fflush(stdout);

	alarm(1);
}

int main(int argc, char **argv)
{
	fd_set rdfs;
	int s[MAXSOCK];

	int opt;
	char *ptr, *nptr;
	struct sockaddr_can addr;
	struct can_frame frame;
	int nbytes, i;
	struct ifreq ifr;
	sigset_t sigmask, savesigmask;

	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);
	signal(SIGINT, sigterm);

	signal(SIGALRM, printstats);

	prg = basename(argv[0]);

	while ((opt = getopt(argc, argv, "rtbcih?")) != -1) {
		switch (opt) {
		case 'r':
			redraw = 1;
			break;

		case 't':
			timestamp = 1;
			break;

		case 'b':
			bargraph = 1;
			break;

		case 'c':
			color = 1;
			break;

		case 'i':
			ignore_bitstuffing = 1;
			break;

		default:
			print_usage(prg);
			exit(1);
			break;
		}
	}

	if (optind == argc) {
		print_usage(prg);
		exit(0);
	}
	
	currmax = argc - optind; /* find real number of CAN devices */

	if (currmax > MAXSOCK) {
		printf("More than %d CAN devices given on commandline!\n", MAXSOCK);
		return 1;
	}

	for (i=0; i < currmax; i++) {

		ptr = argv[optind+i];

		nbytes = strlen(ptr);
		if (nbytes >= IFNAMSIZ+sizeof("@1000000")+1) {
			printf("name of CAN device '%s' is too long!\n", ptr);
			return 1;
		}

#ifdef DEBUG
		printf("open %d '%s'.\n", i, ptr);
#endif

		s[i] = socket(PF_CAN, SOCK_RAW, CAN_RAW);
		if (s[i] < 0) {
			perror("socket");
			return 1;
		}

		nptr = strchr(ptr, '@');

		if (!nptr) {
			print_usage(prg);
			return 1;
		}

		nbytes = nptr - ptr;  /* interface name is up the first '@' */

		if (nbytes >= IFNAMSIZ) {
			printf("name of CAN device '%s' is too long!\n", ptr);
			return 1;
		}

		strncpy(stat[i].devname, ptr, nbytes);
		memset(&ifr.ifr_name, 0, sizeof(ifr.ifr_name));
		strncpy(ifr.ifr_name, ptr, nbytes);

		if (nbytes > max_devname_len)
			max_devname_len = nbytes; /* for nice printing */

		stat[i].bitrate = atoi(nptr+1); /* bitrate is placed behind the '@' */

		if (!stat[i].bitrate || stat[i].bitrate > 1000000) {
			printf("invalid bitrate for CAN device '%s'!\n", ptr);
			return 1;
		}

		nbytes = strlen(nptr+1);
		if (nbytes > max_bitrate_len)
			max_bitrate_len = nbytes; /* for nice printing */


#ifdef DEBUG
		printf("using interface name '%s'.\n", ifr.ifr_name);
#endif

		if (ioctl(s[i], SIOCGIFINDEX, &ifr) < 0) {
			perror("SIOCGIFINDEX");
			exit(1);
		}

		addr.can_family = AF_CAN;
		addr.can_ifindex = ifr.ifr_ifindex;

		if (bind(s[i], (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			perror("bind");
			return 1;
		}
	}

	alarm(1);

	if (redraw)
		printf("%s", CLR_SCREEN);

	while (1) {

		FD_ZERO(&rdfs);
		for (i=0; i<currmax; i++)
			FD_SET(s[i], &rdfs);

		savesigmask = sigmask;

		if (pselect(s[currmax-1]+1, &rdfs, NULL, NULL, NULL, &sigmask) < 0) {
			//perror("pselect");
			sigmask = savesigmask;
			continue;
		}

		for (i=0; i<currmax; i++) {  /* check all CAN RAW sockets */

			if (FD_ISSET(s[i], &rdfs)) {

				nbytes = read(s[i], &frame, sizeof(struct can_frame));

				if (nbytes < 0) {
					perror("read");
					return 1;
				}

				if (nbytes < sizeof(struct can_frame)) {
					fprintf(stderr, "read: incomplete CAN frame\n");
					return 1;
				}

				stat[i].recv_frames++;
				stat[i].recv_bits_payload += frame.can_dlc*8;

				/*
				 * Following Ken Tindells *worst* case calculation for stuff-bits
				 * (see "Guaranteeing Message Latencies on Controller Area Network" 1st ICC'94)
				 * the needed bits on the wire can be calculated as:
				 *
				 * (34 + 8n)/5 + 47 + 8n for SFF frames (11 bit CAN-ID) => (269 + 48n)/5 
				 * (54 + 8n)/5 + 67 + 8n for EFF frames (29 bit CAN-ID) => (389 + 48n)/5 
				 *
				 * while 'n' is the data length code (number of payload bytes)
				 *
				 */

				if (ignore_bitstuffing) {
					/* calculation without bitstuffing */
					if (frame.can_id & CAN_EFF_FLAG)
						stat[i].recv_bits_total += 67 + frame.can_dlc*8;
					else
						stat[i].recv_bits_total += 47 + frame.can_dlc*8;
				} else {
					/* needed bits including estimated worst case stuff bits */
					if (frame.can_id & CAN_EFF_FLAG)
						stat[i].recv_bits_total += (389 + frame.can_dlc*48)/5;
					else
						stat[i].recv_bits_total += (269 + frame.can_dlc*48)/5;
				}
			}
		}
	}

	for (i=0; i<currmax; i++)
		close(s[i]);

	return 0;
}
