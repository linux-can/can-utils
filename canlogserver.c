/*
 *  $Id$
 */

/*
 * canlogserver.c
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
#include <netinet/in.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <signal.h>
#include <wait.h>
#include <errno.h>

#include "lib.h"

#define MAXDEV 6 /* change sscanf()'s manually if changed here */
#define ANYDEV "any"
#define ANL "\r\n" /* newline in ASC mode */

#define DEFPORT 28700

static char devname[MAXDEV][IFNAMSIZ+1];
static int  dindex[MAXDEV];
static int  max_devname_len;

extern int optind, opterr, optopt;

static volatile int running = 1;

void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s [options] <CAN interface>+\n", prg);
	fprintf(stderr, "  (use CTRL-C to terminate %s)\n\n", prg);
	fprintf(stderr, "Options: -m <mask>   (ID filter mask.  Default 0x00000000) *\n");
	fprintf(stderr, "         -v <value>  (ID filter value. Default 0x00000000) *\n");
	fprintf(stderr, "         -i <0|1>    (invert the specified ID filter) *\n");
	fprintf(stderr, "         -e <emask>  (mask for error frames)\n");
	fprintf(stderr, "         -p <port>   (listen on port <port>. Default: %d)\n", DEFPORT);
	fprintf(stderr, "\n");
	fprintf(stderr, "* The CAN ID filter matches, when ...\n");
	fprintf(stderr, "       <received_can_id> & mask == value & mask\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "When using more than one CAN interface the options\n");
	fprintf(stderr, "m/v/i/e have comma seperated values e.g. '-m 0,7FF,0'\n");
	fprintf(stderr, "\nUse interface name '%s' to receive from all CAN interfaces.\n\n", ANYDEV);
}

int idx2dindex(int ifidx, int socket)
{
	int i;
	struct ifreq ifr;

	for (i=0; i<MAXDEV; i++) {
		if (dindex[i] == ifidx)
			return i;
	}

	/* create new interface index cache entry */

	/* remove index cache zombies first */
	for (i=0; i < MAXDEV; i++) {
		if (dindex[i]) {
			ifr.ifr_ifindex = dindex[i];
			if (ioctl(socket, SIOCGIFNAME, &ifr) < 0)
				dindex[i] = 0;
		}
	}

	for (i=0; i < MAXDEV; i++)
		if (!dindex[i]) /* free entry */
			break;

	if (i == MAXDEV) {
		printf("Interface index cache only supports %d interfaces.\n", MAXDEV);
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

/* 
 * This is a Signalhandler. When we get a signal, that a child
 * terminated, we wait for it, so the zombie will disappear.
 */
void childdied(int i)
{
	wait(NULL);
}

/*
 * This is a Signalhandler for a cought SIGTERM
 */
void shutdown_gra(int i)
{
	exit(0);
}


int main(int argc, char **argv)
{
	struct sigaction signalaction;
	sigset_t sigset;
	fd_set rdfs;
	int s[MAXDEV];
	int socki, accsocket;
	canid_t mask[MAXDEV] = {0};
	canid_t value[MAXDEV] = {0};
	int inv_filter[MAXDEV] = {0};
	can_err_mask_t err_mask[MAXDEV] = {0};
	int opt, ret;
	int currmax = 1; /* we assume at least one can bus ;-) */
	struct sockaddr_can addr;
	struct can_filter rfilter;
	struct can_frame frame;
	int nbytes, i, j;
	struct ifreq ifr;
	struct timeval tv, last_tv;
	int port = DEFPORT;
	struct sockaddr_in inaddr;
	struct sockaddr_in clientaddr;
	socklen_t sin_size = sizeof(clientaddr);
	char temp[128];

	sigemptyset(&sigset);
	signalaction.sa_handler = &childdied;
	signalaction.sa_mask = sigset;
	signalaction.sa_flags = 0;
	sigaction(SIGCHLD, &signalaction, NULL);  /* install signal for dying child */
	signalaction.sa_handler = &shutdown_gra;
	signalaction.sa_mask = sigset;
	signalaction.sa_flags = 0;
	sigaction(SIGTERM, &signalaction, NULL); /* install Signal for termination */
	sigaction(SIGINT, &signalaction, NULL); /* install Signal for termination */


	last_tv.tv_sec  = 0;
	last_tv.tv_usec = 0;

	while ((opt = getopt(argc, argv, "m:v:i:e:p:?")) != -1) {

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
		case 'p':
			port = atoi(optarg);
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


	socki = socket(PF_INET, SOCK_STREAM, 0);
	if (socki < 0) {
		perror("socket");
		exit(1);
	}

	inaddr.sin_family = AF_INET;
	inaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	inaddr.sin_port = htons(port);

	while(bind(socki, (struct sockaddr*)&inaddr, sizeof(inaddr)) < 0) {
		printf(".");fflush(NULL);
		usleep(100000);
	}

	if (listen(socki, 3) != 0) {
		perror("listen");
		exit(1);
	}

	while(1) {
		accsocket = accept(socki, (struct sockaddr*)&clientaddr, &sin_size);
		if (accsocket > 0) {
			//printf("accepted\n");
			if (!fork())
				break;
			else
				close(accsocket);
		}
		else if (errno != EINTR) {
			perror("accept");
			exit(1);
		}
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
			perror("bindcan");
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

				if (ioctl(s[i], SIOCGSTAMP, &tv) < 0)
					perror("SIOCGSTAMP");


				idx = idx2dindex(addr.can_ifindex, s[i]);

				sprintf(temp, "(%ld.%06ld) %*s ",
					tv.tv_sec, tv.tv_usec, max_devname_len, devname[idx]);
				sprint_canframe(temp+strlen(temp), &frame, 0); 
				strcat(temp, "\n");

				if (write(accsocket, temp, strlen(temp)) < 0) {
					perror("writeaccsock");
					return 1;
				}
		    
				/* printf("%s\n",temp2); */

#if 0
				/* print CAN frame in log file style to stdout */
				printf("(%ld.%06ld) ", tv.tv_sec, tv.tv_usec);
				printf("%*s ", max_devname_len, devname[idx]);
				fprint_canframe(stdout, &frame, "\n", 0);
#endif
			}

		}
	}

	for (i=0; i<currmax; i++)
		close(s[i]);

	close(accsocket);
	return 0;
}
