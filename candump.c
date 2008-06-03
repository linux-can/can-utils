/*
 *  $Id$
 */

/*
 * candump.c
 *
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
 * Send feedback to <socketcan-users@lists.berlios.de>
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
#include "lib.h"

#define MAXSOCK 16    /* max. number of CAN interfaces given on the cmdline */
#define MAXFILTER 30  /* max. number of possible filters for each socket */
#define MAXIFNAMES 30 /* size of receive name index to omit ioctls */
#define MAXCOL 6      /* number of different colors for colorized output */
#define ANYDEV "any"  /* name of interface to receive from any CAN interface */
#define ANL "\r\n"    /* newline in ASC mode */

#define BOLD    ATTBOLD
#define RED     ATTBOLD FGRED
#define GREEN   ATTBOLD FGGREEN
#define YELLOW  ATTBOLD FGYELLOW
#define BLUE    ATTBOLD FGBLUE
#define MAGENTA ATTBOLD FGMAGENTA
#define CYAN    ATTBOLD FGCYAN

const char col_on [MAXCOL][19] = {BLUE, RED, GREEN, BOLD, MAGENTA, CYAN};
const char col_off [] = ATTRESET;

static char devname[MAXIFNAMES][IFNAMSIZ+1];
static int  dindex[MAXIFNAMES];
static int  max_devname_len; /* to prevent frazzled device name output */ 

#define MAXANI 4
const char anichar[MAXANI] = {'|', '/', '-', '\\'};

extern int optind, opterr, optopt;

static volatile int running = 1;

void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s [options] <CAN interface>+\n", prg);
	fprintf(stderr, "  (use CTRL-C to terminate %s)\n\n", prg);
	fprintf(stderr, "Options: -t <type>   (timestamp: (a)bsolute/(d)elta/(z)ero/(A)bsolute w date)\n");
	fprintf(stderr, "         -c          (increment color mode level)\n");
	fprintf(stderr, "         -i          (binary output - may exceed 80 chars/line)\n");
	fprintf(stderr, "         -a          (enable additional ASCII output)\n");
	fprintf(stderr, "         -s <level>  (silent mode - 1: animation 2: completely silent)\n");
	fprintf(stderr, "         -b <can>    (bridge mode - send received frames to <can>)\n");
	fprintf(stderr, "         -B <can>    (bridge mode - like '-b' with disabled loopback)\n");
	fprintf(stderr, "         -l          (log CAN-frames into file)\n");
	fprintf(stderr, "         -L          (use log file format on stdout)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Up to %d CAN interfaces with optional filter sets can be specified\n", MAXSOCK);
	fprintf(stderr, "on the commandline in the form: <ifname>[,filter]*\n");
	fprintf(stderr, "\nUp to %d comma separated filters can be specified for each given CAN interface:\n", MAXFILTER);
	fprintf(stderr, " <can_id>:<can_mask> (matches when <received_can_id> & mask == can_id & mask)\n");
	fprintf(stderr, " <can_id>~<can_mask> (matches when <received_can_id> & mask != can_id & mask)\n");
	fprintf(stderr, " #<error_mask>       (set error frame filter, see include/linux/can/error.h)\n");
	fprintf(stderr, "\nUse interface name '%s' to receive from all CAN interfaces.\n", ANYDEV);
	fprintf(stderr, "CAN IDs, masks and data content are given and expected in hexadecimal values.\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "%s -c -c -ta can0,123:7FF,400:700,#000000FF can2,400~7F0 can3 can8\n", prg);
	fprintf(stderr, "%s -l any,0~0,#FFFFFFFF    (log only error frames but no(!) data frames)\n", prg);
	fprintf(stderr, "%s vcan2,92345678:9FFFFFFF (match only for extended CAN ID 12345678)\n", prg);
	fprintf(stderr, "\n");
}

void sigterm(int signo)
{
	running = 0;
}

int idx2dindex(int ifidx, int socket) {

	int i;
	struct ifreq ifr;

	for (i=0; i < MAXIFNAMES; i++) {
		if (dindex[i] == ifidx)
			return i;
	}

	/* create new interface index cache entry */

	/* remove index cache zombies first */
	for (i=0; i < MAXIFNAMES; i++) {
		if (dindex[i]) {
			ifr.ifr_ifindex = dindex[i];
			if (ioctl(socket, SIOCGIFNAME, &ifr) < 0)
				dindex[i] = 0;
		}
	}

	for (i=0; i < MAXIFNAMES; i++)
		if (!dindex[i]) /* free entry */
			break;

	if (i == MAXIFNAMES) {
		printf("Interface index cache only supports %d interfaces.\n",
		       MAXIFNAMES);
		exit(1);
	}

	dindex[i] = ifidx;

	ifr.ifr_ifindex = ifidx;
	if (ioctl(socket, SIOCGIFNAME, &ifr) < 0)
		perror("SIOCGIFNAME");

	if (max_devname_len < strlen(ifr.ifr_name))
		max_devname_len = strlen(ifr.ifr_name);

	strcpy(devname[i], ifr.ifr_name);

#ifdef DEBUG
	printf("new index %d (%s)\n", i, devname[i]);
#endif

	return i;
}

int main(int argc, char **argv)
{
	fd_set rdfs;
	int s[MAXSOCK];
	int bridge = 0;
	unsigned char timestamp = 0;
	unsigned char silent = 0;
	unsigned char silentani = 0;
	unsigned char color = 0;
	unsigned char view = 0;
	unsigned char log = 0;
	unsigned char logfrmt = 0;
	int opt, ret;
	int currmax, numfilter;
	char *ptr, *nptr;
	struct sockaddr_can addr;
	struct can_filter rfilter[MAXFILTER];
	can_err_mask_t err_mask;
	struct can_frame frame;
	int nbytes, i;
	struct ifreq ifr;
	struct timeval tv, last_tv;
	FILE *logfile = NULL;

	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);
	signal(SIGINT, sigterm);

	last_tv.tv_sec  = 0;
	last_tv.tv_usec = 0;

	while ((opt = getopt(argc, argv, "t:cias:b:B:lLh?")) != -1) {
		switch (opt) {
		case 't':
			timestamp = optarg[0];
			if ((timestamp != 'a') && (timestamp != 'A') &&
			    (timestamp != 'd') && (timestamp != 'z')) {
				printf("%s: unknown timestamp mode '%c' - ignored\n",
				       basename(argv[0]), optarg[0]);
				timestamp = 0;
			}
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

		case 's':
			silent = atoi(optarg);
			break;

		case 'b':
		case 'B':
			if (strlen(optarg) >= IFNAMSIZ) {
				printf("Name of CAN device '%s' is too long!\n\n", optarg);
				return 1;
			} else {
				bridge = socket(PF_CAN, SOCK_RAW, CAN_RAW);
				if (bridge < 0) {
					perror("bridge socket");
					return 1;
				}
				addr.can_family = AF_CAN;
				strcpy(ifr.ifr_name, optarg);
				if (ioctl(bridge, SIOCGIFINDEX, &ifr) < 0)
					perror("SIOCGIFINDEX");
				addr.can_ifindex = ifr.ifr_ifindex;
		
				if (!addr.can_ifindex) {
					perror("invalid bridge interface");
					return 1;
				}

				if (opt == 'B') {
					int loopback = 0;

					setsockopt(bridge, SOL_CAN_RAW, CAN_RAW_LOOPBACK,
						   &loopback, sizeof(loopback));
				}

				if (bind(bridge, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
					perror("bridge bind");
					return 1;
				}
			}
			break;
	    
		case 'l':
			log = 1;
			break;

		case 'L':
			logfrmt = 1;
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
	
	currmax = argc - optind; /* find real number of CAN devices */

	if (currmax > MAXSOCK) {
		printf("More than %d CAN devices given on commandline!\n", MAXSOCK);
		return 1;
	}

	for (i=0; i < currmax; i++) {

		ptr = argv[optind+i];
		nptr = strchr(ptr, ',');

#ifdef DEBUG
		printf("open %d '%s'.\n", i, ptr);
#endif

		s[i] = socket(PF_CAN, SOCK_RAW, CAN_RAW);
		if (s[i] < 0) {
			perror("socket");
			return 1;
		}

		if (nptr)
			nbytes = nptr - ptr;  /* interface name is up the first ',' */
		else
			nbytes = strlen(ptr); /* no ',' found => no filter definitions */

		if (nbytes >= IFNAMSIZ) {
			printf("name of CAN device '%s' is too long!\n", ptr);
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

		if (strcmp(ANYDEV, ifr.ifr_name)) {
			if (ioctl(s[i], SIOCGIFINDEX, &ifr) < 0) {
				perror("SIOCGIFINDEX");
				exit(1);
			}
			addr.can_ifindex = ifr.ifr_ifindex;
		} else
			addr.can_ifindex = 0; /* any can interface */

		if (nptr) {

			/* found a ',' after the interface name => check for filters */

			numfilter = 0;
			err_mask = 0;

			while (nptr) {

				ptr = nptr+1; /* hop behind the ',' */
				nptr = strchr(ptr, ','); /* update exit condition */

				if (sscanf(ptr, "%lx:%lx",
					   (long unsigned int *)
					   &rfilter[numfilter].can_id, 
					   (long unsigned int *)
					   &rfilter[numfilter].can_mask) == 2) {
					numfilter++;
				} else if (sscanf(ptr, "%lx~%lx",
						  (long unsigned int *)
						  &rfilter[numfilter].can_id, 
						  (long unsigned int *)
						  &rfilter[numfilter].can_mask) == 2) {
					rfilter[numfilter].can_id |= CAN_INV_FILTER;
					numfilter++;
				} else if (sscanf(ptr, "#%lx",
						  (long unsigned int *)&err_mask) != 1) { 
					printf("Error in filter option parsing: '%s'\n", ptr);
					exit(1);
				}

				if (numfilter > MAXFILTER) {
					printf("Too many filters specified for '%s'.\n",
					       ifr.ifr_name);
					exit(1);
				}
			}

			if (err_mask)
				setsockopt(s[i], SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
					   &err_mask, sizeof(err_mask));

			if (numfilter)
				setsockopt(s[i], SOL_CAN_RAW, CAN_RAW_FILTER,
					   &rfilter, numfilter * sizeof(struct can_filter));
		} /* if (nptr) */

		if (bind(s[i], (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			perror("bind");
			return 1;
		}
	}

	if (log) {
		time_t currtime;
		struct tm now;
		char fname[sizeof("candump-2006-11-20_202026.log")+1];

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

		printf("\nEnabling Logfile '%s'\n\n", fname);

		logfile = fopen(fname, "w");
		if (!logfile) {
			perror("logfile");
			return 1;
		}
	}

	while (running) {

		FD_ZERO(&rdfs);
		for (i=0; i<currmax; i++)
			FD_SET(s[i], &rdfs);

		if ((ret = select(s[currmax-1]+1, &rdfs, NULL, NULL, NULL)) < 0) {
			//perror("select");
			running = 0;
			continue;
		}

		for (i=0; i<currmax; i++) {  /* check all CAN RAW sockets */

			if (FD_ISSET(s[i], &rdfs)) {

				socklen_t len = sizeof(addr);
				int idx;

				nbytes = recvfrom(s[i], &frame, sizeof(struct can_frame), 0,
						  (struct sockaddr*)&addr, &len);
				if (nbytes < 0) {
					perror("read");
					return 1;
				}

				if (nbytes < sizeof(struct can_frame)) {
					fprintf(stderr, "read: incomplete CAN frame\n");
					return 1;
				}

				if (bridge) {
					nbytes = write(bridge, &frame, sizeof(struct can_frame));
					if (nbytes < 0) {
						perror("bridge write");
						return 1;
					} else if (nbytes < sizeof(struct can_frame)) {
						fprintf(stderr,"bridge write: incomplete CAN frame\n");
						return 1;
					}
				}
		    
				if (timestamp || log || logfrmt)
					if (ioctl(s[i], SIOCGSTAMP, &tv) < 0)
						perror("SIOCGSTAMP");


				idx = idx2dindex(addr.can_ifindex, s[i]);

				if (log) {
					/* log CAN frame with absolute timestamp & device */
					fprintf(logfile, "(%ld.%06ld) ", tv.tv_sec, tv.tv_usec);
					fprintf(logfile, "%*s ", max_devname_len, devname[idx]);
					/* without seperator as logfile use-case is parsing */
					fprint_canframe(logfile, &frame, "\n", 0);
				}

				if (logfrmt) {
					/* print CAN frame in log file style to stdout */
					printf("(%ld.%06ld) ", tv.tv_sec, tv.tv_usec);
					printf("%*s ", max_devname_len, devname[idx]);
					fprint_canframe(stdout, &frame, "\n", 0);
					goto out_fflush; /* no other output to stdout */
				}

				if (silent){
					if (silent == 1) {
						printf("%c\b", anichar[silentani%=MAXANI]);
						silentani++;
					}
					goto out_fflush; /* no other output to stdout */
				}
		      
				printf(" %s", (color>2)?col_on[idx%MAXCOL]:"");

				switch (timestamp) {

				case 'a': /* absolute with timestamp */
					printf("(%ld.%06ld) ", tv.tv_sec, tv.tv_usec);
					break;

				case 'A': /* absolute with date */
				{
					struct tm tm;
					char timestring[25];

					tm = *localtime(&tv.tv_sec);
					strftime(timestring, 24, "%Y-%m-%d %H:%M:%S", &tm);
					printf("(%s.%06ld) ", timestring, tv.tv_usec);
				}
				break;

				case 'd': /* delta */
				case 'z': /* starting with zero */
				{
					struct timeval diff;

					if (last_tv.tv_sec == 0)   /* first init */
						last_tv = tv;
					diff.tv_sec  = tv.tv_sec  - last_tv.tv_sec;
					diff.tv_usec = tv.tv_usec - last_tv.tv_usec;
					if (diff.tv_usec < 0)
						diff.tv_sec--, diff.tv_usec += 1000000;
					if (diff.tv_sec < 0)
						diff.tv_sec = diff.tv_usec = 0;
					printf("(%ld.%06ld) ", diff.tv_sec, diff.tv_usec);
				
					if (timestamp == 'd')
						last_tv = tv; /* update for delta calculation */
				}
				break;

				default: /* no timestamp output */
					break;
				}

				printf(" %s", (color && (color<3))?col_on[idx%MAXCOL]:"");
				printf("%*s", max_devname_len, devname[idx]);
				printf("%s  ", (color==1)?col_off:"");

				fprint_long_canframe(stdout, &frame, NULL, view);

				printf("%s", (color>1)?col_off:"");
				printf("\n");
			}

		out_fflush:
			fflush(stdout);
		}
	}

	for (i=0; i<currmax; i++)
		close(s[i]);

	if (bridge)
		close(bridge);

	if (log)
		fclose(logfile);

	return 0;
}
