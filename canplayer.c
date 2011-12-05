/*
 *  $Id$
 */

/*
 * canplayer.c - replay a compact CAN frame logfile to CAN devices
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
#include <string.h>
#include <time.h>
#include <libgen.h>
#include <stdlib.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#include "lib.h"

#define DEFAULT_GAP	1	/* ms */
#define DEFAULT_LOOPS	1	/* only one replay */
#define CHANNELS	20	/* anyone using more than 20 CAN interfaces at a time? */
#define BUFSZ		400	/* for one line in the logfile */
#define STDOUTIDX	65536	/* interface index for printing on stdout - bigger than max uint16 */

struct assignment {
	char txif[IFNAMSIZ];
	int  txifidx;
	char rxif[IFNAMSIZ];
};
static struct assignment asgn[CHANNELS];

extern int optind, opterr, optopt;

void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s <options> [interface assignment]*\n\n", prg);
	fprintf(stderr, "Options:              -I <infile>  (default stdin)\n");
	fprintf(stderr, "                      -l <num>     "
		"(process input file <num> times)\n"
		"                                   "
		"(Use 'i' for infinite loop - default: %d)\n", DEFAULT_LOOPS);
	fprintf(stderr, "                      -t           (ignore timestamps: "
		"send frames immediately)\n");
	fprintf(stderr, "                      -g <ms>      (gap in milli "
		"seconds - default: %d ms)\n", DEFAULT_GAP);
	fprintf(stderr, "                      -s <s>      (skip gaps in "
		"timestamps > 's' seconds)\n");
	fprintf(stderr, "                      -x           (disable local "
		"loopback of sent CAN frames)\n");
	fprintf(stderr, "                      -v           (verbose: print "
		"sent CAN frames)\n\n");
	fprintf(stderr, "Interface assignment:  0..n assignments like "
		"<write-if>=<log-if>\n");
	fprintf(stderr, "e.g. vcan2=can0 ( send frames received from can0 on "
		"vcan2 )\n");
	fprintf(stderr, "extra hook: stdout=can0 ( print logfile line marked with can0 on "
		"stdout )\n");
	fprintf(stderr, "No assignments => send frames to the interface(s) they "
		"had been received from.\n\n");
	fprintf(stderr, "Lines in the logfile not beginning with '(' (start of "
		"timestamp) are ignored.\n\n");
}

/* copied from /usr/src/linux/include/linux/time.h ...
 * lhs < rhs:  return <0
 * lhs == rhs: return 0
 * lhs > rhs:  return >0
 */
static inline int timeval_compare(struct timeval *lhs, struct timeval *rhs)
{
	if (lhs->tv_sec < rhs->tv_sec)
		return -1;
	if (lhs->tv_sec > rhs->tv_sec)
		return 1;
	return lhs->tv_usec - rhs->tv_usec;
}

static inline void create_diff_tv(struct timeval *today, struct timeval *diff,
				  struct timeval *log) {

	/* create diff_tv so that log_tv + diff_tv = today_tv */
	diff->tv_sec  = today->tv_sec  - log->tv_sec;
	diff->tv_usec = today->tv_usec - log->tv_usec;
}

static inline int frames_to_send(struct timeval *today, struct timeval *diff,
				 struct timeval *log)
{
	/* return value <0 when log + diff < today */

	struct timeval cmp;

	cmp.tv_sec  = log->tv_sec  + diff->tv_sec;
	cmp.tv_usec = log->tv_usec + diff->tv_usec;

	if (cmp.tv_usec > 1000000) {
		cmp.tv_usec -= 1000000;
		cmp.tv_sec++;
	}

	if (cmp.tv_usec < 0) {
		cmp.tv_usec += 1000000;
		cmp.tv_sec--;
	}

	return timeval_compare(&cmp, today);
}

int get_txidx(char *logif_name) {

	int i;

	for (i=0; i<CHANNELS; i++) {
		if (asgn[i].rxif[0] == 0) /* end of table content */
			break;
		if (strcmp(asgn[i].rxif, logif_name) == 0) /* found device name */
			break;
	}

	if ((i == CHANNELS) || (asgn[i].rxif[0] == 0))
		return 0; /* not found */

	return asgn[i].txifidx; /* return interface index */
}

char *get_txname(char *logif_name) {

	int i;

	for (i=0; i<CHANNELS; i++) {
		if (asgn[i].rxif[0] == 0) /* end of table content */
			break;
		if (strcmp(asgn[i].rxif, logif_name) == 0) /* found device name */
			break;
	}

	if ((i == CHANNELS) || (asgn[i].rxif[0] == 0))
		return 0; /* not found */

	return asgn[i].txif; /* return interface name */
}

int add_assignment(char *mode, int socket, char *txname, char *rxname,
		   int verbose) {

	struct ifreq ifr;
	int i;

	/* find free entry */
	for (i=0; i<CHANNELS; i++) {
		if (asgn[i].txif[0] == 0)
			break;
	}

	if (i == CHANNELS) {
		fprintf(stderr, "Assignment table exceeded!\n");
		return 1;
	}

	if (strlen(txname) >= IFNAMSIZ) {
		fprintf(stderr, "write-if interface name '%s' too long!", txname);
		return 1;
	}
	strcpy(asgn[i].txif, txname);

	if (strlen(rxname) >= IFNAMSIZ) {
		fprintf(stderr, "log-if interface name '%s' too long!", rxname);
		return 1;
	}
	strcpy(asgn[i].rxif, rxname);

	if (strcmp(txname, "stdout")) {
		strcpy(ifr.ifr_name, txname);
		if (ioctl(socket, SIOCGIFINDEX, &ifr) < 0) {
			perror("SIOCGIFINDEX");
			fprintf(stderr, "write-if interface name '%s' is wrong!\n", txname);
			return 1;
		}
		asgn[i].txifidx = ifr.ifr_ifindex;
	} else
		asgn[i].txifidx = STDOUTIDX;

	if (verbose > 1) /* use -v -v to see this */
		printf("added %s assignment: log-if=%s write-if=%s write-if-idx=%d\n",
		       mode, asgn[i].rxif, asgn[i].txif, asgn[i].txifidx);

	return 0;
}

int main(int argc, char **argv)
{
	static char buf[BUFSZ], device[BUFSZ], ascframe[BUFSZ];
	struct sockaddr_can addr;
	static struct can_frame frame;
	static struct timeval today_tv, log_tv, last_log_tv, diff_tv;
	struct timespec sleep_ts;
	int s; /* CAN_RAW socket */
	FILE *infile = stdin;
	unsigned long gap = DEFAULT_GAP; 
	int use_timestamps = 1;
	static int verbose, opt, delay_loops, skipgap;
	static int loopback_disable = 0;
	static int infinite_loops = 0;
	static int loops = DEFAULT_LOOPS;
	int assignments; /* assignments defined on the commandline */
	int txidx;       /* sendto() interface index */
	int eof, nbytes, i, j;
	char *fret;

	while ((opt = getopt(argc, argv, "I:l:tg:s:xv?")) != -1) {
		switch (opt) {
		case 'I':
			infile = fopen(optarg, "r");
			if (!infile) {
				perror("infile");
				return 1;
			}
			break;

		case 'l':
			if (optarg[0] == 'i')
				infinite_loops = 1;
			else
				if (!(loops = atoi(optarg))) {
					fprintf(stderr, "Invalid argument for option -l !\n");
					return 1;
				}
			break;

		case 't':
			use_timestamps = 0;
			break;

		case 'g':
			gap = strtoul(optarg, NULL, 10);
			break;

		case 's':
			skipgap = strtoul(optarg, NULL, 10);
			if (skipgap < 1) {
				fprintf(stderr, "Invalid argument for option -s !\n");
				return 1;
			}
			break;

		case 'x':
			loopback_disable = 1;
			break;

		case 'v':
			verbose++;
			break;

		case '?':
		default:
			print_usage(basename(argv[0]));
			return 1;
			break;
		}
	}

	assignments = argc - optind; /* find real number of user assignments */

	if (infile == stdin) { /* no jokes with stdin */
		infinite_loops = 0;
		loops = 1;
	}

	if (verbose > 1) { /* use -v -v to see this */
		if (infinite_loops)
			printf("infinite_loops\n");
		else
			printf("%d loops\n", loops);
	}

	sleep_ts.tv_sec  =  gap / 1000;
	sleep_ts.tv_nsec = (gap % 1000) * 1000000;

	/* open socket */
	if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		perror("socket");
		return 1;
	}

	addr.can_family  = AF_CAN;
	addr.can_ifindex = 0;

	/* disable unneeded default receive filter on this RAW socket */
	setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);

	if (loopback_disable) {
		int loopback = 0;

		setsockopt(s, SOL_CAN_RAW, CAN_RAW_LOOPBACK,
			   &loopback, sizeof(loopback));
	}

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	if (assignments) {
		/* add & check user assginments from commandline */
		for (i=0; i<assignments; i++) {
			if (strlen(argv[optind+i]) >= BUFSZ) {
				fprintf(stderr, "Assignment too long!\n");
				print_usage(basename(argv[0]));
				return 1;
			}
			strcpy(buf, argv[optind+i]);
			for (j=0; j<BUFSZ; j++) { /* find '=' in assignment */
				if (buf[j] == '=')
					break;
			}
			if ((j == BUFSZ) || (buf[j] != '=')) {
				fprintf(stderr, "'=' missing in assignment!\n");
				print_usage(basename(argv[0]));
				return 1;
			}
			buf[j] = 0; /* cut string in two pieces */
			if (add_assignment("user", s, &buf[0], &buf[j+1], verbose))
				return 1;
		}
	}

	while (infinite_loops || loops--) {

		if (infile != stdin)
			rewind(infile); /* for each loop */

		if (verbose > 1) /* use -v -v to see this */
			printf (">>>>>>>>> start reading file. remaining loops = %d\n", loops);

		/* read first non-comment frame from logfile */
		while ((fret = fgets(buf, BUFSZ-1, infile)) != NULL && buf[0] != '(') {
			if (strlen(buf) >= BUFSZ-2) {
				fprintf(stderr, "comment line too long for input buffer\n");
				return 1;
			}
		}

		if (!fret)
			goto out; /* nothing to read */

		eof = 0;

		if (sscanf(buf, "(%ld.%ld) %s %s", &log_tv.tv_sec, &log_tv.tv_usec,
			   device, ascframe) != 4) {
			fprintf(stderr, "incorrect line format in logfile\n");
			return 1;
		}

		if (use_timestamps) { /* throttle sending due to logfile timestamps */

			gettimeofday(&today_tv, NULL);
			create_diff_tv(&today_tv, &diff_tv, &log_tv);
			last_log_tv = log_tv;
		}

		while (!eof) {

			while ((!use_timestamps) ||
			       (frames_to_send(&today_tv, &diff_tv, &log_tv) < 0)) {

				/* log_tv/device/ascframe are valid here */

				if (strlen(device) >= IFNAMSIZ) {
					fprintf(stderr, "log interface name '%s' too long!", device);
					return 1;
				}

				txidx = get_txidx(device); /* get ifindex for sending the frame */
 
				if ((!txidx) && (!assignments)) {
					/* ifindex not found and no user assignments */
					/* => assign this device automatically       */
					if (add_assignment("auto", s, device, device, verbose))
						return 1;
					txidx = get_txidx(device);
				}

				if (txidx == STDOUTIDX) { /* hook to print logfile lines on stdout */

					printf("%s", buf); /* print the line AS-IS without extra \n */
					fflush(stdout);

				} else if (txidx > 0) { /* only send to valid CAN devices */

					if (parse_canframe(ascframe, &frame)) {
						fprintf(stderr, "wrong CAN frame format: '%s'!", ascframe);
						return 1;
					}

					addr.can_family  = AF_CAN;
					addr.can_ifindex = txidx; /* send via this interface */
 
					nbytes = sendto(s, &frame, sizeof(struct can_frame), 0,
							(struct sockaddr*)&addr, sizeof(addr));

					if (nbytes != sizeof(struct can_frame)) {
						perror("sendto");
						return 1;
					}

					if (verbose) {
						printf("%s (%s) ", get_txname(device), device);
						fprint_long_canframe(stdout, &frame, "\n", 1);
					}
				}

				/* read next non-comment frame from logfile */
				while ((fret = fgets(buf, BUFSZ-1, infile)) != NULL && buf[0] != '(') {
					if (strlen(buf) >= BUFSZ-2) {
						fprintf(stderr, "comment line too long for input buffer\n");
						return 1;
					}
				}

				if (!fret) {
					eof = 1; /* this file is completely processed */
					break;
				}

				if (sscanf(buf, "(%ld.%ld) %s %s", &log_tv.tv_sec, &log_tv.tv_usec,
					   device, ascframe) != 4) {
					fprintf(stderr, "incorrect line format in logfile\n");
					return 1;
				}

				if (use_timestamps) {
					gettimeofday(&today_tv, NULL);

					/* test for logfile timestamps jumping backwards OR      */
					/* if the user likes to skip long gaps in the timestamps */
					if ((last_log_tv.tv_sec > log_tv.tv_sec) ||
					    (skipgap && abs(last_log_tv.tv_sec - log_tv.tv_sec) > skipgap))
						create_diff_tv(&today_tv, &diff_tv, &log_tv);

					last_log_tv = log_tv;
				}

			} /* while frames_to_send ... */

			if (nanosleep(&sleep_ts, NULL))
				return 1;

			delay_loops++; /* private statistics */
			gettimeofday(&today_tv, NULL);

		} /* while (!eof) */

	} /* while (infinite_loops || loops--) */

out:

	close(s);
	fclose(infile);

	if (verbose > 1) /* use -v -v to see this */
		printf("%d delay_loops\n", delay_loops);

	return 0;
}
