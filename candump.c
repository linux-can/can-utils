/*
 *  $Id$
 */

/*
 * candump.c
 *
 * Copyright (c) 2002-2005 Volkswagen Group Electronic Research
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, the following disclaimer and
 *    the referenced file 'COPYING'.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2 as distributed in the 'COPYING'
 * file from the main directory of the linux kernel source.
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

#define MAXDEV 6 /* change sscanf()'s manually if changed here */
#define ANYDEV "any"
#define ANL "\r\n" /* newline in ASC mode */

#define BOLD    ATTBOLD
#define RED     ATTBOLD FGRED
#define GREEN   ATTBOLD FGGREEN
#define YELLOW  ATTBOLD FGYELLOW
#define BLUE    ATTBOLD FGBLUE
#define MAGENTA ATTBOLD FGMAGENTA
#define CYAN    ATTBOLD FGCYAN

const char col_on [MAXDEV][19] = {BOLD, MAGENTA, GREEN, BLUE, CYAN, RED};
const char col_off [] = ATTRESET;

static char devname[MAXDEV][IFNAMSIZ+1];
static int  dindex[MAXDEV];
static int  max_devname_len;

#define MAXANI 8
const char anichar[MAXANI] = {'|', '/', '-', '\\', '|', '/', '-', '\\'};

extern int optind, opterr, optopt;

static volatile int running = 1;

void print_usage(char *prg)
{
    fprintf(stderr, "Usage: %s [can-interfaces]\n", prg);
    fprintf(stderr, "Options: -m <mask>   (default 0x00000000)\n");
    fprintf(stderr, "         -v <value>  (default 0x00000000)\n");
    fprintf(stderr, "         -i <0|1>    (inv_filter)\n");
    fprintf(stderr, "         -e <emask>  (mask for error frames)\n");
    fprintf(stderr, "         -t <type>   (timestamp: Absolute/Delta/Zero)\n");
    fprintf(stderr, "         -c          (color mode)\n");
    fprintf(stderr, "         -a          (enable additional ASCII output)\n");
    fprintf(stderr, "         -s <level>  (silent mode - 1: animation 2: nothing)\n");
    fprintf(stderr, "         -b <can>    (bridge mode - send received frames to <can>)\n");
    fprintf(stderr, "         -l          (log CAN-frames into file)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "When using more than one CAN interface the options\n");
    fprintf(stderr, "m/v/i/e have comma seperated values e.g. '-m 0,7FF,0'\n");
    fprintf(stderr, "Use interface name '%s' to receive from all can-interfaces\n", ANYDEV);
}

void sigterm(int signo)
{
    running = 0;
}

int idx2dindex(int ifidx, int socket) {

    int i;
    struct ifreq ifr;

    for (i=0; i<MAXDEV; i++) {
	if (dindex[i] == ifidx)
	    return i;
    }

    /* create new interface index cache entry */

    for (i=0; i < MAXDEV; i++)
	if (!dindex[i]) /* free entry */
	    break;

    if (i == MAXDEV) {
	printf("BUG in interface index cache! MAXDEV?\n");
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
    int s[MAXDEV];
    int bridge = 0;
    canid_t mask[MAXDEV] = {0};
    canid_t value[MAXDEV] = {0};
    int inv_filter[MAXDEV] = {0};
    can_err_mask_t err_mask[MAXDEV] = {0};
    unsigned char timestamp = 0;
    unsigned char silent = 0;
    unsigned char silentani = 0;
    unsigned char color = 0;
    unsigned char ascii = 0;
    unsigned char log = 0;
    int opt, ret;
    int currmax = 1; /* we assume at least one can bus ;-) */
    struct sockaddr_can addr;
    struct can_filter rfilter;
    struct can_frame frame;
    int nbytes, i, j;
    struct ifreq ifr;
    struct timeval tv, last_tv;
    FILE *logfile = NULL;

    signal(SIGTERM, sigterm);
    signal(SIGHUP, sigterm);
    signal(SIGINT, sigterm);

    last_tv.tv_sec = 0; /* init */

    while ((opt = getopt(argc, argv, "m:v:i:e:t:cas:b:l")) != -1) {
	switch (opt) {
	case 'm':
	    i = sscanf(optarg, "%x,%x,%x,%x,%x,%x",
		       &mask[0], &mask[1], &mask[2],
		       &mask[3], &mask[4], &mask[5]);
	    if (i > currmax)
		currmax = i;
	    break;

	case 'v':
	    i = sscanf(optarg, "%x,%x,%x,%x,%x,%x",
		       &value[0], &value[1], &value[2],
		       &value[3], &value[4], &value[5]);
	    if (i > currmax)
		currmax = i;
	    break;

	case 'i':
	    i = sscanf(optarg, "%d,%d,%d,%d,%d,%d",
		       &inv_filter[0], &inv_filter[1], &inv_filter[2],
		       &inv_filter[3], &inv_filter[4], &inv_filter[5]);
	    if (i > currmax)
		currmax = i;
	    break;

	case 'e':
	    i = sscanf(optarg, "%x,%x,%x,%x,%x,%x",
		       &err_mask[0], &err_mask[1], &err_mask[2],
		       &err_mask[3], &err_mask[4], &err_mask[5]);
	    if (i > currmax)
		currmax = i;
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

	case 'c':
	    color++;
	    break;

	case 'a':
	    ascii = 1;
	    break;

	case 's':
	    silent = atoi(optarg);
	    break;

	case 'b':
	    if (strlen(optarg) >= IFNAMSIZ) {
		printf("Name of CAN device '%s' is too long!\n\n", optarg);
		return 1;
	    }
	    else {
		if ((bridge = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
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

		if (bind(bridge, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		    perror("bridge bind");
		    return 1;
		}
	    }
	    break;
	    
	case 'l':
	    log = 1;
	    break;

	default:
	    fprintf(stderr, "Unknown option %c\n", opt);
	    print_usage(basename(argv[0]));
	    exit(1);
	    break;
	}
    }

    if (optind == argc) {
	print_usage(basename(argv[0]));
	exit(0);
    }
	
    /* count in options higher than device count ? */
    if (optind + currmax > argc) {
	printf("low count of CAN devices!\n");
	return 1;
    }

    currmax = argc - optind; /* find real number of CAN devices */

    if (currmax > MAXDEV) {
	printf("More than %d CAN devices!\n", MAXDEV);
	return 1;
    }

    for (i=0; i<currmax; i++) {

#ifdef DEBUG
	printf("open %d '%s' m%08X v%08X i%d e%d.\n",
	       i, argv[optind+i], mask[i], value[i],
	       inv_filter[i], err_mask[i]);
#endif

	if ((s[i] = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
	    perror("socket");
	    return 1;
	}

	if (mask[i] || value[i]) {

	    printf("CAN ID filter[%d] for %s set to "
		   "mask = %08X, value = %08X %s\n",
		   i, argv[optind+i], mask[i], value[i],
		   (inv_filter[i]) ? "(inv_filter)" : "");

	    rfilter.can_id   = value[i];
	    rfilter.can_mask = mask[i];
	    if (inv_filter[i])
		rfilter.can_id |= CAN_INV_FILTER;

	    setsockopt(s[i], SOL_CAN_RAW, CAN_RAW_FILTER,
		       &rfilter, sizeof(rfilter));
	}

	if (err_mask[i])
	    setsockopt(s[i], SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
		       &err_mask[i], sizeof(err_mask[i]));

	j = strlen(argv[optind+i]);

	if (!(j < IFNAMSIZ)) {
	    printf("name of CAN device '%s' is too long!\n", argv[optind+i]);
	    return 1;
	}

	if (j > max_devname_len)
	    max_devname_len = j; /* for nice printing */

	addr.can_family = AF_CAN;

	if (strcmp(ANYDEV, argv[optind+i])) {
	    strcpy(ifr.ifr_name, argv[optind+i]);
	    if (ioctl(s[i], SIOCGIFINDEX, &ifr) < 0) {
		perror("SIOCGIFINDEX");
		exit(1);
	    }
	    addr.can_ifindex = ifr.ifr_ifindex;
	}
	else
	    addr.can_ifindex = 0; /* any can interface */

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
	FD_SET(0, &rdfs);
	for (i=0; i<currmax; i++)
	    FD_SET(s[i], &rdfs);

	if ((ret = select(s[currmax-1]+1, &rdfs, NULL, NULL, NULL)) < 0) {
	    //perror("select");
	    running = 0;
	    continue;
	}

	if (FD_ISSET(0, &rdfs)) {
	    running = 0; /* stop with input from stdin */
	    getchar();
	}

	for (i=0; i<currmax; i++) {  /* check all CAN RAW sockets */

	    if (FD_ISSET(s[i], &rdfs)) {

		socklen_t len = sizeof(addr);
		int idx;

		if ((nbytes = recvfrom(s[i], &frame,
				       sizeof(struct can_frame), 0,
				       (struct sockaddr*)&addr, &len)) < 0) {
		    perror("read");
		    return 1;
		}

		if (nbytes < sizeof(struct can_frame)) {
		    fprintf(stderr, "read: incomplete CAN frame\n");
		    return 1;
		}

		if (bridge) {
		    if ((nbytes = write(bridge, &frame,
					sizeof(struct can_frame))) < 0) {
			perror("bridge write");
			return 1;
		    } else if (nbytes < sizeof(struct can_frame)) {
			fprintf(stderr,"bridge write: incomplete CAN frame\n");
			return 1;
		    }
		}
		    
		if (timestamp || log)
		    if (ioctl(s[i], SIOCGSTAMP, &tv) < 0)
			perror("SIOCGSTAMP");


		idx = idx2dindex(addr.can_ifindex, s[i]);

		if (log) {
		    /* log CAN frame with absolute timestamp & device */
		    fprintf(logfile, "(%ld.%06ld) ", tv.tv_sec, tv.tv_usec);
		    fprintf(logfile, "%*s", max_devname_len, devname[idx]);
		    fprintf(logfile, " ");
		    /* without seperator as logfile use-case is parsing */
		    fprint_canframe(logfile, &frame, "\n", 0);
		}

		if (silent){
		    if (silent == 1) {
			printf("%c\b", anichar[silentani%=MAXANI]);
			silentani++;
		    }
		    continue;
		}
		      
		printf(" %s",(color>2)?col_on[idx]:"");

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

		printf(" %s",(color && (color<3))?col_on[idx]:"");
		printf("%*s", max_devname_len, devname[idx]);
		printf("%s  ",(color<2)?col_off:"");

		fprint_long_canframe(stdout, &frame, NULL, ascii);

		printf("%s",(color>1)?col_off:"");
		printf("\n");
	    }
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
