/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011 EIA Electronics
 *
 * Authors:
 * Kurt Van Dijck <kurt.van.dijck@eia.be>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <err.h>
#include <getopt.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "libj1939.h"

/*
 * getopt
 */
static const char help_msg[] =
	"j1939sr: An SAE J1939 send/recv utility" "\n"
	"Usage: j1939sr [OPTION...] SOURCE [DEST]" "\n"
	"Options:\n"
	"  -v, --verbose		Increase verbosity" "\n"
	"  -p, --priority=VAL	J1939 priority (0..7, default 6)" "\n"
	"  -S, --serialize	Strictly serialize outgoing packets" "\n"
	"  -s, --size		Packet size, default autodetected" "\n"
	"\n"
	"  SOURCE	[IFACE:][NAME|SA][,PGN]" "\n"
	"  DEST			[NAME|SA]" "\n"
	;

#ifdef _GNU_SOURCE
static struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "verbose", no_argument, NULL, 'v', },

	{ "priority", required_argument, NULL, 'p', },
	{ "size", required_argument, NULL, 's', },
	{ "serialize", no_argument, NULL, 'S', },
	{ },
};
#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif
static const char optstring[] = "vp:s:S?";

/*
 * static variables: configurations
 */
static struct {
	int verbose;
	int sendflags; /* flags for sendto() */
	int pkt_len;
	int priority;
	int defined;
	#define DEF_SRC		1
	#define DEF_DST		2
	#define DEF_PRIO	4
	struct sockaddr_can src, dst;
} s = {
	.priority = 6,
	.src.can_addr.j1939 = {
		.name = J1939_NO_NAME,
		.addr = J1939_NO_ADDR,
		.pgn = J1939_NO_PGN,
	},
	.dst.can_addr.j1939 = {
		.name = J1939_NO_NAME,
		.addr = J1939_NO_ADDR,
		.pgn = J1939_NO_PGN,
	},
};

int main(int argc, char **argv)
{

	int ret, sock, opt;
	unsigned int len;
	struct pollfd pfd[2];
	uint8_t *buf;

#ifdef _GNU_SOURCE
	program_invocation_name = program_invocation_short_name;
#endif
	/* argument parsing */
	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
		switch (opt) {
		case 'v':
			++s.verbose;
			break;
		case 's':
			s.pkt_len = strtoul(optarg, 0, 0);
			if (!s.pkt_len)
				err(1, "packet size of %s", optarg);
			break;
		case 'p':
			s.priority = strtoul(optarg, 0, 0);
			s.defined |= DEF_PRIO;
			break;
		case 'S':
			s.sendflags |= MSG_SYN;
			break;
		default:
			fputs(help_msg, stderr);
			exit(1);
			break;
		}

	if (argv[optind]) {
		optarg = argv[optind++];
		ret = libj1939_str2addr(optarg, 0, &s.src);
		if (ret < 0)
			err(1, "bad address spec [%s]", optarg);
		s.defined |= DEF_SRC;
	}
	if (argv[optind]) {
		optarg = argv[optind++];
		ret = libj1939_str2addr(optarg, 0, &s.dst);
		if (ret < 0)
			err(1, "bad address spec [%s]", optarg);
		s.defined |= DEF_DST;
	}

	if (!s.pkt_len) {
		struct stat st;

		if (fstat(STDIN_FILENO, &st) < 0)
			err(1, "stat stdin, could not determine buffer size");
		s.pkt_len = st.st_size ?: 1024;
	}

	/* prepare */
	buf = malloc(s.pkt_len);
	if (!buf)
		err(1, "malloc %u", s.pkt_len);

	sock = socket(PF_CAN, SOCK_DGRAM, CAN_J1939);
	if (sock < 0)
		err(1, "socket(can, dgram, j1939)");

	if (s.defined & DEF_PRIO) {
		ret = setsockopt(sock, SOL_CAN_J1939, SO_J1939_SEND_PRIO, &s.priority, sizeof(s.priority));
		if (ret < 0)
			err(1, "setsockopt priority");
	}
	if (s.defined & DEF_SRC) {
		s.src.can_family = AF_CAN;
		ret = bind(sock, (void *)&s.src, sizeof(s.src));
		if (ret < 0)
			err(1, "bind(%s), %i", libj1939_addr2str(&s.src), -errno);
	}

	if (s.defined & DEF_DST) {
		s.dst.can_family = AF_CAN;
		ret = connect(sock, (void *)&s.dst, sizeof(s.dst));
		if (ret < 0)
			err(1, "connect(%s), %i", libj1939_addr2str(&s.dst), -errno);
	}

	pfd[0].fd = STDIN_FILENO;
	pfd[0].events = POLLIN;
	pfd[1].fd = sock;
	pfd[1].events = POLLIN;

	/* run */
	while (1) {
		ret = poll(pfd, sizeof(pfd)/sizeof(pfd[0]), -1);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			err(1, "poll()");
		}
		if (pfd[0].revents) {
			ret = read(pfd[0].fd, buf, s.pkt_len);
			if (ret < 0)
				err(1, "read(stdin)");
			if (!ret)
				break;
			len = ret;
			do {
				ret = send(pfd[1].fd, buf, len, s.sendflags);
				if (ret < 0 && errno != ENOBUFS)
					err(1, "write(%s)", libj1939_addr2str(&s.src));
			} while (ret < 0);
		}
		if (pfd[1].revents) {
			ret = read(pfd[1].fd, buf, s.pkt_len);
			if (ret < 0) {
				ret = errno;
				err(0, "read(%s)", libj1939_addr2str(&s.dst));
				switch (ret) {
				case EHOSTDOWN:
					break;
				default:
					exit(1);
				}
			} else {
				if (write(STDOUT_FILENO, buf, ret) < 0)
					err(1, "write(stdout)");
			}
		}
	}

	free(buf);
	return 0;
}

