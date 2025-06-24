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
		.tv_sec = s,
		.tv_nsec = (s - (long)(s)) * NSEC_PER_SEC,
	};

	return timespec_normalise(ts);
}

static struct timespec ns_to_timespec(int64_t ns)
{
	struct timespec ts = {
		.tv_sec = ns / NSEC_PER_SEC,
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
	fprintf(stderr, "         -X            (generate CAN XL CAN frames)\n");
	fprintf(stderr, "         -R            (generate RTR frames)\n");
	fprintf(stderr, "         -8            (allow DLC values greater then 8 for Classic CAN frames)\n");
	fprintf(stderr, "         -m            (mix -e -f -b -E -R -X frames)\n");
	fprintf(stderr, "         -I <mode>     (CAN ID generation mode - see below)\n");
	fprintf(stderr, "         -L <mode>     (CAN data length code (dlc) generation mode - see below)\n");
	fprintf(stderr, "         -D <mode>     (CAN data (payload) generation mode - see below)\n");
	fprintf(stderr, "         -F <mode>     (CAN XL Flags generation mode - see below, no e/o mode)\n");
	fprintf(stderr, "         -S <mode>     (CAN XL SDT generation mode - see below, no e/o mode)\n");
	fprintf(stderr, "         -A <mode>     (CAN XL AF generation mode - see below, no e/o mode)\n");
	fprintf(stderr, "         -V <mode>     (CAN XL VCID generation mode - see below, no e/o mode)\n");
	fprintf(stderr, "         -p <timeout>  (poll on -ENOBUFS to write frames with <timeout> ms)\n");
	fprintf(stderr, "         -P <priority> (set socket priority using SO_PRIORITY)\n");
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

static int do_send_one(int fd, cu_t *cu, size_t len, int timeout)
{
	uint8_t control[CMSG_SPACE(sizeof(uint64_t))] = { 0 };
	struct iovec iov = {
		.iov_base = cu,
	};
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};
	ssize_t nbytes;
	int ret;

	/* CAN XL frames need real frame length for sending */
	if (len == CANXL_MTU)
		len = CANXL_HDR_SIZE + cu->xl.len;

	iov.iov_len = len;

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
	int i;

	/* Mark nibbles with * as fuzzable */
	for (i = 0; i < CANFD_MAX_DLEN && i < arglen / 2; i++) {
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
	unsigned char canxl = 0;
	unsigned char brs = 0;
	unsigned char esi = 0;
	unsigned char mix = 0;
	unsigned char id_mode = MODE_RANDOM;
	unsigned char data_mode = MODE_RANDOM;
	unsigned char dlc_mode = MODE_RANDOM;
	__u8 xl_flags = 0;
	__u8 xl_sdt = 0;
	__u32 xl_af = 0;
	__u8 xl_vcid = 0;
	unsigned char xl_flags_mode = MODE_RANDOM;
	unsigned char xl_sdt_mode = MODE_RANDOM;
	unsigned char xl_af_mode = MODE_RANDOM;
	unsigned char xl_vcid_mode = MODE_RANDOM;
	unsigned char loopback_disable = 0;
	unsigned char verbose = 0;
	unsigned char rtr_frame = 0;
	unsigned char len8_dlc = 0;
	unsigned char view = 0;
	int count = 0;
	unsigned long burst_sent_count = 0;
	int mtu, maxdlen;
	uint64_t incdata = 0;
	__u8 *data; /* base pointer for CC/FD or XL data */
	int incdlc = 0;
	int priority = -1;
	unsigned long rnd;
	unsigned char fixdata[CANFD_MAX_DLEN];
	unsigned char rand_position[CANFD_MAX_DLEN] = { 0 };

	int opt;
	int s; /* socket */

	struct sockaddr_can addr = { 0 };
	struct can_raw_vcid_options vcid_opts = {
		.flags = CAN_RAW_XL_VCID_TX_PASS,
	};
	static cu_t cu;
	int i;
	struct ifreq ifr = { 0 };
	const int enable_canfx = 1;

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

	while ((opt = getopt_long(argc, argv, "g:atefbEXR8mI:L:D:F:S:A:V:p:P:n:ixc:vh?", long_options, NULL)) != -1) {
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
			view |= CANLIB_VIEW_INDENT_SFF;
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

		case 'X':
			canxl = 1;
			break;

		case 'R':
			rtr_frame = 1;
			break;

		case '8':
			len8_dlc = 1;
			view |= CANLIB_VIEW_LEN8_DLC;
			break;

		case 'm':
			mix = 1;
			canfd = 1; /* to switch the socket into CAN FD mode */
			view |= CANLIB_VIEW_INDENT_SFF;
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
				cu.fd.can_id = strtoul(optarg, NULL, 16);
			}
			break;

		case 'L':
			if (optarg[0] == 'r') {
				dlc_mode = MODE_RANDOM;
			} else if (optarg[0] == 'i') {
				dlc_mode = MODE_INCREMENT;
			} else {
				dlc_mode = MODE_FIX;
				cu.fd.len = atoi(optarg) & 0xFF; /* is cut to 8 / 64 later */
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

		case 'F':
			if (optarg[0] == 'r') {
				xl_flags_mode = MODE_RANDOM;
			} else if (optarg[0] == 'i') {
				xl_flags_mode = MODE_INCREMENT;
			} else {
				xl_flags_mode = MODE_FIX;
				if (sscanf(optarg, "%hhx", &xl_flags) != 1) {
					printf("Bad xl_flags definition '%s'.\n", optarg);
					exit(1);
				}
			}
			break;

		case 'S':
			if (optarg[0] == 'r') {
				xl_sdt_mode = MODE_RANDOM;
			} else if (optarg[0] == 'i') {
				xl_sdt_mode = MODE_INCREMENT;
			} else {
				xl_sdt_mode = MODE_FIX;
				if (sscanf(optarg, "%hhx", &xl_sdt) != 1) {
					printf("Bad xl_sdt definition '%s'.\n", optarg);
					exit(1);
				}
			}
			break;

		case 'A':
			if (optarg[0] == 'r') {
				xl_af_mode = MODE_RANDOM;
			} else if (optarg[0] == 'i') {
				xl_af_mode = MODE_INCREMENT;
			} else {
				xl_af_mode = MODE_FIX;
				xl_af = strtoul(optarg, NULL, 16);
			}
			break;

		case 'V':
			if (optarg[0] == 'r') {
				xl_vcid_mode = MODE_RANDOM;
			} else if (optarg[0] == 'i') {
				xl_vcid_mode = MODE_INCREMENT;
			} else {
				xl_vcid_mode = MODE_FIX;
				if (sscanf(optarg, "%hhx", &xl_vcid) != 1) {
					printf("Bad xl_vcid definition '%s'.\n", optarg);
					exit(1);
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

		case 'P':
			priority = atoi(optarg);
			if (priority < 0) {
				printf("socket priority has to be >= 0\n");
				exit(1);
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

	if (verbose > 2)
		view |= CANLIB_VIEW_ASCII;

	ts_gap = double_to_timespec(gap / 1000);

	/* recognize obviously missing commandline option */
	if (id_mode == MODE_FIX && cu.fd.can_id > 0x7FF && !extended) {
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

	/*
	 * user can use tc to configure the queuing discipline (e.g. mqprio),
	 * together with SO_PRIORITY option to specify the message send from
	 * this socket should go to which queue.
	 */
	if (priority >= 0 &&
	    setsockopt(s, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority))) {
		printf("error setting SO_PRIORITY\n");
	}

	if (loopback_disable) {
		const int loopback = 0;

		setsockopt(s, SOL_CAN_RAW, CAN_RAW_LOOPBACK,
			   &loopback, sizeof(loopback));
	}

	if (canfd || canxl) {

		/* check if the frame fits into the CAN netdevice */
		if (ioctl(s, SIOCGIFMTU, &ifr) < 0) {
			perror("SIOCGIFMTU");
			return 1;
		}

		if (canfd) {
			/* ensure discrete CAN FD length values 0..8, 12, 16, 20, 24, 32, 64 */
			cu.fd.len = can_fd_dlc2len(can_fd_len2dlc(cu.fd.len));
		} else {
			/* limit fixed CAN XL data length to 64 */
			if (cu.fd.len > CANFD_MAX_DLEN)
				cu.fd.len = CANFD_MAX_DLEN;
		}

		if (canxl && (ifr.ifr_mtu < (int)CANXL_MIN_MTU)) {
			printf("CAN interface not CAN XL capable - sorry.\n");
			return 1;
		}

		if (canfd && (ifr.ifr_mtu < (int)CANFD_MTU)) {
			printf("CAN interface not CAN FD capable - sorry.\n");
			return 1;
		}

		if (ifr.ifr_mtu == (int)CANFD_MTU) {
			/* interface is ok - try to switch the socket into CAN FD mode */
			if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
				       &enable_canfx, sizeof(enable_canfx))){
				printf("error when enabling CAN FD support\n");
				return 1;
			}
		}

		if (ifr.ifr_mtu >= (int)CANXL_MIN_MTU) {
			/* interface is ok - try to switch the socket into CAN XL mode */
			if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_XL_FRAMES,
				       &enable_canfx, sizeof(enable_canfx))){
				printf("error when enabling CAN XL support\n");
				return 1;
			}
			/* try to enable the CAN XL VCID pass through mode */
			if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_XL_VCID_OPTS,
				       &vcid_opts, sizeof(vcid_opts))) {
				printf("error when enabling CAN XL VCID pass through\n");
				return 1;
			}
		}

	} else {
		/* sanitize Classical CAN 2.0 frame length */
		if (len8_dlc) {
			if (cu.cc.len > CAN_MAX_RAW_DLC)
				cu.cc.len = CAN_MAX_RAW_DLC;

			if (cu.cc.len > CAN_MAX_DLEN)
				cu.cc.len8_dlc = cu.cc.len;
		}

		if (cu.cc.len > CAN_MAX_DLEN)
			cu.cc.len = CAN_MAX_DLEN;
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
		/* clear values but preserve cu.fd.len */
		cu.fd.flags = 0;
		cu.fd.__res0 = 0;
		cu.fd.__res1 = 0;

		if (count && (--count == 0))
			running = 0;

		if (canxl) {
			mtu = CANXL_MTU;
			maxdlen = CANFD_MAX_DLEN; /* generate up to 64 byte */
			extended = 0; /* prio has only 11 bit ID content */
			data = cu.xl.data; /* fill CAN XL data */
		} else if (canfd) {
			mtu = CANFD_MTU;
			maxdlen = CANFD_MAX_DLEN;
			data = cu.fd.data; /* fill CAN CC/FD data */
			cu.fd.flags = CANFD_FDF;
			if (brs)
				cu.fd.flags |= CANFD_BRS;
			if (esi)
				cu.fd.flags |= CANFD_ESI;
		} else {
			mtu = CAN_MTU;
			maxdlen = CAN_MAX_DLEN;
			data = cu.cc.data; /* fill CAN CC/FD data */
		}

		if (id_mode == MODE_RANDOM)
			cu.fd.can_id = random();
		else if (id_mode == MODE_RANDOM_EVEN)
			cu.fd.can_id = random() & ~0x1;
		else if (id_mode == MODE_RANDOM_ODD)
			cu.fd.can_id = random() | 0x1;

		if (extended) {
			cu.fd.can_id &= CAN_EFF_MASK;
			cu.fd.can_id |= CAN_EFF_FLAG;
		} else {
			cu.fd.can_id &= CAN_SFF_MASK;
		}

		if (rtr_frame && !canfd && !canxl)
			cu.fd.can_id |= CAN_RTR_FLAG;

		if (dlc_mode == MODE_RANDOM) {
			if (canxl)
				cu.fd.len = CANXL_MIN_DLEN + (random() & 0x3F);
			else if (canfd)
				cu.fd.len = can_fd_dlc2len(random() & 0xF);
			else {
				cu.cc.len = random() & 0xF;

				if (cu.cc.len > CAN_MAX_DLEN) {
					/* generate Classic CAN len8 DLCs? */
					if (len8_dlc)
						cu.cc.len8_dlc = cu.cc.len;

					cu.cc.len = 8; /* for about 50% of the frames */
				} else {
					cu.cc.len8_dlc = 0;
				}
			}
		}

		if (data_mode == MODE_INCREMENT && !cu.cc.len)
			cu.cc.len = 1; /* min dlc value for incr. data */

		if (data_mode == MODE_RANDOM) {
			rnd = random();
			memcpy(&data[0], &rnd, 4);
			rnd = random();
			memcpy(&data[4], &rnd, 4);

			/* omit extra random number generation for CAN FD */
			if ((canfd || canxl) && cu.fd.len > 8) {
				memcpy(&data[8], &data[0], 8);
				memcpy(&data[16], &data[0], 16);
				memcpy(&data[32], &data[0], 32);
			}
		}

		if (data_mode == MODE_RANDOM_FIX) {
			int i;

			memcpy(data, fixdata, CANFD_MAX_DLEN);

			for (i = 0; i < cu.fd.len; i++) {
				if (rand_position[i] == (NIBBLE_H | NIBBLE_L)) {
					data[i] = random();
				} else if (rand_position[i] == NIBBLE_H) {
					data[i] = (data[i] & 0x0f) | (random() & 0xf0);
				} else if (rand_position[i] == NIBBLE_L) {
					data[i] = (data[i] & 0xf0) | (random() & 0x0f);
				}
			}
		}

		if (data_mode == MODE_FIX)
			memcpy(data, fixdata, CANFD_MAX_DLEN);

		/* set unused payload data to zero like the CAN driver does it on rx */
		if (cu.fd.len < maxdlen)
			memset(&data[cu.fd.len], 0, maxdlen - cu.fd.len);

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

		if (canxl) {
			/* convert some CAN FD frame content into a CAN XL frame */
			if (cu.fd.len < CANXL_MIN_DLEN) {
				cu.fd.len = CANXL_MIN_DLEN;
				data[0] = 0xCC; /* default filler */
			}
			cu.xl.len = cu.fd.len;

			rnd = random();

			if (xl_flags_mode == MODE_RANDOM) {
				cu.xl.flags = rnd & (CANXL_SEC | CANXL_RRS);
			} else if (xl_flags_mode == MODE_FIX) {
				cu.xl.flags = xl_flags;
			} else if (xl_flags_mode == MODE_INCREMENT) {
				xl_flags++;
				cu.xl.flags = (xl_flags & (CANXL_SEC | CANXL_RRS));
			}

			/* mark CAN XL frame */
			cu.xl.flags |= CANXL_XLF;

			if (xl_sdt_mode == MODE_RANDOM) {
				cu.xl.sdt = rnd & 0xFF;
			} else if (xl_sdt_mode == MODE_FIX) {
				cu.xl.sdt = xl_sdt;
			} else if (xl_sdt_mode == MODE_INCREMENT) {
				xl_sdt++;
				cu.xl.sdt = xl_sdt;
			}

			if (xl_af_mode == MODE_RANDOM) {
				cu.xl.af = rnd;
			} else if (xl_af_mode == MODE_FIX) {
				cu.xl.af = xl_af;
			} else if (xl_af_mode == MODE_INCREMENT) {
				xl_af++;
				cu.xl.af = xl_af;
			}

			if (xl_vcid_mode == MODE_RANDOM) {
				cu.xl.prio |= rnd & CANXL_VCID_MASK;
			} else if (xl_vcid_mode == MODE_FIX) {
				cu.xl.prio |= xl_vcid << CANXL_VCID_OFFSET;
			} else if (xl_vcid_mode == MODE_INCREMENT) {
				xl_vcid++;
				cu.xl.prio |= xl_vcid << CANXL_VCID_OFFSET;
			}
		}

		if (verbose) {
			static char afrbuf[AFRSZ]; /* ASCII CAN frame buffer size */

			printf("  %s  ", argv[optind]);

			if (verbose > 1)
				snprintf_long_canframe(afrbuf, sizeof(afrbuf), &cu, view);
			else
				snprintf_canframe(afrbuf, sizeof(afrbuf), &cu, 1);

			printf("%s\n", afrbuf);
		}

		ret = do_send_one(s, &cu, mtu, polltimeout);
		if (ret)
			return 1;

		if (burst_sent_count >= burst_count)
			burst_sent_count = 0;
		burst_sent_count++;

		/* restore some CAN FD frame content from CAN XL frame */
		if (canxl)
			cu.fd.len = cu.xl.len;

		if (id_mode == MODE_INCREMENT)
			cu.cc.can_id++;

		if (dlc_mode == MODE_INCREMENT) {
			incdlc++;
			incdlc %= CAN_MAX_RAW_DLC + 1;

			if ((canfd || canxl) && !mix)
				cu.fd.len = can_fd_dlc2len(incdlc);
			else if (len8_dlc) {
				if (incdlc > CAN_MAX_DLEN) {
					cu.cc.len = CAN_MAX_DLEN;
					cu.cc.len8_dlc = incdlc;
				} else {
					cu.cc.len = incdlc;
					cu.cc.len8_dlc = 0;
				}
			} else {
				incdlc %= CAN_MAX_DLEN + 1;
				cu.fd.len = incdlc;
			}
		}

		if (data_mode == MODE_INCREMENT) {
			incdata++;

			for (i = 0; i < 8; i++)
				data[i] = incdata >> i * 8;
		}

		if (mix) {
			i = random();
			extended = i & 1;
			canfd = i & 2;
			if (canfd) {
				brs = i & 4;
				esi = i & 8;
			}
			/* generate CAN XL traffic if the interface is capable */
			if (ifr.ifr_mtu >= (int)CANXL_MIN_MTU)
				canxl = ((i & 96) == 96);

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
