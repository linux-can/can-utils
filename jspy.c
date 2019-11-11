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

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>

#include <unistd.h>
#include <getopt.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "libj1939.h"

/*
 * getopt
 */
static const char help_msg[] =
	"jspy: An SAE J1939 spy utility" "\n"
	"Usage: jspy [OPTION...] [[IFACE:][NAME|SA][,PGN]]" "\n"
	"\n"
	"  -v, --verbose		Increase verbosity" "\n"
	"  -P, --promisc		Run in promiscuous mode" "\n"
	"			(= receive traffic not for this ECU)" "\n"
	"  -b, --block=SIZE	Use a receive buffer of SIZE (default 1024)" "\n"
	"  -t, --time[=a|d|z|A]	Show time: (a)bsolute, (d)elta, (z)ero, (A)bsolute w date" "\n"
	;

#ifdef _GNU_SOURCE
static struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "verbose", no_argument, NULL, 'v', },

	{ "promisc", no_argument, NULL, 'P', },
	{ "block", required_argument, NULL, 'b', },
	{ "time", optional_argument, NULL, 't', },
	{ },
};
#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif
static const char optstring[] = "vPb:t::?";

/*
 * static variables
 */
static struct {
	int verbose;
	struct sockaddr_can addr;
	int promisc;
	int time;
	int pkt_len;
} s = {
	.pkt_len = 1024,
	.addr.can_addr.j1939 = {
		.name = J1939_NO_NAME,
		.addr = J1939_NO_ADDR,
		.pgn = J1939_NO_PGN,
	},
};

/*
 * useful buffers
 */
static const int ival_1 = 1;

static char ctrlmsg[
	  CMSG_SPACE(sizeof(struct timeval))
	+ CMSG_SPACE(sizeof(uint8_t)) /* dest addr */
	+ CMSG_SPACE(sizeof(uint64_t)) /* dest name */
	+ CMSG_SPACE(sizeof(uint8_t)) /* priority */
	];
static struct iovec iov;
static struct msghdr msg;
static struct cmsghdr *cmsg;
static uint8_t *buf;

/*
 * program
 */
int main(int argc, char **argv)
{
	int ret, sock, j, opt;
	unsigned int len;
	struct timeval tref, tdut, ttmp;
	struct sockaddr_can src;
	struct j1939_filter filt;
	int filter = 0;
	uint8_t priority, dst_addr;
	uint64_t dst_name;
	long recvflags;

	/* argument parsing */
	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'v':
		++s.verbose;
		break;
	case 'b':
		s.pkt_len = strtoul(optarg, 0, 0);
		break;
	case 'P':
		++s.promisc;
		break;
	case 't':
		if (optarg) {
			if (!strchr("adzA", optarg[0]))
				err(1, "unknown time option '%c'", optarg[0]);
			s.time = optarg[0];
		} else {
			s.time = 'z';
		}
		break;
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}
	if (argv[optind]) {
		optarg = argv[optind];
		ret = libj1939_str2addr(optarg, 0, &s.addr);
		if (ret < 0) {
			err(0, "bad URI %s", optarg);
			return 1;
		}
	}

	buf = malloc(s.pkt_len);
	if (!buf)
		err(1, "malloc %u", s.pkt_len);

	/* setup socket */
	sock = socket(PF_CAN, SOCK_DGRAM, CAN_J1939);
	if (sock < 0)
		err(1, "socket(can, dgram, j1939)");

	memset(&filt, 0, sizeof(filt));
	if (s.addr.can_addr.j1939.name) {
		filt.name = s.addr.can_addr.j1939.name;
		filt.name_mask = ~0ULL;
		++filter;
	}
	if (s.addr.can_addr.j1939.addr < 0xff) {
		filt.addr = s.addr.can_addr.j1939.addr;
		filt.addr_mask = ~0;
		++filter;
	}
	if (s.addr.can_addr.j1939.pgn <= J1939_PGN_MAX) {
		filt.pgn = s.addr.can_addr.j1939.pgn;
		filt.pgn_mask = ~0;
		++filter;
	}
	if (filter) {
		ret = setsockopt(sock, SOL_CAN_J1939, SO_J1939_FILTER, &filt, sizeof(filt));
		if (ret < 0)
			err(1, "setsockopt filter");
	}

	if (s.promisc) {
		ret = setsockopt(sock, SOL_CAN_J1939, SO_J1939_PROMISC, &ival_1, sizeof(ival_1));
		if (ret < 0)
			err(1, "setsockopt promisc");
	}

	if (s.time) {
		ret = setsockopt(sock, SOL_SOCKET, SO_TIMESTAMP, &ival_1, sizeof(ival_1));
		if (ret < 0)
			err(1, "setsockopt timestamp");
	}
	ret = setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &s.pkt_len, sizeof(s.pkt_len));
		if (ret < 0)
			err(1, "setsockopt rcvbuf %u", s.pkt_len);

	/* bind(): to default, only ifindex is used. */
	memset(&src, 0, sizeof(src));
	src.can_ifindex = s.addr.can_ifindex;
	src.can_family = AF_CAN;
	src.can_addr.j1939.name = J1939_NO_NAME;
	src.can_addr.j1939.addr = J1939_NO_ADDR;
	src.can_addr.j1939.pgn = J1939_NO_PGN;
	ret = bind(sock, (void *)&src, sizeof(src));
	if (ret < 0)
		err(1, "bind(%s)", argv[1]);

	/* these settings are static and can be held out of the hot path */
	iov.iov_base = &buf[0];
	msg.msg_name = &src;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &ctrlmsg;

	memset(&tref, 0, sizeof(tref));
	if (s.verbose)
		err(0, "listening");
	while (1) {
		/* these settings may be modified by recvmsg() */
		iov.iov_len = s.pkt_len;
		msg.msg_namelen = sizeof(src);
		msg.msg_controllen = sizeof(ctrlmsg);
		msg.msg_flags = 0;

		ret = recvmsg(sock, &msg, 0);
		//ret = recvfrom(buf, s.pkt_len, 0, (void *)&addr, &len);
		if (ret < 0) {
			switch (errno) {
			case ENETDOWN:
				err(0, "ifindex %i", s.addr.can_ifindex);
				continue;
			case EINTR:
				continue;
			default:
				err(1, "recvmsg(ifindex %i)", s.addr.can_ifindex);
				break;
			}
		}
		len = ret;
		recvflags = 0;
		dst_addr = 0;
		priority = 0;
		for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
			switch (cmsg->cmsg_level) {
			case SOL_SOCKET:
				if (cmsg->cmsg_type == SCM_TIMESTAMP) {
					memcpy(&tdut, CMSG_DATA(cmsg), sizeof(tdut));
					recvflags |= 1 << cmsg->cmsg_type;
				}
				break;
			case SOL_CAN_J1939:
				recvflags |= 1 << cmsg->cmsg_type;
				if (cmsg->cmsg_type == SCM_J1939_DEST_ADDR)
					dst_addr = *CMSG_DATA(cmsg);
				else if (cmsg->cmsg_type == SCM_J1939_DEST_NAME)
					memcpy(&dst_name, CMSG_DATA(cmsg), cmsg->cmsg_len - CMSG_LEN(0));
				else if (cmsg->cmsg_type == SCM_J1939_PRIO)
					priority = *CMSG_DATA(cmsg);
				break;
			}

		}
		if (recvflags & (1 << SCM_TIMESTAMP)) {
			if ('z' == s.time) {
				if (!tref.tv_sec)
					tref = tdut;
				timersub(&tdut, &tref, &ttmp);
				tdut = ttmp;
				goto abs_time;
			} else if ('d' == s.time) {
				timersub(&tdut, &tref, &ttmp);
				tref = tdut;
				tdut = ttmp;
				goto abs_time;
			} else if ('a' == s.time) {
				abs_time:
				printf("(%lu.%04lu)", tdut.tv_sec, tdut.tv_usec / 100);
			} else if ('A' == s.time) {
				struct tm tm;
				tm = *localtime(&tdut.tv_sec);
				printf("(%04u%02u%02uT%02u%02u%02u.%04lu)",
					tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
					tm.tm_hour, tm.tm_min, tm.tm_sec,
					tdut.tv_usec/100);
			}
		}
		printf(" %s ", libj1939_addr2str(&src));
		if (recvflags & (1 << SCM_J1939_DEST_NAME))
			printf("%016llx ", (unsigned long long)dst_name);
		else if (recvflags & (1 << SCM_J1939_DEST_ADDR))
			printf("%02x ", dst_addr);
		else
			printf("- ");
		printf("!%u ", priority);

		printf("[%i%s]", len, (msg.msg_flags & MSG_TRUNC) ? "..." : "");
		for (j = 0; j < len; ) {
			int end = j + 4;
			if (end > len)
				end = len;
			printf(" ");
			for (; j < end; ++j)
				printf("%02x", buf[j]);
		}
		printf("\n");
	}

	free(buf);
	return 0;
}

