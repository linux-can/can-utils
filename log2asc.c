/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * log2asc.c - convert compact CAN frame logfile to ASC logfile
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

#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/can.h>
#include <net/if.h>
#include <sys/time.h>

#include "lib.h"

#define BUFSZ 400 /* for one line in the logfile */

extern int optind, opterr, optopt;

void print_usage(char *prg)
{
	fprintf(stderr, "%s - convert compact CAN frame logfile to ASC logfile.\n", prg);
	fprintf(stderr, "Usage: %s <options> [can-interfaces]\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -I <infile>   (default stdin)\n");
	fprintf(stderr, "         -O <outfile>  (default stdout)\n");
	fprintf(stderr, "         -4  (reduce decimal place to 4 digits)\n");
	fprintf(stderr, "         -n  (set newline to cr/lf - default lf)\n");
	fprintf(stderr, "         -f  (use CANFD format also for Classic CAN)\n");
	fprintf(stderr, "         -r  (suppress dlc for RTR frames - pre v8.5 tools)\n");
}

void can_asc(struct canfd_frame *cf, int devno, int nortrdlc, char *extra_info, FILE *outfile)
{
	int i;
	char id[10];
	char *dir = "Rx";

	fprintf(outfile, "%-2d ", devno); /* channel number left aligned */

	if (cf->can_id & CAN_ERR_FLAG)
		fprintf(outfile, "ErrorFrame");
	else {
		sprintf(id, "%X%c", cf->can_id & CAN_EFF_MASK,
			(cf->can_id & CAN_EFF_FLAG)?'x':' ');

		/* check for extra info */
		if (strlen(extra_info) > 0) {
			/* only the first char is defined so far */
			if (extra_info[0] == 'T')
				dir = "Tx";
		}

		fprintf(outfile, "%-15s %s   ", id, dir);

		if (cf->can_id & CAN_RTR_FLAG) {
			if (nortrdlc)
				fprintf(outfile, "r"); /* RTR frame */
			else
				fprintf(outfile, "r %d", cf->len); /* RTR frame */
		} else {
			fprintf(outfile, "d %d", cf->len); /* data frame */

			for (i = 0; i < cf->len; i++) {
				fprintf(outfile, " %02X", cf->data[i]);
			}
		}
	}
}

void canfd_asc(struct canfd_frame *cf, int devno, int mtu, char *extra_info, FILE *outfile)
{
	int i;
	char id[10];
	char *dir = "Rx";
	unsigned int flags = 0;
	unsigned int dlen = cf->len;

	/* relevant flags in Flags field */
#define ASC_F_RTR 0x00000010
#define ASC_F_FDF 0x00001000
#define ASC_F_BRS 0x00002000
#define ASC_F_ESI 0x00004000

	/* check for extra info */
	if (strlen(extra_info) > 0) {
		/* only the first char is defined so far */
		if (extra_info[0] == 'T')
			dir = "Tx";
	}

	fprintf(outfile, "CANFD %3d %s ", devno, dir); /* 3 column channel number right aligned */

	sprintf(id, "%X%c", cf->can_id & CAN_EFF_MASK,
		(cf->can_id & CAN_EFF_FLAG)?'x':' ');
	fprintf(outfile, "%11s                                  ", id);
	fprintf(outfile, "%c ", (cf->flags & CANFD_BRS)?'1':'0');
	fprintf(outfile, "%c ", (cf->flags & CANFD_ESI)?'1':'0');
	fprintf(outfile, "%x ", can_len2dlc(dlen));

	if (mtu == CAN_MTU) {
		if (cf->can_id & CAN_RTR_FLAG) {
			/* no data length but dlc for RTR frames */
			dlen = 0;
			flags = ASC_F_RTR;
		}
	} else {
		flags = ASC_F_FDF;
		if (cf->flags & CANFD_BRS)
			flags |= ASC_F_BRS;
		if (cf->flags & CANFD_ESI)
			flags |= ASC_F_ESI;
	}

	fprintf(outfile, "%2d", dlen);

	for (i = 0; i < (int)dlen; i++) {
		fprintf(outfile, " %02X", cf->data[i]);
	}

	fprintf(outfile, " %8d %4d %8X 0 0 0 0 0", 130000, 130, flags);
}

int main(int argc, char **argv)
{
	static char buf[BUFSZ], device[BUFSZ], ascframe[BUFSZ], extra_info[BUFSZ];

	struct canfd_frame cf;
	static struct timeval tv, start_tv;
	FILE *infile = stdin;
	FILE *outfile = stdout;
	static int maxdev, devno, i, crlf, fdfmt, nortrdlc, d4, opt, mtu;

	while ((opt = getopt(argc, argv, "I:O:4nfr?")) != -1) {
		switch (opt) {
		case 'I':
			infile = fopen(optarg, "r");
			if (!infile) {
				perror("infile");
				return 1;
			}
			break;

		case 'O':
			outfile = fopen(optarg, "w");
			if (!outfile) {
				perror("outfile");
				return 1;
			}
			break;

		case 'n':
			crlf = 1;
			break;

		case 'f':
			fdfmt = 1;
			break;

		case 'r':
			nortrdlc = 1;
			break;

		case '4':
			d4 = 1;
			break;

		case '?':
			print_usage(basename(argv[0]));
			return 0;
			break;

		default:
			fprintf(stderr, "Unknown option %c\n", opt);
			print_usage(basename(argv[0]));
			return 1;
			break;
		}
	}

	maxdev = argc - optind; /* find real number of CAN devices */

	if (!maxdev) {
		fprintf(stderr, "no CAN interfaces defined!\n");
		print_usage(basename(argv[0]));
		return 1;
	}
	
	//printf("Found %d CAN devices!\n", maxdev);

	while (fgets(buf, BUFSZ-1, infile)) {

		if (strlen(buf) >= BUFSZ-2) {
			fprintf(stderr, "line too long for input buffer\n");
			return 1;
		}

		/* check for a comment line */
		if (buf[0] != '(')
			continue;

		if (sscanf(buf, "(%lu.%lu) %s %s %s", &tv.tv_sec, &tv.tv_usec,
			   device, ascframe, extra_info) != 5) {

			/* do not evaluate the extra info */
			extra_info[0] = 0;

			if (sscanf(buf, "(%lu.%lu) %s %s", &tv.tv_sec, &tv.tv_usec,
				   device, ascframe) != 4) {
				fprintf(stderr, "incorrect line format in logfile\n");
				return 1;
			}
		}

		if (!start_tv.tv_sec) { /* print banner */
			start_tv = tv;
			fprintf(outfile, "date %s", ctime(&start_tv.tv_sec));
			fprintf(outfile, "base hex  timestamps absolute%s",
				(crlf)?"\r\n":"\n");
			fprintf(outfile, "no internal events logged%s",
				(crlf)?"\r\n":"\n");
		}

		for (i=0, devno=0; i<maxdev; i++) {
			if (!strcmp(device, argv[optind+i])) {
				devno = i+1; /* start with channel '1' */
				break;
			}
		}

		if (devno) { /* only convert for selected CAN devices */
			mtu = parse_canframe(ascframe, &cf);
			if ((mtu != CAN_MTU) && (mtu != CANFD_MTU))
				return 1;

			/* we don't support error message frames in CAN FD */
			if ((mtu == CANFD_MTU) && (cf.can_id & CAN_ERR_FLAG))
				continue;

			tv.tv_sec  = tv.tv_sec - start_tv.tv_sec;
			tv.tv_usec = tv.tv_usec - start_tv.tv_usec;
			if (tv.tv_usec < 0)
				tv.tv_sec--, tv.tv_usec += 1000000;
			if (tv.tv_sec < 0)
				tv.tv_sec = tv.tv_usec = 0;

			if (d4)
				fprintf(outfile, "%4lu.%04lu ", tv.tv_sec, tv.tv_usec/100);
			else
				fprintf(outfile, "%4lu.%06lu ", tv.tv_sec, tv.tv_usec);

			if ((mtu == CAN_MTU) && (fdfmt == 0))
				can_asc(&cf, devno, nortrdlc, extra_info, outfile);
			else
				canfd_asc(&cf, devno, mtu, extra_info, outfile);

			if (crlf)
				fprintf(outfile, "\r");
			fprintf(outfile, "\n");
		}
	}
	fflush(outfile);
	fclose(outfile);
	fclose(infile);

	return 0;
}
