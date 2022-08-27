/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * candump.c
 *
 * Copyright (c) 2002-2009 Volkswagen Group Electronic Research
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
#include <libgen.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include "terminal.h"
#include "lib.h"

/* for hardware timestamps - since Linux 2.6.30 */
#ifndef SO_TIMESTAMPING
#define SO_TIMESTAMPING 37
#endif

/* from #include <linux/net_tstamp.h> - since Linux 2.6.30 */
#define SOF_TIMESTAMPING_SOFTWARE (1 << 4)
#define SOF_TIMESTAMPING_RX_SOFTWARE (1 << 3)
#define SOF_TIMESTAMPING_RAW_HARDWARE (1 << 6)
#define TIMESTAMPSZ 50 /* string 'absolute with date' requires max 49 bytes */

#define MAXSOCK 16    /* max. number of CAN interfaces given on the cmdline */
#define MAXIFNAMES 30 /* size of receive name index to omit ioctls */
#define MAXCOL 6      /* number of different colors for colorized output */
#define ANYDEV "any"  /* name of interface to receive from any CAN interface */
#define ANL "\r\n"    /* newline in ASC mode */

#define SILENT_INI 42 /* detect user setting on commandline */
#define SILENT_OFF 0  /* no silent mode */
#define SILENT_ANI 1  /* silent mode with animation */
#define SILENT_ON 2   /* silent mode (completely silent) */

#define BOLD ATTBOLD
#define RED (ATTBOLD FGRED)
#define GREEN (ATTBOLD FGGREEN)
#define YELLOW (ATTBOLD FGYELLOW)
#define BLUE (ATTBOLD FGBLUE)
#define MAGENTA (ATTBOLD FGMAGENTA)
#define CYAN (ATTBOLD FGCYAN)

static const char col_on[MAXCOL][19] = { BLUE, RED, GREEN, BOLD, MAGENTA, CYAN };
static const char col_off[] = ATTRESET;

struct if_info { /* bundled information per open socket */
	int s; /* socket */
	char *cmdlinename;
	__u32 dropcnt;
	__u32 last_dropcnt;
};
static struct if_info sock_info[MAXSOCK];

static char devname[MAXIFNAMES][IFNAMSIZ+1];
static int dindex[MAXIFNAMES];
static int max_devname_len; /* to prevent frazzled device name output */
static const int canfd_on = 1;

#define MAXANI 4
static const char anichar[MAXANI] = { '|', '/', '-', '\\' };
static const char extra_m_info[4][4] = { "- -", "B -", "- E", "B E" };

extern int optind, opterr, optopt;

static volatile int running = 1;

static void print_usage(char *prg)
{
	fprintf(stderr, "%s - dump CAN bus traffic.\n", prg);
	fprintf(stderr, "\nUsage: %s [options] <CAN interface>+\n", prg);
	fprintf(stderr, "  (use CTRL-C to terminate %s)\n\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -t <type>   (timestamp: (a)bsolute/(d)elta/(z)ero/(A)bsolute w date)\n");
	fprintf(stderr, "         -H          (read hardware timestamps instead of system timestamps)\n");
	fprintf(stderr, "         -c          (increment color mode level)\n");
	fprintf(stderr, "         -i          (binary output - may exceed 80 chars/line)\n");
	fprintf(stderr, "         -a          (enable additional ASCII output)\n");
	fprintf(stderr, "         -S          (swap byte order in printed CAN data[] - marked with '%c' )\n", SWAP_DELIMITER);
	fprintf(stderr, "         -s <level>  (silent mode - %d: off (default) %d: animation %d: silent)\n", SILENT_OFF, SILENT_ANI, SILENT_ON);
	fprintf(stderr, "         -l          (log CAN-frames into file. Sets '-s %d' by default)\n", SILENT_ON);
	fprintf(stderr, "         -f <fname>  (log CAN-frames into file <fname>. Sets '-s %d' by default)\n", SILENT_ON);
	fprintf(stderr, "         -L          (use log file format on stdout)\n");
	fprintf(stderr, "         -n <count>  (terminate after reception of <count> CAN frames)\n");
	fprintf(stderr, "         -r <size>   (set socket receive buffer to <size>)\n");
	fprintf(stderr, "         -D          (Don't exit if a \"detected\" can device goes down)\n");
	fprintf(stderr, "         -d          (monitor dropped CAN frames)\n");
	fprintf(stderr, "         -e          (dump CAN error frames in human-readable format)\n");
	fprintf(stderr, "         -8          (display raw DLC values in {} for Classical CAN)\n");
	fprintf(stderr, "         -x          (print extra message infos, rx/tx brs esi)\n");
	fprintf(stderr, "         -T <msecs>  (terminate after <msecs> if no frames were received)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Up to %d CAN interfaces with optional filter sets can be specified\n", MAXSOCK);
	fprintf(stderr, "on the commandline in the form: <ifname>[,filter]*\n");
	fprintf(stderr, "\nFilters:\n");
	fprintf(stderr, "  Comma separated filters can be specified for each given CAN interface:\n");
	fprintf(stderr, "    <can_id>:<can_mask>\n         (matches when <received_can_id> & mask == can_id & mask)\n");
	fprintf(stderr, "    <can_id>~<can_mask>\n         (matches when <received_can_id> & mask != can_id & mask)\n");
	fprintf(stderr, "    #<error_mask>\n         (set error frame filter, see include/linux/can/error.h)\n");
	fprintf(stderr, "    [j|J]\n         (join the given CAN filters - logical AND semantic)\n");
	fprintf(stderr, "\nCAN IDs, masks and data content are given and expected in hexadecimal values.\n");
	fprintf(stderr, "When the can_id is 8 digits long the CAN_EFF_FLAG is set for 29 bit EFF format.\n");
	fprintf(stderr, "Without any given filter all data frames are received ('0:0' default filter).\n");
	fprintf(stderr, "\nUse interface name '%s' to receive from all CAN interfaces.\n", ANYDEV);
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "%s -c -c -ta can0,123:7FF,400:700,#000000FF can2,400~7F0 can3 can8\n\n", prg);
	fprintf(stderr, "%s -l any,0~0,#FFFFFFFF\n         (log only error frames but no(!) data frames)\n", prg);
	fprintf(stderr, "%s -l any,0:0,#FFFFFFFF\n         (log error frames and also all data frames)\n", prg);
	fprintf(stderr, "%s vcan2,12345678:DFFFFFFF\n         (match only for extended CAN ID 12345678)\n", prg);
	fprintf(stderr, "%s vcan2,123:7FF\n         (matches CAN ID 123 - including EFF and RTR frames)\n", prg);
	fprintf(stderr, "%s vcan2,123:C00007FF\n         (matches CAN ID 123 - only SFF and non-RTR frames)\n", prg);
	fprintf(stderr, "\n");
}

static void sigterm(int signo)
{
	running = 0;
}

static int idx2dindex(int ifidx, int socket)
{

	int i;
	struct ifreq ifr;

	for (i = 0; i < MAXIFNAMES; i++) {
		if (dindex[i] == ifidx)
			return i;
	}

	/* create new interface index cache entry */

	/* remove index cache zombies first */
	for (i = 0; i < MAXIFNAMES; i++) {
		if (dindex[i]) {
			ifr.ifr_ifindex = dindex[i];
			if (ioctl(socket, SIOCGIFNAME, &ifr) < 0)
				dindex[i] = 0;
		}
	}

	for (i = 0; i < MAXIFNAMES; i++)
		if (!dindex[i]) /* free entry */
			break;

	if (i == MAXIFNAMES) {
		fprintf(stderr, "Interface index cache only supports %d interfaces.\n",
			MAXIFNAMES);
		exit(1);
	}

	dindex[i] = ifidx;

	ifr.ifr_ifindex = ifidx;
	if (ioctl(socket, SIOCGIFNAME, &ifr) < 0)
		perror("SIOCGIFNAME");

	if (max_devname_len < (int)strlen(ifr.ifr_name))
		max_devname_len = strlen(ifr.ifr_name);

	strcpy(devname[i], ifr.ifr_name);

#ifdef DEBUG
	printf("new index %d (%s)\n", i, devname[i]);
#endif

	return i;
}

static inline void sprint_timestamp(const char timestamp, const struct timeval *tv,
				    struct timeval *const last_tv, char *ts_buffer)
{
	switch (timestamp) {

	case 'a': /* absolute with timestamp */
		sprintf(ts_buffer, "(%010lu.%06lu) ", tv->tv_sec, tv->tv_usec);
		break;

	case 'A': /* absolute with date */
	{
		struct tm tm;
		char timestring[25];

		tm = *localtime(&tv->tv_sec);
		strftime(timestring, 24, "%Y-%m-%d %H:%M:%S", &tm);
		sprintf(ts_buffer, "(%s.%06lu) ", timestring, tv->tv_usec);
	}
	break;

	case 'd': /* delta */
	case 'z': /* starting with zero */
	{
		struct timeval diff;

		if (last_tv->tv_sec == 0)   /* first init */
			*last_tv = *tv;
		diff.tv_sec  = tv->tv_sec - last_tv->tv_sec;
		diff.tv_usec = tv->tv_usec - last_tv->tv_usec;
		if (diff.tv_usec < 0)
			diff.tv_sec--, diff.tv_usec += 1000000;
		if (diff.tv_sec < 0)
			diff.tv_sec = diff.tv_usec = 0;
		sprintf(ts_buffer, "(%03lu.%06lu) ", diff.tv_sec, diff.tv_usec);

		if (timestamp == 'd')
			*last_tv = *tv; /* update for delta calculation */
	}
	break;

	default: /* no timestamp output */
		break;
	}
}

static inline void print_timestamp(const char timestamp, const struct timeval *tv,
				   struct timeval *const last_tv)
{
	static char buffer[TIMESTAMPSZ];

	sprint_timestamp(timestamp, tv, last_tv, buffer);
	printf("%s", buffer);
}

int main(int argc, char **argv)
{
	int fd_epoll;
	struct epoll_event events_pending[MAXSOCK];
	struct epoll_event event_setup = {
		.events = EPOLLIN, /* prepare the common part */
	};
	unsigned char timestamp = 0;
	unsigned char logtimestamp = 'a';
	unsigned char hwtimestamp = 0;
	unsigned char down_causes_exit = 1;
	unsigned char dropmonitor = 0;
	unsigned char extra_msg_info = 0;
	unsigned char silent = SILENT_INI;
	unsigned char silentani = 0;
	unsigned char color = 0;
	unsigned char view = 0;
	unsigned char log = 0;
	unsigned char logfrmt = 0;
	int count = 0;
	int rcvbuf_size = 0;
	int opt, num_events;
	int currmax, numfilter;
	int join_filter;
	char *ptr, *nptr;
	struct sockaddr_can addr;
	char ctrlmsg[CMSG_SPACE(sizeof(struct timeval)) +
		     CMSG_SPACE(3 * sizeof(struct timespec)) +
		     CMSG_SPACE(sizeof(__u32))];
	struct iovec iov;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct can_filter *rfilter;
	can_err_mask_t err_mask;
	struct canfd_frame frame;
	int nbytes, i, maxdlen;
	struct ifreq ifr;
	struct timeval tv, last_tv;
	int timeout_ms = -1; /* default to no timeout */
	FILE *logfile = NULL;
	char *logname = NULL;

	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);
	signal(SIGINT, sigterm);

	last_tv.tv_sec = 0;
	last_tv.tv_usec = 0;

	while ((opt = getopt(argc, argv, "t:HciaSs:lDdxLf:n:r:he8T:?")) != -1) {
		switch (opt) {
		case 't':
			timestamp = optarg[0];
			logtimestamp = optarg[0];
			if ((timestamp != 'a') && (timestamp != 'A') &&
			    (timestamp != 'd') && (timestamp != 'z')) {
				fprintf(stderr, "%s: unknown timestamp mode '%c' - ignored\n",
					basename(argv[0]), optarg[0]);
				timestamp = 0;
			}
			if ((logtimestamp != 'a') && (logtimestamp != 'z')) {
				logtimestamp = 'a';
			}
			break;

		case 'H':
			hwtimestamp = 1;
			break;

		case 'c':
			color++;
			break;

		case 'i':
			view |= CANLIB_VIEW_BINARY;
			break;

		case 'a':
			view |= CANLIB_VIEW_ASCII;
			break;

		case 'S':
			view |= CANLIB_VIEW_SWAP;
			break;

		case 'e':
			view |= CANLIB_VIEW_ERROR;
			break;

		case '8':
			view |= CANLIB_VIEW_LEN8_DLC;
			break;

		case 's':
			silent = atoi(optarg);
			if (silent > SILENT_ON) {
				print_usage(basename(argv[0]));
				exit(1);
			}
			break;

		case 'l':
			log = 1;
			break;

		case 'D':
			down_causes_exit = 0;
			break;

		case 'd':
			dropmonitor = 1;
			break;

		case 'x':
			extra_msg_info = 1;
			break;

		case 'L':
			logfrmt = 1;
			break;

		case 'f':
			logname = optarg;
			log = 1;
			break;

		case 'n':
			count = atoi(optarg);
			if (count < 1) {
				print_usage(basename(argv[0]));
				exit(1);
			}
			break;

		case 'r':
			rcvbuf_size = atoi(optarg);
			if (rcvbuf_size < 1) {
				print_usage(basename(argv[0]));
				exit(1);
			}
			break;

		case 'T':
			errno = 0;
			timeout_ms = strtol(optarg, NULL, 0);
			if (errno != 0) {
				print_usage(basename(argv[0]));
				exit(1);
			}
			break;
		default:
			print_usage(basename(argv[0]));
			exit(1);
			break;
		}
	}

	if (optind == argc) {
		print_usage(basename(argv[0]));
		exit(0);
	}

	if (logfrmt && view) {
		fprintf(stderr, "Log file format selected: Please disable ASCII/BINARY/SWAP/RAWDLC options!\n");
		exit(0);
	}

	/* "-f -"  is equal to "-L" (print logfile format on stdout) */
	if (log && logname && strcmp("-", logname) == 0) {
		log = 0; /* no logging into a file */
		logfrmt = 1; /* print logformat output to stdout */
	}

	if (silent == SILENT_INI) {
		if (log) {
			fprintf(stderr, "Disabled standard output while logging.\n");
			silent = SILENT_ON; /* disable output on stdout */
		} else
			silent = SILENT_OFF; /* default output */
	}

	currmax = argc - optind; /* find real number of CAN devices */

	if (currmax > MAXSOCK) {
		fprintf(stderr, "More than %d CAN devices given on commandline!\n", MAXSOCK);
		return 1;
	}

	fd_epoll = epoll_create(1);
	if (fd_epoll < 0) {
		perror("epoll_create");
		return 1;
	}

	for (i = 0; i < currmax; i++) {
		struct if_info* obj = &sock_info[i];
		ptr = argv[optind+i];
		nptr = strchr(ptr, ',');

#ifdef DEBUG
		printf("open %d '%s'.\n", i, ptr);
#endif

		obj->s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
		if (obj->s < 0) {
			perror("socket");
			return 1;
		}

		event_setup.data.ptr = obj; /* remember the instance as private data */
		if (epoll_ctl(fd_epoll, EPOLL_CTL_ADD, obj->s, &event_setup)) {
			perror("failed to add socket to epoll");
			return 1;
		}

		obj->cmdlinename = ptr; /* save pointer to cmdline name of this socket */

		if (nptr)
			nbytes = nptr - ptr;  /* interface name is up the first ',' */
		else
			nbytes = strlen(ptr); /* no ',' found => no filter definitions */

		if (nbytes >= IFNAMSIZ) {
			fprintf(stderr, "name of CAN device '%s' is too long!\n", ptr);
			return 1;
		}

		if (nbytes > max_devname_len)
			max_devname_len = nbytes; /* for nice printing */

		addr.can_family = AF_CAN;

		memset(&ifr.ifr_name, 0, sizeof(ifr.ifr_name));
		strncpy(ifr.ifr_name, ptr, nbytes);

#ifdef DEBUG
		printf("using interface name '%s'.\n", ifr.ifr_name);
#endif

		if (strcmp(ANYDEV, ifr.ifr_name) != 0) {
			if (ioctl(obj->s, SIOCGIFINDEX, &ifr) < 0) {
				perror("SIOCGIFINDEX");
				exit(1);
			}
			addr.can_ifindex = ifr.ifr_ifindex;
		} else
			addr.can_ifindex = 0; /* any can interface */

		if (nptr) {

			/* found a ',' after the interface name => check for filters */

			/* determine number of filters to alloc the filter space */
			numfilter = 0;
			ptr = nptr;
			while (ptr) {
				numfilter++;
				ptr++; /* hop behind the ',' */
				ptr = strchr(ptr, ','); /* exit condition */
			}

			rfilter = malloc(sizeof(struct can_filter) * numfilter);
			if (!rfilter) {
				fprintf(stderr, "Failed to create filter space!\n");
				return 1;
			}

			numfilter = 0;
			err_mask = 0;
			join_filter = 0;

			while (nptr) {

				ptr = nptr + 1; /* hop behind the ',' */
				nptr = strchr(ptr, ','); /* update exit condition */

				if (sscanf(ptr, "%x:%x",
					   &rfilter[numfilter].can_id,
					   &rfilter[numfilter].can_mask) == 2) {
					rfilter[numfilter].can_mask &= ~CAN_ERR_FLAG;
					if (*(ptr + 8) == ':')
						rfilter[numfilter].can_id |= CAN_EFF_FLAG;
					numfilter++;
				} else if (sscanf(ptr, "%x~%x",
						  &rfilter[numfilter].can_id,
						  &rfilter[numfilter].can_mask) == 2) {
					rfilter[numfilter].can_id |= CAN_INV_FILTER;
					rfilter[numfilter].can_mask &= ~CAN_ERR_FLAG;
					if (*(ptr + 8) == '~')
						rfilter[numfilter].can_id |= CAN_EFF_FLAG;
					numfilter++;
				} else if (*ptr == 'j' || *ptr == 'J') {
					join_filter = 1;
				} else if (sscanf(ptr, "#%x", &err_mask) != 1) {
					fprintf(stderr, "Error in filter option parsing: '%s'\n", ptr);
					return 1;
				}
			}

			if (err_mask)
				setsockopt(obj->s, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
					   &err_mask, sizeof(err_mask));

			if (join_filter && setsockopt(obj->s, SOL_CAN_RAW, CAN_RAW_JOIN_FILTERS,
						      &join_filter, sizeof(join_filter)) < 0) {
				perror("setsockopt CAN_RAW_JOIN_FILTERS not supported by your Linux Kernel");
				return 1;
			}

			if (numfilter)
				setsockopt(obj->s, SOL_CAN_RAW, CAN_RAW_FILTER,
					   rfilter, numfilter * sizeof(struct can_filter));

			free(rfilter);

		} /* if (nptr) */

		/* try to switch the socket into CAN FD mode */
		setsockopt(obj->s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &canfd_on, sizeof(canfd_on));

		if (rcvbuf_size) {
			int curr_rcvbuf_size;
			socklen_t curr_rcvbuf_size_len = sizeof(curr_rcvbuf_size);

			/* try SO_RCVBUFFORCE first, if we run with CAP_NET_ADMIN */
			if (setsockopt(obj->s, SOL_SOCKET, SO_RCVBUFFORCE,
				       &rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
#ifdef DEBUG
				printf("SO_RCVBUFFORCE failed so try SO_RCVBUF ...\n");
#endif
				if (setsockopt(obj->s, SOL_SOCKET, SO_RCVBUF,
					       &rcvbuf_size, sizeof(rcvbuf_size)) < 0) {
					perror("setsockopt SO_RCVBUF");
					return 1;
				}

				if (getsockopt(obj->s, SOL_SOCKET, SO_RCVBUF,
					       &curr_rcvbuf_size, &curr_rcvbuf_size_len) < 0) {
					perror("getsockopt SO_RCVBUF");
					return 1;
				}

				/* Only print a warning the first time we detect the adjustment */
				/* n.b.: The wanted size is doubled in Linux in net/sore/sock.c */
				if (!i && curr_rcvbuf_size < rcvbuf_size * 2)
					fprintf(stderr, "The socket receive buffer size was "
						"adjusted due to /proc/sys/net/core/rmem_max.\n");
			}
		}

		if (timestamp || log || logfrmt) {
			if (hwtimestamp) {
				const int timestamping_flags = (SOF_TIMESTAMPING_SOFTWARE |
								SOF_TIMESTAMPING_RX_SOFTWARE |
								SOF_TIMESTAMPING_RAW_HARDWARE);

				if (setsockopt(obj->s, SOL_SOCKET, SO_TIMESTAMPING,
					       &timestamping_flags, sizeof(timestamping_flags)) < 0) {
					perror("setsockopt SO_TIMESTAMPING is not supported by your Linux kernel");
					return 1;
				}
			} else {
				const int timestamp_on = 1;

				if (setsockopt(obj->s, SOL_SOCKET, SO_TIMESTAMP,
					       &timestamp_on, sizeof(timestamp_on)) < 0) {
					perror("setsockopt SO_TIMESTAMP");
					return 1;
				}
			}
		}

		if (dropmonitor) {
			const int dropmonitor_on = 1;

			if (setsockopt(obj->s, SOL_SOCKET, SO_RXQ_OVFL,
				       &dropmonitor_on, sizeof(dropmonitor_on)) < 0) {
				perror("setsockopt SO_RXQ_OVFL not supported by your Linux Kernel");
				return 1;
			}
		}

		if (bind(obj->s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			perror("bind");
			return 1;
		}
	}

	if (log) {
		if (!logname) {
			time_t currtime;
			struct tm now;
			char fname[83]; /* suggested by -Wformat-overflow= */

			if (time(&currtime) == (time_t)-1) {
				perror("time");
				return 1;
			}

			localtime_r(&currtime, &now);

			sprintf(fname, "candump-%04d-%02d-%02d_%02d%02d%02d.log",
				now.tm_year + 1900,
				now.tm_mon + 1,
				now.tm_mday,
				now.tm_hour,
				now.tm_min,
				now.tm_sec);

			logname = fname;
		}

		if (silent != SILENT_ON)
			fprintf(stderr, "Warning: Console output active while logging!\n");

		fprintf(stderr, "Enabling Logfile '%s'\n", logname);

		logfile = fopen(logname, "w");
		if (!logfile) {
			perror("logfile");
			return 1;
		}
	}

	/* these settings are static and can be held out of the hot path */
	iov.iov_base = &frame;
	msg.msg_name = &addr;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = &ctrlmsg;

	while (running) {
		num_events = epoll_wait(fd_epoll, events_pending, currmax, timeout_ms);
		if (num_events == -1) {
			if (errno != EINTR)
				running = 0;
			continue;
		}

		/* handle timeout */
		if (!num_events && timeout_ms >= 0) {
			running = 0;
			continue;
		}

		for (i = 0; i < num_events; i++) {  /* check waiting CAN RAW sockets */
			struct if_info* obj = events_pending[i].data.ptr;
			int idx;
			char *extra_info = "";

			/* these settings may be modified by recvmsg() */
			iov.iov_len = sizeof(frame);
			msg.msg_namelen = sizeof(addr);
			msg.msg_controllen = sizeof(ctrlmsg);
			msg.msg_flags = 0;

			nbytes = recvmsg(obj->s, &msg, 0);
			idx = idx2dindex(addr.can_ifindex, obj->s);

			if (nbytes < 0) {
				if ((errno == ENETDOWN) && !down_causes_exit) {
					fprintf(stderr, "%s: interface down\n", devname[idx]);
					continue;
				}
				perror("read");
				return 1;
			}

			if ((size_t)nbytes == CAN_MTU)
				maxdlen = CAN_MAX_DLEN;
			else if ((size_t)nbytes == CANFD_MTU)
				maxdlen = CANFD_MAX_DLEN;
			else {
				fprintf(stderr, "read: incomplete CAN frame\n");
				return 1;
			}

			if (count && (--count == 0))
				running = 0;

			for (cmsg = CMSG_FIRSTHDR(&msg);
			     cmsg && (cmsg->cmsg_level == SOL_SOCKET);
			     cmsg = CMSG_NXTHDR(&msg,cmsg)) {
				if (cmsg->cmsg_type == SO_TIMESTAMP) {
					memcpy(&tv, CMSG_DATA(cmsg), sizeof(tv));
				} else if (cmsg->cmsg_type == SO_TIMESTAMPING) {

					struct timespec *stamp = (struct timespec *)CMSG_DATA(cmsg);

					/*
					 * stamp[0] is the software timestamp
					 * stamp[1] is deprecated
					 * stamp[2] is the raw hardware timestamp
					 * See chapter 2.1.2 Receive timestamps in
					 * linux/Documentation/networking/timestamping.txt
					 */
					tv.tv_sec = stamp[2].tv_sec;
					tv.tv_usec = stamp[2].tv_nsec/1000;
				} else if (cmsg->cmsg_type == SO_RXQ_OVFL)
					memcpy(&obj->dropcnt, CMSG_DATA(cmsg), sizeof(__u32));
			}

			/* check for (unlikely) dropped frames on this specific socket */
			if (obj->dropcnt != obj->last_dropcnt) {

				__u32 frames = obj->dropcnt - obj->last_dropcnt;

				if (silent != SILENT_ON)
					printf("DROPCOUNT: dropped %u CAN frame%s on '%s' socket (total drops %u)\n",
					       frames, (frames > 1)?"s":"", devname[idx], obj->dropcnt);

				if (log)
					fprintf(logfile, "DROPCOUNT: dropped %u CAN frame%s on '%s' socket (total drops %u)\n",
						frames, (frames > 1)?"s":"", devname[idx], obj->dropcnt);

				obj->last_dropcnt = obj->dropcnt;
			}

			/* once we detected a EFF frame indent SFF frames accordingly */
			if (frame.can_id & CAN_EFF_FLAG)
				view |= CANLIB_VIEW_INDENT_SFF;

			if (extra_msg_info) {
				if (msg.msg_flags & MSG_DONTROUTE)
					extra_info = " T";
				else
					extra_info = " R";
			}

			if (log) {
				char buf[CL_CFSZ]; /* max length */
				char ts_buf[TIMESTAMPSZ];

				sprint_timestamp(logtimestamp, &tv, &last_tv, ts_buf);

				/* log CAN frame with absolute timestamp & device */
				sprint_canframe(buf, &frame, 0, maxdlen);
				fprintf(logfile, "%s%*s %s%s\n", ts_buf,
					max_devname_len, devname[idx], buf,
					extra_info);
			}

			if ((logfrmt) && (silent == SILENT_OFF)){
				char buf[CL_CFSZ]; /* max length */

				/* print CAN frame in log file style to stdout */
				sprint_canframe(buf, &frame, 0, maxdlen);
				print_timestamp(logtimestamp, &tv, &last_tv);

				printf("%*s %s%s\n",
					max_devname_len, devname[idx], buf,
					extra_info);
				goto out_fflush; /* no other output to stdout */
			}

			if (silent != SILENT_OFF){
				if (silent == SILENT_ANI) {
					printf("%c\b", anichar[silentani %= MAXANI]);
					silentani++;
				}
				goto out_fflush; /* no other output to stdout */
			}

			printf(" %s", (color > 2) ? col_on[idx % MAXCOL] : "");
			print_timestamp(timestamp, &tv, &last_tv);
			printf(" %s", (color && (color < 3)) ? col_on[idx % MAXCOL] : "");
			printf("%*s", max_devname_len, devname[idx]);

			if (extra_msg_info) {

				if (msg.msg_flags & MSG_DONTROUTE)
					printf ("  TX %s", extra_m_info[frame.flags & 3]);
				else
					printf ("  RX %s", extra_m_info[frame.flags & 3]);
			}

			printf("%s  ", (color == 1) ? col_off : "");

			fprint_long_canframe(stdout, &frame, NULL, view, maxdlen);

			printf("%s", (color > 1) ? col_off : "");
			printf("\n");

 out_fflush:
			fflush(stdout);
		}
	}

	for (i = 0; i < currmax; i++)
		close(sock_info[i].s);

	close(fd_epoll);

	if (log)
		fclose(logfile);

	return 0;
}
