/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * canbusload.c - monitor CAN bus load
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

#include <ctype.h>
#include <libgen.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include "lib.h"
#include "terminal.h"
#include "canframelen.h"

#define ANYDEV "any" /* name of interface to receive from any CAN interface */
#define MAXDEVS 20   /* max. number of CAN interfaces given on the cmdline */

#define PERCENTRES 5 /* resolution in percent for bargraph */
#define NUMBAR (100 / PERCENTRES) /* number of bargraph elements */
#define BRSTRLEN 20
#define VISUAL_WINDOW 90 /* window width for visualization */

/*
 * Inspired from
 * https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/
 * include/linux/sched/loadavg.h
 *
 * Following are the fixed-point math constants and the exponential-damping
 * factors for:
 *  - 1 samples/s in 1 minute
 *  - 1 samples/s in 5 minutes
 *  - 1 samples/s in 15 minutes
 * in fixed-point representation.
 */
#define FP_SHIFT 12              /* bits of precision */
#define FP_ONE   (1 << FP_SHIFT) /* 1.0 fixed-point representation */
#define EXP_1    4028            /* (1 / e ^ (1 /  60)) * FP_ONE */
#define EXP_5    4082            /* (1 / e ^ (1 / 300)) * FP_ONE */
#define EXP_15   4091            /* (1 / e ^ (1 / 900)) * FP_ONE */

extern int optind, opterr, optopt;

static struct {
	char devname[IFNAMSIZ + 1];
	char bitratestr[BRSTRLEN]; /* 100000/2000000 => 100k/2M */
	char recv_direction;
	int ifindex;
	unsigned int bitrate;
	unsigned int dbitrate;
	unsigned int recv_frames;
	unsigned int recv_bits_total;
	unsigned int recv_bits_payload;
	unsigned int recv_bits_dbitrate;
	unsigned int load_min;
	unsigned int load_max;
	unsigned int load_1m;
	unsigned int load_5m;
	unsigned int load_15m;
	unsigned int loads[VISUAL_WINDOW];
	unsigned int index;
} stat[MAXDEVS + 1];

static volatile int running = 1;
static volatile sig_atomic_t signal_num;
static int max_devname_len; /* to prevent frazzled device name output */
static int max_bitratestr_len;
static unsigned int currmax;
static unsigned char redraw;
static unsigned char timestamp;
static unsigned char color;
static unsigned char bargraph;
static bool statistic;
static bool reset;
static bool visualize;
static enum cfl_mode mode = CFL_WORSTCASE;
static char *prg;
static struct termios old;

static void print_usage(char *prg)
{
	fprintf(stderr, "%s - monitor CAN bus load.\n", prg);
	fprintf(stderr, "\nUsage: %s [options] <CAN interface>+\n", prg);
	fprintf(stderr, "  (use CTRL-C to terminate %s)\n\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -t  (show current time on the first line)\n");
	fprintf(stderr, "         -c  (colorize lines)\n");
	fprintf(stderr, "         -b  (show bargraph in %d%% resolution)\n", PERCENTRES);
	fprintf(stderr, "         -r  (redraw the terminal - similar to top)\n");
	fprintf(stderr, "         -i  (ignore bitstuffing in bandwidth calculation)\n");
	fprintf(stderr, "         -e  (exact calculation of stuffed bits)\n");
	fprintf(stderr, "         -s  (show statistics, press 'r' to reset)\n");
	fprintf(stderr, "         -v  (show busload visualization)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Up to %d CAN interfaces with mandatory bitrate can be specified on the \n", MAXDEVS);
	fprintf(stderr, "commandline in the form: <ifname>@<bitrate>[,<dbitrate>]\n");
	fprintf(stderr, "The interface name 'any' enables an auto detection with the given bitrate[s]\n\n");
	fprintf(stderr, "The bitrate is mandatory as it is needed to know the CAN bus bitrate to\n");
	fprintf(stderr, "calculate the bus load percentage based on the received CAN frames.\n");
	fprintf(stderr, "Due to the bitstuffing estimation the calculated busload may exceed 100%%.\n");
	fprintf(stderr, "For each given interface the data is presented in one line which contains:\n\n");
	fprintf(stderr, "(interface) (received CAN frames) (bits total) (bits payload) (bits payload brs)\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "\nuser$> canbusload can0@100000 can1@500000,2000000 can2@500000 -r -t -b -c\n\n");
	fprintf(stderr, "%s 2024-08-08 16:30:05 (worst case bitstuffing)\n", prg);
	fprintf(stderr, " can0@100k      192   21980    9136       0  21%% |TTTT................|\n");
	fprintf(stderr, " can1@500k/2M  2651  475500  234448  131825  74%% |XXXXXXXXXXXXXX......|\n");
	fprintf(stderr, " can2@500k      855  136777   62968   35219  27%% |RRRRR...............|\n");
	fprintf(stderr, "\n");
}

static void sigterm(int signo)
{
	running = 0;
	signal_num = signo;
}

static int add_bitrate(char *brstr, unsigned int bitrate)
{
	if (bitrate % 1000000 == 0)
		return sprintf(brstr, "%dM", bitrate / 1000000);

	if (bitrate % 1000 == 0)
		return sprintf(brstr, "%dk", bitrate / 1000);

	return sprintf(brstr, "%d", bitrate);
}

static void create_bitrate_string(int stat_idx, int *max_bitratestr_len)
{
	int ptr;

	ptr = add_bitrate(&stat[stat_idx].bitratestr[0], stat[stat_idx].bitrate);

	if (stat[stat_idx].bitrate != stat[stat_idx].dbitrate) {
		ptr += sprintf(&stat[stat_idx].bitratestr[ptr], "/");
		ptr += add_bitrate(&stat[stat_idx].bitratestr[ptr], stat[stat_idx].dbitrate);
	}

	if (ptr > *max_bitratestr_len)
		*max_bitratestr_len = ptr;
}

static unsigned int calc_load(unsigned int load_fp,
                              unsigned int exp_fp,
                              unsigned int sample)
{
	unsigned int sample_fp  = sample << FP_SHIFT;
	unsigned int damped_sum = (load_fp * exp_fp) +
	                          (sample_fp * (FP_ONE - exp_fp));
	return damped_sum >> FP_SHIFT;
}

static void printstats(int signo)
{
	unsigned int i, j, k, percent, index;

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

		printf("%s %04d-%02d-%02d %02d:%02d:%02d ",
		       prg,
		       now.tm_year + 1900,
		       now.tm_mon + 1,
		       now.tm_mday,
		       now.tm_hour,
		       now.tm_min,
		       now.tm_sec);

		switch (mode) {

		case CFL_NO_BITSTUFFING:
			/* plain bit calculation without bitstuffing */
			printf("(ignore bitstuffing)\n");
			break;

		case CFL_WORSTCASE:
			/* worst case estimation - see above */
			printf("(worst case bitstuffing)\n");
			break;

		case CFL_EXACT:
			/* exact calculation of stuffed bits based on frame content and CRC */
			printf("(exact bitstuffing)\n");
			break;

		default:
			printf("(unknown bitstuffing)\n");
			break;
		}
	}

	for (i = 0; i < currmax; i++) {
		if (color) {
			if (i % 2)
				printf("%s", FGRED);
			else
				printf("%s", FGBLUE);
		}

		if (stat[i].bitrate)
			percent = ((stat[i].recv_bits_total - stat[i].recv_bits_dbitrate) * 100) / stat[i].bitrate +
				(stat[i].recv_bits_dbitrate * 100) / stat[i].dbitrate;
		else
			percent = 0;

		printf(" %*s@%-*s %5d %7d %7d %7d %3d%%",
		       max_devname_len, stat[i].devname,
		       max_bitratestr_len, stat[i].bitratestr,
		       stat[i].recv_frames,
		       stat[i].recv_bits_total,
		       stat[i].recv_bits_payload,
		       stat[i].recv_bits_dbitrate,
		       percent);

		if (statistic) {
			if (reset) {
				stat[i].load_min = UINT_MAX;
				stat[i].load_max = 0;
				stat[i].load_1m = 0;
				stat[i].load_5m = 0;
				stat[i].load_15m = 0;
			}

			stat[i].load_min = MIN(stat[i].load_min, percent);
			stat[i].load_max = MAX(stat[i].load_max, percent);

			stat[i].load_1m = calc_load(stat[i].load_1m, EXP_1, percent);
			stat[i].load_5m = calc_load(stat[i].load_5m, EXP_5, percent);
			stat[i].load_15m = calc_load(stat[i].load_15m, EXP_15, percent);

			printf(" min:%3d%%, max:%3d%%, load:%3d%% %3d%% %3d%%",
			       stat[i].load_min,
			       stat[i].load_max,
			       (stat[i].load_1m + (FP_ONE >> 1)) >> FP_SHIFT,
			       (stat[i].load_5m + (FP_ONE >> 1)) >> FP_SHIFT,
			       (stat[i].load_15m + (FP_ONE >> 1)) >> FP_SHIFT);
		}

		if (bargraph) {

			printf(" |");

			if (percent > 100)
				percent = 100;

			for (j = 0; j < NUMBAR; j++) {
				if (j < percent / PERCENTRES)
					printf("%c", stat[i].recv_direction);
				else
					printf(".");
			}

			printf("|");
		}

		if (visualize) {
			stat[i].loads[stat[i].index] = percent;
			stat[i].index = (stat[i].index + 1) % VISUAL_WINDOW;

			printf("\n");
			for (j = 0; j < NUMBAR; j++) {
				printf("%3d%%|", (NUMBAR - j) * PERCENTRES);
				index = stat[i].index;
				for (k = 0; k < VISUAL_WINDOW; k++) {
					percent = stat[i].loads[index];

					if ((percent / PERCENTRES) >= (NUMBAR - j))
						printf("X");
					else
						printf(".");

					index = (index + 1) % VISUAL_WINDOW;
				}
				printf("\n");
			}
		}

		if (color)
			printf("%s", ATTRESET);

		if (!redraw || (i < currmax - 1))
			printf("\n");

		stat[i].recv_frames = 0;
		stat[i].recv_bits_total = 0;
		stat[i].recv_bits_dbitrate = 0;
		stat[i].recv_bits_payload = 0;
		stat[i].recv_direction = '.';
	}

	reset = false;

	if (!redraw)
		printf("\n");

	fflush(stdout);

	alarm(1);
}

void cleanup()
{
	tcsetattr(STDIN_FILENO, TCSANOW, &old);
}

int main(int argc, char **argv)
{
	fd_set rdfs;
	int s;
	int opt;
	char *ptr, *nptr;
	struct sockaddr_can addr;
	struct canfd_frame frame;
	struct iovec iov;
	struct msghdr msg;
	unsigned int i;
	int nbytes;

	int have_anydev = 0;
	unsigned int anydev_bitrate = 0;
	unsigned int anydev_dbitrate = 0;
	char anydev_bitratestr[BRSTRLEN]; /* 100000/2000000 => 100k/2M */
	struct termios temp;

	tcgetattr(STDIN_FILENO, &old);
	atexit(cleanup);
	temp = old;
	temp.c_lflag &= ~(ICANON | ECHO);
	tcsetattr(STDIN_FILENO, TCSANOW, &temp);

	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);
	signal(SIGINT, sigterm);

	signal(SIGALRM, printstats);

	prg = basename(argv[0]);

	while ((opt = getopt(argc, argv, "rtbciesvh?")) != -1) {
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
			mode = CFL_NO_BITSTUFFING;
			break;

		case 'e':
			mode = CFL_EXACT;
			break;

		case 's':
			statistic = true;
			reset = true;
			break;

		case 'v':
			visualize = true;
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

	if (currmax > MAXDEVS) {
		printf("More than %d CAN devices given on commandline!\n", MAXDEVS);
		return 1;
	}

	/* prefill stat[] array with given interface assignments */
	for (i = 0; i < currmax; i++) {
		ptr = argv[optind + i + have_anydev];

		nbytes = strlen(ptr);
		if (nbytes >= (int)(IFNAMSIZ + sizeof("@1000000,2000000") + 1)) {
			printf("name of CAN device '%s' is too long!\n", ptr);
			return 1;
		}

		pr_debug("handle %d '%s'.\n", i, ptr);

		nptr = strchr(ptr, '@');

		if (!nptr) {
			fprintf(stderr, "Specify CAN interfaces in the form <CAN interface>@<bitrate>, e.g. can0@500000\n");
			print_usage(prg);
			return 1;
		}

		/* interface name length */
		nbytes = nptr - ptr;  /* interface name is up the first '@' */
		if (nbytes >= (int)IFNAMSIZ) {
			printf("name of CAN device '%s' is too long!\n", ptr);
			return 1;
		}

		/* copy interface name to stat[] entry */
		strncpy(stat[i].devname, ptr, nbytes);

		if (nbytes > max_devname_len)
			max_devname_len = nbytes; /* for nice printing */

		char *endp;
		 /* bitrate is placed behind the '@' */
		stat[i].bitrate = strtol(nptr + 1, &endp, 0);

		/* check for CAN FD additional data bitrate */
		if (*endp == ',')
			/* data bitrate is placed behind the ',' */
			stat[i].dbitrate = strtol(endp + 1, &endp, 0);
		else
			stat[i].dbitrate = stat[i].bitrate;

		if (!stat[i].bitrate || stat[i].bitrate > 1000000 ||
		    !stat[i].dbitrate || stat[i].dbitrate > 8000000) {
			printf("invalid bitrate for CAN device '%s'!\n", ptr);
			return 1;
		}

		/* prepare bitrate string for hot path */
		create_bitrate_string(i, &max_bitratestr_len);

		stat[i].recv_direction = '.';

		/* handling for 'any' device */
		if (have_anydev == 0 && strcmp(ANYDEV, stat[i].devname) == 0) {
			anydev_bitrate = stat[i].bitrate;
			anydev_dbitrate = stat[i].dbitrate;
			memcpy(anydev_bitratestr, stat[i].bitratestr, BRSTRLEN);
			/* no real interface: remove this command line entry */
			have_anydev = 1;
			currmax--;
			i--;
		} else {
			stat[i].ifindex = if_nametoindex(stat[i].devname);
			if (!stat[i].ifindex) {
				printf("invalid CAN device '%s'!\n", stat[i].devname);
				return 1;
			}
			pr_debug("using interface name '%s'.\n", stat[i].devname);
		}
	}

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) {
		perror("socket");
		return 1;
	}

	/* try to switch the socket into CAN FD mode */
	const int canfd_on = 1;
	setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canfd_on, sizeof(canfd_on));

	addr.can_family = AF_CAN;
	addr.can_ifindex = 0; /* any CAN device */

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	alarm(1);

	if (redraw)
		printf("%s", CLR_SCREEN);

	/* these settings are static and can be held out of the hot path */
	iov.iov_base = &frame;
	msg.msg_name = &addr;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;

	while (running) {
		FD_ZERO(&rdfs);
		FD_SET(s, &rdfs);
		FD_SET(STDIN_FILENO, &rdfs);

		if (select(s + 1, &rdfs, NULL, NULL, NULL) < 0) {
			//perror("pselect");
			continue;
		}

		if (FD_ISSET(STDIN_FILENO, &rdfs)) {
			if (getchar() == 'r') {
				reset = true;
			}
		}

		/* these settings may be modified by recvmsg() */
		iov.iov_len = sizeof(frame);
		msg.msg_namelen = sizeof(addr);
		msg.msg_controllen = 0;
		msg.msg_flags = 0;

		nbytes = recvmsg(s, &msg, 0);

		if (nbytes < 0) {
			perror("read");
			return 1;
		}

		if (nbytes != (int)sizeof(struct can_frame) &&
		    nbytes != (int)sizeof(struct canfd_frame)) {
			fprintf(stderr, "read: incomplete CAN frame\n");
			return 1;
		}

		/* find received ifindex in stat[] array */
		for (i = 0; i < currmax; i++) {
			if (stat[i].ifindex == addr.can_ifindex)
				break;
		}

		/* not found? check for unknown interface */
		if (i >= currmax) {
			/* drop unwanted traffic */
			if (have_anydev == 0)
				continue;

			/* can we add another interface? */
			if (currmax >= MAXDEVS)
				continue;

			/* add an new entry */
			stat[i].ifindex = addr.can_ifindex;
			stat[i].bitrate = anydev_bitrate;
			stat[i].dbitrate = anydev_dbitrate;
			memcpy(stat[i].bitratestr, anydev_bitratestr, BRSTRLEN);
			stat[i].recv_direction = '.';
			if_indextoname(addr.can_ifindex, stat[i].devname);
			nbytes = strlen(stat[i].devname);
			if (nbytes > max_devname_len)
				max_devname_len = nbytes; /* for nice printing */
			currmax++;
		}

		if (msg.msg_flags & MSG_DONTROUTE) {
			/* TX direction */
			if (stat[i].recv_direction == '.')
				stat[i].recv_direction = 'T';
			else if (stat[i].recv_direction == 'R')
				stat[i].recv_direction = 'X';
		} else {
			/* RX direction */
			if (stat[i].recv_direction == '.')
				stat[i].recv_direction = 'R';
			else if (stat[i].recv_direction == 'T')
				stat[i].recv_direction = 'X';
		}

		stat[i].recv_frames++;
		stat[i].recv_bits_payload += frame.len * 8;
		stat[i].recv_bits_dbitrate += can_frame_dbitrate_length(
			&frame, mode, sizeof(frame));
		stat[i].recv_bits_total += can_frame_length(&frame,
							    mode, nbytes);
	}

	close(s);

	if (signal_num)
		return 128 + signal_num;

	return 0;
}
