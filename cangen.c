/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * cangen.c - CAN frames generator
 *
 * Copyright (c) 2022 Pengutronix,
 *		 Marc Kleine-Budde <kernel@pengutronix.de>
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

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/net_tstamp.h>

#include "lib.h"

#define DEFAULT_GAP 200 /* ms */
#define DEFAULT_BURST_COUNT 1
#define DEFAULT_SO_MARK_VAL 1

#define MODE_RANDOM 0
#define MODE_INCREMENT 1
#define MODE_FIX 2
#define MODE_RANDOM_EVEN 3
#define MODE_RANDOM_ODD 4
#define MODE_RANDOM_FIX 5

#define NIBBLE_H 1
#define NIBBLE_L 2

#define CHAR_RANDOM 'x'

extern int optind, opterr, optopt;

static volatile int running = 1;
static volatile sig_atomic_t signal_num;
static unsigned long long enobufs_count;
static bool ignore_enobufs;
static bool use_so_txtime;

static int clockid = CLOCK_TAI;
static int clock_nanosleep_flags;
static struct timespec ts, ts_gap;
static int so_mark_val = DEFAULT_SO_MARK_VAL;

#define NSEC_PER_SEC 1000000000LL

static struct timespec timespec_normalise(struct timespec ts)
{
	while (ts.tv_nsec >= NSEC_PER_SEC) {
		++(ts.tv_sec);
		ts.tv_nsec -= NSEC_PER_SEC;
	}

	while (ts.tv_nsec <= -NSEC_PER_SEC) {
		--(ts.tv_sec);
		ts.tv_nsec += NSEC_PER_SEC;
	}

	if (ts.tv_nsec < 0) {
		/*
		 * Negative nanoseconds isn't valid according to
		 * POSIX. Decrement tv_sec and roll tv_nsec over.
		 */

		--(ts.tv_sec);
		ts.tv_nsec = (NSEC_PER_SEC + ts.tv_nsec);
	}

	return ts;
}

static struct timespec timespec_add(struct timespec ts1, struct timespec ts2)
{
	/*
	 * Normalize inputs to prevent tv_nsec rollover if
	 * whole-second values are packed in it.
	 */
	ts1 = timespec_normalise(ts1);
	ts2 = timespec_normalise(ts2);

	ts1.tv_sec += ts2.tv_sec;
	ts1.tv_nsec += ts2.tv_nsec;

	return timespec_normalise(ts1);
}

struct timespec double_to_timespec(double s)
{
	struct timespec ts = {
		.tv_sec  = s,
		.tv_nsec = (s - (long)(s)) * NSEC_PER_SEC,
	};

	return timespec_normalise(ts);
}

static struct timespec ns_to_timespec(int64_t ns)
{
	struct timespec ts = {
		.tv_sec  = ns / NSEC_PER_SEC,
		.tv_nsec = ns % NSEC_PER_SEC,
	};

	return timespec_normalise(ts);
}

static void print_usage(char *prg)
{
	fprintf(stderr, "%s - CAN frames generator.\n\n", prg);
	fprintf(stderr, "Usage: %s [options] <CAN interface>\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -g <ms>       (gap in milli seconds - default: %d ms)\n", DEFAULT_GAP);
	fprintf(stderr, "         -a            (use absolute time for gap)\n");
	fprintf(stderr, "         -t            (use SO_TXTIME)\n");
	fprintf(stderr, "         --start <ns>  (start time (UTC nanoseconds))\n");
	fprintf(stderr, "         --mark <id>   (set SO_MARK to <id>, default %u)\n", DEFAULT_SO_MARK_VAL);
	fprintf(stderr, "         -e            (generate extended frame mode (EFF) CAN frames)\n");
	fprintf(stderr, "         -f            (generate CAN FD CAN frames)\n");
	fprintf(stderr, "         -b            (generate CAN FD CAN frames with bitrate switch (BRS))\n");
	fprintf(stderr, "         -E            (generate CAN FD CAN frames with error state (ESI))\n");
	fprintf(stderr, "         -R            (generate RTR frames)\n");
	fprintf(stderr, "         -8            (allow DLC values greater then 8 for Classic CAN frames)\n");
	fprintf(stderr, "         -m            (mix -e -f -b -E -R frames)\n");
	fprintf(stderr, "         -I <mode>     (CAN ID generation mode - see below)\n");
	fprintf(stderr, "         -L <mode>     (CAN data length code (dlc) generation mode - see below)\n");
	fprintf(stderr, "         -D <mode>     (CAN data (payload) generation mode - see below)\n");
	fprintf(stderr, "         -p <timeout>  (poll on -ENOBUFS to write frames with <timeout> ms)\n");
	fprintf(stderr, "         -n <count>    (terminate after <count> CAN frames - default infinite)\n");
	fprintf(stderr, "         -i            (ignore -ENOBUFS return values on write() syscalls)\n");
	fprintf(stderr, "         -x            (disable local loopback of generated CAN frames)\n");
	fprintf(stderr, "         -c <count>    (number of messages to send in burst, default %u)\n", DEFAULT_BURST_COUNT);
	fprintf(stderr, "         -v            (increment verbose level for printing sent CAN frames)\n\n");
	fprintf(stderr, "Generation modes:\n");
	fprintf(stderr, " 'r'     => random values (default)\n");
	fprintf(stderr, " 'e'     => random values, even ID\n");
	fprintf(stderr, " 'o'     => random values, odd ID\n");
	fprintf(stderr, " 'i'     => increment values\n");
	fprintf(stderr, " <value> => fixed value (in hexadecimal for -I and -D)\n");
	fprintf(stderr, "         => nibbles written as '%c' are randomized (only -D)\n\n", CHAR_RANDOM);
	fprintf(stderr, "The gap value (in milliseconds) may have decimal places, e.g. '-g 4.73'\n");
	fprintf(stderr, "When incrementing the CAN data the data length code minimum is set to 1.\n");
	fprintf(stderr, "CAN IDs and data content are given and expected in hexadecimal values.\n\n");
	fprintf(stderr, "Examples:\n");
	fprintf(stderr, "%s vcan0 -g 4 -I 42A -L 1 -D i -v -v\n", prg);
	fprintf(stderr, "\t(fixed CAN ID and length, inc. data)\n");
	fprintf(stderr, "%s vcan0 -e -L i -v -v -v\n", prg);
	fprintf(stderr, "\t(generate EFF frames, incr. length)\n");
	fprintf(stderr, "%s vcan0 -D 11223344DEADBEEF -L 8\n", prg);
	fprintf(stderr, "\t(fixed CAN data payload and length)\n");
	fprintf(stderr, "%s vcan0 -D 11%c%c3344DEADBEEF -L 8\n", prg, CHAR_RANDOM, CHAR_RANDOM);
	fprintf(stderr, "\t(fixed CAN data payload where 2. byte is randomized, fixed length)\n");
	fprintf(stderr, "%s vcan0 -I 555 -D CCCCCCCCCCCCCCCC -L 8 -g 3.75\n", prg);
	fprintf(stderr, "\t(generate a fix busload without bit-stuffing effects)\n");
	fprintf(stderr, "%s vcan0 -g 0 -i -x\n", prg);
	fprintf(stderr, "\t(full load test ignoring -ENOBUFS)\n");
	fprintf(stderr, "%s vcan0 -g 0 -p 10 -x\n", prg);
	fprintf(stderr, "\t(full load test with polling, 10ms timeout)\n");
	fprintf(stderr, "%s vcan0\n", prg);
	fprintf(stderr, "\t(my favourite default :)\n\n");
}

static void sigterm(int signo)
{
	running = 0;
	signal_num = signo;
}

static int setsockopt_txtime(int fd)
{
	const struct sock_txtime so_txtime_val = {
		.clockid = clockid,
		.flags = SOF_TXTIME_REPORT_ERRORS,
	};
	struct sock_txtime so_txtime_val_read;
	int so_mark_val_read;
	socklen_t vallen;
	int ret;

	/* SO_TXTIME */

	ret = setsockopt(fd, SOL_SOCKET, SO_TXTIME,
			 &so_txtime_val, sizeof(so_txtime_val));
	if (ret) {
		int err = errno;

		perror("setsockopt() SO_TXTIME");
		if (err == EPERM)
			fprintf(stderr, "Run with CAP_NET_ADMIN or as root.\n");

		return -err;
	};

	vallen = sizeof(so_txtime_val_read);
	ret = getsockopt(fd, SOL_SOCKET, SO_TXTIME,
			 &so_txtime_val_read, &vallen);
	if (ret) {
		perror("getsockopt() SO_TXTIME");
		return -errno;
	};

	if (vallen != sizeof(so_txtime_val) ||
	    memcmp(&so_txtime_val, &so_txtime_val_read, vallen)) {
		perror("getsockopt() SO_TXTIME: mismatch");
		return -EINVAL;
	}

	/* SO_MARK */

	ret = setsockopt(fd, SOL_SOCKET, SO_MARK, &so_mark_val, sizeof(so_mark_val));
	if (ret) {
		int err = errno;

		perror("setsockopt() SO_MARK");
		if (err == EPERM)
			fprintf(stderr, "Run with CAP_NET_ADMIN or as root.\n");

		return -err;
	};

	vallen = sizeof(so_mark_val_read);
	ret = getsockopt(fd, SOL_SOCKET, SO_MARK,
			 &so_mark_val_read, &vallen);
	if (ret) {
		perror("getsockopt() SO_MARK");
		return -errno;
	};

	if (vallen != sizeof(so_mark_val) ||
	    memcmp(&so_mark_val, &so_mark_val_read, vallen)) {
		perror("getsockopt() SO_MARK: mismatch");
		return -EINVAL;
	}

	return 0;
}

static int do_send_one(int fd, void *buf, size_t len, int timeout)
{
	uint8_t control[CMSG_SPACE(sizeof(uint64_t))] = { 0 };
	struct iovec iov = {
		.iov_base = buf,
		.iov_len = len,
	};
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};
	ssize_t nbytes;
	int ret;

	if (use_so_txtime) {
		struct cmsghdr *cm;
		uint64_t tdeliver;

		msg.msg_control = control;
		msg.msg_controllen = sizeof(control);

		tdeliver = ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
		ts = timespec_add(ts, ts_gap);

		cm = CMSG_FIRSTHDR(&msg);
		cm->cmsg_level = SOL_SOCKET;
		cm->cmsg_type = SCM_TXTIME;
		cm->cmsg_len = CMSG_LEN(sizeof(tdeliver));
		memcpy(CMSG_DATA(cm), &tdeliver, sizeof(tdeliver));
	}

 resend:
	nbytes = sendmsg(fd, &msg, 0);
	if (nbytes < 0) {
		ret = -errno;
		if (ret != -ENOBUFS) {
			perror("write");
			return ret;
		}
		if (!ignore_enobufs && !timeout) {
			perror("write");
			return ret;
		}
		if (timeout) {
			struct pollfd fds = {
				.fd = fd,
				.events = POLLOUT,
			};

			/* wait for the write socket (with timeout) */
			ret = poll(&fds, 1, timeout);
			if (ret == 0 || (ret == -1 && errno != EINTR)) {
				ret = -errno;
				perror("poll");
				return ret;
			}
			goto resend;
		} else {
			enobufs_count++;
		}

	} else if (nbytes < (ssize_t)len) {
		fprintf(stderr, "write: incomplete CAN frame\n");
		return -EINVAL;
	}

	return 0;
}

static int setup_time(void)
{
	int ret;

	if (use_so_txtime) {
		/* start time is defined */
		if (ts.tv_sec || ts.tv_nsec)
			return 0;

		/* start time is now .... */
		ret = clock_gettime(clockid, &ts);
		if (ret) {
			perror("clock_gettime");
			return ret;
		}

		/* ... + gap */
		ts = timespec_add(ts, ts_gap);

		return 0;
	}

	if (ts.tv_sec || ts.tv_nsec) {
		ret = clock_nanosleep(clockid, TIMER_ABSTIME, &ts, NULL);
		if (ret != 0 && ret != EINTR) {
			perror("clock_nanosleep");
			return ret;
		}
	} else if (clock_nanosleep_flags == TIMER_ABSTIME) {
		ret = clock_gettime(clockid, &ts);
		if (ret)
			perror("clock_gettime");

		return ret;
	}

	if (clock_nanosleep_flags != TIMER_ABSTIME)
		ts = ts_gap;

	return 0;
}

enum {
	OPT_MARK = UCHAR_MAX + 1,
	OPT_START = UCHAR_MAX + 2,
};

/*
 * Search for CHAR_RANDOM in dataoptarg, save its position, replace it with 0.
 * Return 1 if at least one CHAR_RANDOM found.
 */
static int parse_dataoptarg(char *dataoptarg, unsigned char *rand_position)
{
	int mode_format_selected = MODE_FIX;
	int arglen = strlen(dataoptarg);

	/* Mark nibbles with * as fuzzable */
	for (int i = 0; i < CANFD_MAX_DLEN && i < arglen / 2; i++) {
		if (optarg[2 * i] == CHAR_RANDOM) {
			optarg[2 * i] = '0';
			rand_position[i] += NIBBLE_H;
			mode_format_selected = MODE_RANDOM_FIX;
		}
		if (optarg[2 * i + 1] == CHAR_RANDOM) {
			optarg[2 * i + 1] = '0';
			rand_position[i] += NIBBLE_L;
			mode_format_selected = MODE_RANDOM_FIX;
		}
	}

	return mode_format_selected;
}

int main(int argc, char **argv)
{
	double gap = DEFAULT_GAP;
	unsigned long burst_count = DEFAULT_BURST_COUNT;
	unsigned long polltimeout = 0;
	unsigned char extended = 0;
	unsigned char canfd = 0;
	unsigned char brs = 0;
	unsigned char esi = 0;
	unsigned char mix = 0;
	unsigned char id_mode = MODE_RANDOM;
	unsigned char data_mode = MODE_RANDOM;
	unsigned char dlc_mode = MODE_RANDOM;
	unsigned char loopback_disable = 0;
	unsigned char verbose = 0;
	unsigned char rtr_frame = 0;
	unsigned char len8_dlc = 0;
	int count = 0;
	unsigned long burst_sent_count = 0;
	int mtu, maxdlen;
	uint64_t incdata = 0;
	int incdlc = 0;
	unsigned long rnd;
	unsigned char fixdata[CANFD_MAX_DLEN];
	unsigned char rand_position[CANFD_MAX_DLEN] = {0};

	int opt;
	int s; /* socket */

	struct sockaddr_can addr = { 0 };
	static struct canfd_frame frame;
	struct can_frame *ccf = (struct can_frame *)&frame;
	int i;
	struct ifreq ifr;

	struct timeval now;
	int ret;

	/* set seed value for pseudo random numbers */
	gettimeofday(&now, NULL);
	srandom(now.tv_usec);

	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);
	signal(SIGINT, sigterm);

	const struct option long_options[] = {
		{ "mark",	required_argument,	0, OPT_MARK, },
		{ "start",	required_argument,	0, OPT_START, },
		{ 0,		0,			0, 0 },
	};

	while ((opt = getopt_long(argc, argv, "g:atefbER8mI:L:D:p:n:ixc:vh?", long_options, NULL)) != -1) {
		switch (opt) {

		case 'g':
			gap = strtod(optarg, NULL);
			break;

		case 'a':
			clock_nanosleep_flags = TIMER_ABSTIME;
			break;

		case 't':
			clock_nanosleep_flags = TIMER_ABSTIME;
			use_so_txtime = true;
			break;

		case OPT_START: {
			int64_t start_time_ns;

			start_time_ns = strtoll(optarg, NULL, 0);
			ts = ns_to_timespec(start_time_ns);

			break;
		}
		case OPT_MARK:
			so_mark_val = strtoul(optarg, NULL, 0);
			break;
		case 'e':
			extended = 1;
			break;

		case 'f':
			canfd = 1;
			break;

		case 'b':
			brs = 1; /* bitrate switch implies CAN FD */
			canfd = 1;
			break;

		case 'E':
			esi = 1; /* error state indicator implies CAN FD */
			canfd = 1;
			break;

		case 'R':
			rtr_frame = 1;
			break;

		case '8':
			len8_dlc = 1;
			break;

		case 'm':
			mix = 1;
			canfd = 1; /* to switch the socket into CAN FD mode */
			break;

		case 'I':
			if (optarg[0] == 'r') {
				id_mode = MODE_RANDOM;
			} else if (optarg[0] == 'i') {
				id_mode = MODE_INCREMENT;
			} else if (optarg[0] == 'e') {
				id_mode = MODE_RANDOM_EVEN;
			} else if (optarg[0] == 'o') {
				id_mode = MODE_RANDOM_ODD;
			} else {
				id_mode = MODE_FIX;
				frame.can_id = strtoul(optarg, NULL, 16);
			}
			break;

		case 'L':
			if (optarg[0] == 'r') {
				dlc_mode = MODE_RANDOM;
			} else if (optarg[0] == 'i') {
				dlc_mode = MODE_INCREMENT;
			} else {
				dlc_mode = MODE_FIX;
				frame.len = atoi(optarg) & 0xFF; /* is cut to 8 / 64 later */
			}
			break;

		case 'D':
			if (optarg[0] == 'r') {
				data_mode = MODE_RANDOM;
			} else if (optarg[0] == 'i') {
				data_mode = MODE_INCREMENT;
			} else {
				data_mode = parse_dataoptarg(optarg, rand_position);

				if (hexstring2data(optarg, fixdata, CANFD_MAX_DLEN)) {
					printf("wrong fix data definition\n");
					return 1;
				}
			}
			break;

		case 'p':
			polltimeout = strtoul(optarg, NULL, 10);
			break;

		case 'n':
			count = atoi(optarg);
			if (count < 1) {
				print_usage(basename(argv[0]));
				return 1;
			}
			break;

		case 'i':
			ignore_enobufs = true;
			break;

		case 'x':
			loopback_disable = 1;
			break;

		case 'c':
			burst_count = strtoul(optarg, NULL, 10);
			break;

		case 'v':
			verbose++;
			break;

		case '?':
		case 'h':
		default:
			print_usage(basename(argv[0]));
			return 1;
		}
	}

	if (optind == argc) {
		print_usage(basename(argv[0]));
		return 1;
	}

	ts_gap = double_to_timespec(gap / 1000);

	/* recognize obviously missing commandline option */
	if (id_mode == MODE_FIX && frame.can_id > 0x7FF && !extended) {
		printf("The given CAN-ID is greater than 0x7FF and the '-e' option is not set.\n");
		return 1;
	}

	if (strlen(argv[optind]) >= IFNAMSIZ) {
		printf("Name of CAN device '%s' is too long!\n\n", argv[optind]);
		return 1;
	}

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) {
		perror("socket");
		return 1;
	}

	addr.can_family = AF_CAN;

	strcpy(ifr.ifr_name, argv[optind]);
	if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
		perror("SIOCGIFINDEX");
		return 1;
	}
	addr.can_ifindex = ifr.ifr_ifindex;

	/*
	 * disable default receive filter on this RAW socket
	 * This is obsolete as we do not read from the socket at all, but for
	 * this reason we can remove the receive list in the Kernel to save a
	 * little (really a very little!) CPU usage.
	 */
	setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);

	if (loopback_disable) {
		const int loopback = 0;

		setsockopt(s, SOL_CAN_RAW, CAN_RAW_LOOPBACK,
			   &loopback, sizeof(loopback));
	}

	if (canfd) {
		const int enable_canfd = 1;

		/* check if the frame fits into the CAN netdevice */
		if (ioctl(s, SIOCGIFMTU, &ifr) < 0) {
			perror("SIOCGIFMTU");
			return 1;
		}

		if (ifr.ifr_mtu != CANFD_MTU) {
			printf("CAN interface is not CAN FD capable - sorry.\n");
			return 1;
		}

		/* interface is ok - try to switch the socket into CAN FD mode */
		if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd, sizeof(enable_canfd))) {
			printf("error when enabling CAN FD support\n");
			return 1;
		}

		/* ensure discrete CAN FD length values 0..8, 12, 16, 20, 24, 32, 64 */
		frame.len = can_fd_dlc2len(can_fd_len2dlc(frame.len));
	} else {
		/* sanitize Classical CAN 2.0 frame length */
		if (len8_dlc) {
			if (frame.len > CAN_MAX_RAW_DLC)
				frame.len = CAN_MAX_RAW_DLC;

			if (frame.len > CAN_MAX_DLEN)
				ccf->len8_dlc = frame.len;
		}

		if (frame.len > CAN_MAX_DLEN)
			frame.len = CAN_MAX_DLEN;
	}

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	if (use_so_txtime) {
		ret = setsockopt_txtime(s);
		if (ret)
			return 1;
	}

	ret = setup_time();
	if (ret)
		return 1;

	while (running) {
		frame.flags = 0;

		if (count && (--count == 0))
			running = 0;

		if (canfd) {
			mtu = CANFD_MTU;
			maxdlen = CANFD_MAX_DLEN;
			if (brs)
				frame.flags |= CANFD_BRS;
			if (esi)
				frame.flags |= CANFD_ESI;
		} else {
			mtu = CAN_MTU;
			maxdlen = CAN_MAX_DLEN;
		}

		if (id_mode == MODE_RANDOM)
			frame.can_id = random();
		else if (id_mode == MODE_RANDOM_EVEN)
			frame.can_id = random() & ~0x1;
		else if (id_mode == MODE_RANDOM_ODD)
			frame.can_id = random() | 0x1;

		if (extended) {
			frame.can_id &= CAN_EFF_MASK;
			frame.can_id |= CAN_EFF_FLAG;
		} else
			frame.can_id &= CAN_SFF_MASK;

		if (rtr_frame && !canfd)
			frame.can_id |= CAN_RTR_FLAG;

		if (dlc_mode == MODE_RANDOM) {
			if (canfd)
				frame.len = can_fd_dlc2len(random() & 0xF);
			else {
				frame.len = random() & 0xF;

				if (frame.len > CAN_MAX_DLEN) {
					/* generate Classic CAN len8 DLCs? */
					if (len8_dlc)
						ccf->len8_dlc = frame.len;

					frame.len = 8; /* for about 50% of the frames */
				} else {
					ccf->len8_dlc = 0;
				}
			}
		}

		if (data_mode == MODE_INCREMENT && !frame.len)
			frame.len = 1; /* min dlc value for incr. data */

		if (data_mode == MODE_RANDOM) {
			rnd = random();
			memcpy(&frame.data[0], &rnd, 4);
			rnd = random();
			memcpy(&frame.data[4], &rnd, 4);

			/* omit extra random number generation for CAN FD */
			if (canfd && frame.len > 8) {
				memcpy(&frame.data[8], &frame.data[0], 8);
				memcpy(&frame.data[16], &frame.data[0], 16);
				memcpy(&frame.data[32], &frame.data[0], 32);
			}
		}

		if (data_mode == MODE_RANDOM_FIX) {
			memcpy(frame.data, fixdata, CANFD_MAX_DLEN);

			for (int i = 0; i < frame.len; i++) {
				if (rand_position[i] == (NIBBLE_H | NIBBLE_L)) {
					frame.data[i] = random();
				} else if (rand_position[i] == NIBBLE_H) {
					frame.data[i] = (frame.data[i] & 0x0f) | (random() & 0xf0);
				} else if (rand_position[i] == NIBBLE_L) {
					frame.data[i] = (frame.data[i] & 0xf0) | (random() & 0x0f);
				}
			}
		}

		if (data_mode == MODE_FIX)
			memcpy(frame.data, fixdata, CANFD_MAX_DLEN);

		/* set unused payload data to zero like the CAN driver does it on rx */
		if (frame.len < maxdlen)
			memset(&frame.data[frame.len], 0, maxdlen - frame.len);

		if (!use_so_txtime &&
		    (ts.tv_sec || ts.tv_nsec) &&
		    burst_sent_count >= burst_count) {
			if (clock_nanosleep_flags == TIMER_ABSTIME)
				ts = timespec_add(ts, ts_gap);

			ret = clock_nanosleep(clockid, clock_nanosleep_flags, &ts, NULL);
			if (ret != 0 && ret != EINTR) {
				perror("clock_nanosleep");
				return 1;
			}
		}

		if (verbose) {
			printf("  %s  ", argv[optind]);

			if (verbose > 1)
				fprint_long_canframe(stdout, &frame, "\n", (verbose > 2) ? 1 : 0, maxdlen);
			else
				fprint_canframe(stdout, &frame, "\n", 1, maxdlen);
		}

		ret = do_send_one(s, &frame, mtu, polltimeout);
		if (ret)
			return 1;

		if (burst_sent_count >= burst_count)
			burst_sent_count = 0;
		burst_sent_count++;

		if (id_mode == MODE_INCREMENT)
			frame.can_id++;

		if (dlc_mode == MODE_INCREMENT) {
			incdlc++;
			incdlc %= CAN_MAX_RAW_DLC + 1;

			if (canfd && !mix)
				frame.len = can_fd_dlc2len(incdlc);
			else if (len8_dlc) {
				if (incdlc > CAN_MAX_DLEN) {
					frame.len = CAN_MAX_DLEN;
					ccf->len8_dlc = incdlc;
				} else {
					frame.len = incdlc;
					ccf->len8_dlc = 0;
				}
			} else {
				incdlc %= CAN_MAX_DLEN + 1;
				frame.len = incdlc;
			}
		}

		if (data_mode == MODE_INCREMENT) {
			incdata++;

			for (i = 0; i < 8; i++)
				frame.data[i] = incdata >> i * 8;
		}

		if (mix) {
			i = random();
			extended = i & 1;
			canfd = i & 2;
			if (canfd) {
				brs = i & 4;
				esi = i & 8;
			}
			rtr_frame = ((i & 24) == 24); /* reduce RTR frames to 1/4 */
		}
	}

	if (enobufs_count)
		printf("\nCounted %llu ENOBUFS return values on write().\n\n",
		       enobufs_count);

	close(s);

	if (signal_num)
		return 128 + signal_num;

	return 0;
}
