/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * asc2log.c - convert ASC logfile to compact CAN frame logfile
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
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/error.h>
#include <net/if.h>

#include "lib.h"

#define BUFLEN 400 /* CAN FD mode lines can be pretty long */

extern int optind, opterr, optopt;

void print_usage(char *prg)
{
	fprintf(stderr, "%s - convert ASC logfile to compact CAN frame logfile.\n", prg);
	fprintf(stderr, "Usage: %s\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t-I <infile>\t(default stdin)\n");
	fprintf(stderr, "\t-O <outfile>\t(default stdout)\n");
}

void prframe(FILE *file, struct timeval *tv, int dev, struct canfd_frame *cf, unsigned int max_dlen, char *extra_info) {

	fprintf(file, "(%lu.%06lu) ", tv->tv_sec, tv->tv_usec);

	if (dev > 0)
		fprintf(file, "can%d ", dev-1);
	else
		fprintf(file, "canX ");

	fprint_canframe(file, cf, extra_info, 0, max_dlen);
}

void get_can_id(struct canfd_frame *cf, char *idstring, int base) {

	if (idstring[strlen(idstring)-1] == 'x') {
		cf->can_id = CAN_EFF_FLAG;
		idstring[strlen(idstring)-1] = 0;
	} else
		cf->can_id = 0;
    
	cf->can_id |= strtoul(idstring, NULL, base);
}

void calc_tv(struct timeval *tv, struct timeval *read_tv,
	     struct timeval *date_tv, char timestamps, int dplace) {

	if (dplace == 4) /* shift values having only 4 decimal places */
		read_tv->tv_usec *= 100;                /* and need for 6 */

	if (dplace == 5) /* shift values having only 5 decimal places */
		read_tv->tv_usec *= 10;                /* and need for 6 */

	if (timestamps == 'a') { /* absolute */

		tv->tv_sec  = date_tv->tv_sec  + read_tv->tv_sec;
		tv->tv_usec = date_tv->tv_usec + read_tv->tv_usec;

	} else { /* relative */

		if (((!tv->tv_sec) && (!tv->tv_usec)) && 
		    (date_tv->tv_sec || date_tv->tv_usec)) {
			tv->tv_sec  = date_tv->tv_sec; /* initial date/time */
			tv->tv_usec = date_tv->tv_usec;
		}

		tv->tv_sec  += read_tv->tv_sec;
		tv->tv_usec += read_tv->tv_usec;
	}

	if (tv->tv_usec > 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec++;
	}
}

void eval_can(char* buf, struct timeval *date_tvp, char timestamps, char base, int dplace, FILE *outfile) {

	int interface;
	static struct timeval tv; /* current frame timestamp */
	static struct timeval read_tv; /* frame timestamp from ASC file */
	struct canfd_frame cf;
	char rtr;
	int dlc = 0;
	int data[8];
	char tmp1[BUFLEN];
	char dir[3]; /* 'Rx' or 'Tx' plus terminating zero */
	char *extra_info;
	int i, items, found;

	/* 0.002367 1 390x Rx d 8 17 00 14 00 C0 00 08 00 */

	found = 0; /* found valid CAN frame ? */

	if (base == 'h') { /* check for CAN frames with hexadecimal values */

		items = sscanf(buf, "%lu.%lu %d %s %2s %c %d %x %x %x %x %x %x %x %x",
			       &read_tv.tv_sec, &read_tv.tv_usec, &interface,
			       tmp1, dir, &rtr, &dlc,
			       &data[0], &data[1], &data[2], &data[3],
			       &data[4], &data[5], &data[6], &data[7]);

		if ((items == dlc + 7 ) || /* data frame */
		    ((items == 6) && (rtr == 'r')) || /* RTR without DLC */
		    ((items == 7) && (rtr == 'r'))) { /* RTR with DLC */
			found = 1;
			get_can_id(&cf, tmp1, 16);
		}

	} else { /* check for CAN frames with decimal values */

		items = sscanf(buf, "%lu.%lu %d %s %2s %c %d %d %d %d %d %d %d %d %d",
			       &read_tv.tv_sec, &read_tv.tv_usec, &interface,
			       tmp1, dir, &rtr, &dlc,
			       &data[0], &data[1], &data[2], &data[3],
			       &data[4], &data[5], &data[6], &data[7]);

		if ((items == dlc + 7 ) || /* data frame */
		    ((items == 6) && (rtr == 'r')) || /* RTR without DLC */
		    ((items == 7) && (rtr == 'r'))) { /* RTR with DLC */
			found = 1;
			get_can_id(&cf, tmp1, 10);
		}
	}

	if (found) {

		if (dlc > CAN_MAX_DLC)
			return;

		if (strlen(dir) != 2) /* "Rx" or "Tx" */
			return;

		if (dir[0] == 'R')
			extra_info = " R\n";
		else
			extra_info = " T\n";

		cf.len = dlc;
		if (rtr == 'r')
			cf.can_id |= CAN_RTR_FLAG;
		else
			for (i = 0; i < dlc; i++)
				cf.data[i] = data[i] & 0xFFU;

		calc_tv(&tv, &read_tv, date_tvp, timestamps, dplace);
		prframe(outfile, &tv, interface, &cf, CAN_MAX_DLEN, extra_info);
		fflush(outfile);
		return;
	}

	/* check for ErrorFrames */
	if (sscanf(buf, "%lu.%lu %d %s",
		   &read_tv.tv_sec, &read_tv.tv_usec,
		   &interface, tmp1) == 4) {

		if (!strncmp(tmp1, "ErrorFrame", strlen("ErrorFrame"))) {

			memset(&cf, 0, sizeof(cf));
			/* do not know more than 'Error' */
			cf.can_id = (CAN_ERR_FLAG | CAN_ERR_BUSERROR);
			cf.len = CAN_ERR_DLC;

			calc_tv(&tv, &read_tv, date_tvp, timestamps, dplace);
			prframe(outfile, &tv, interface, &cf, CAN_MAX_DLEN, "\n");
			fflush(outfile);
		}
	}
}

void eval_canfd(char* buf, struct timeval *date_tvp, char timestamps, int dplace, FILE *outfile) {

	int interface;
	static struct timeval tv; /* current frame timestamp */
	static struct timeval read_tv; /* frame timestamp from ASC file */
	struct canfd_frame cf = { 0 };
	unsigned char brs, esi, ctmp;
	unsigned int flags;
	int dlc, dlen = 0;
	char tmp1[BUFLEN];
	char dir[3]; /* 'Rx' or 'Tx' plus terminating zero */
	char *extra_info;
	char *ptr;
	int i;

	/* The CANFD format is mainly in hex representation but <DataLength>
	   and probably some content we skip anyway. Don't trust the docs! */

	/* 21.671796 CANFD   1 Tx         11  msgCanFdFr1                      1 0 a 16 \
	   00 00 00 00 00 00 00 00 00 00 00 00 00 00 59 c0		\
	   100000  214   223040 80000000 46500250 460a0250 20011736 20010205 */

	/* check for valid line without symbolic name */
	if (sscanf(buf, "%lu.%lu %*s %d %2s %s %hhx %hhx %x %d ",
		   &read_tv.tv_sec, &read_tv.tv_usec, &interface,
		   dir, tmp1, &brs, &esi, &dlc, &dlen) != 9) {

		/* check for valid line with a symbolic name */
		if (sscanf(buf, "%lu.%lu %*s %d %2s %s %*s %hhx %hhx %x %d ",
			   &read_tv.tv_sec, &read_tv.tv_usec, &interface,
			   dir, tmp1, &brs, &esi, &dlc, &dlen) != 9) {

			/* no valid CANFD format pattern */
			return;
		}
	}

	/* check for allowed (unsigned) value ranges */
	if ((dlen > CANFD_MAX_DLEN) || (dlc > CANFD_MAX_DLC) ||
	    (brs > 1) || (esi > 1))
		return;

	if (strlen(dir) != 2) /* "Rx" or "Tx" */
		return;

	if (dir[0] == 'R')
		extra_info = " R\n";
	else
		extra_info = " T\n";

	/* don't trust ASCII content - sanitize data length */
	if (dlen != can_dlc2len(can_len2dlc(dlen)))
		return;

	get_can_id(&cf, tmp1, 16);

	/* now search for the beginning of the data[] content */
	sprintf(tmp1, " %x %x %x %2d ", brs, esi, dlc, dlen);

	/* search for the pattern generated by real data */
	ptr = strcasestr(buf, tmp1);
	if (ptr == NULL)
		return;

	ptr += strlen(tmp1); /* start of ASCII hex frame data */

	cf.len = dlen;

	for (i = 0; i < dlen; i++) {
		ctmp = asc2nibble(ptr[0]);
		if (ctmp > 0x0F)
			return;

		cf.data[i] = (ctmp << 4);

		ctmp = asc2nibble(ptr[1]);
		if (ctmp > 0x0F)
			return;

		cf.data[i] |= ctmp;

		ptr += 3; /* start of next ASCII hex byte */
	}

	/* skip MessageDuration and MessageLength to get Flags value */
	if (sscanf(ptr, "   %*x %*x %x ", &flags) != 1)
		return;

	/* relevant flags in Flags field */
#define ASC_F_RTR 0x00000010
#define ASC_F_FDF 0x00001000
#define ASC_F_BRS 0x00002000
#define ASC_F_ESI 0x00004000

	if (flags & ASC_F_FDF) {
		dlen = CANFD_MAX_DLEN;
		if (flags & ASC_F_BRS)
			cf.flags |= CANFD_BRS;
		if (flags & ASC_F_ESI)
			cf.flags |= CANFD_ESI;
	} else {
		/* yes. The 'CANFD' format supports classic CAN content! */
		dlen = CAN_MAX_DLEN;
		if (flags & ASC_F_RTR) {
			cf.can_id |= CAN_RTR_FLAG;
			/* dlen is always 0 for classic CAN RTR frames
			   but the DLC value is valid in RTR cases */
			cf.len = dlc;
		}
	}

	calc_tv(&tv, &read_tv, date_tvp, timestamps, dplace);
	prframe(outfile, &tv, interface, &cf, dlen, extra_info);
	fflush(outfile);

	/* No support for really strange CANFD ErrorFrames format m( */
}

int get_date(struct timeval *tv, char *date) {

	struct tm tms;
	unsigned int msecs = 0;

	if (strcasestr(date, " pm ") != NULL) {
		/* assume EN/US date due to existing am/pm field */

		if (!setlocale(LC_TIME, "en_US")) {
			fprintf(stderr, "Setting locale to 'en_US' failed!\n");
			return 1;
		}

		if (!strptime(date, "%B %d %I:%M:%S %p %Y", &tms)) {
			/* The string might contain a milliseconds value which strptime()
			   does not support. So we read the ms value into the year variable
			   before parsing the real year value (hack) */
			if (!strptime(date, "%B %d %I:%M:%S.%Y %p %Y", &tms))
				return 1;
			sscanf(date, "%*s %*d %*d:%*d:%*d.%3u ", &msecs);
		}

	} else {
		/* assume DE date due to non existing am/pm field */

		if (!setlocale(LC_TIME, "de_DE")) {
			fprintf(stderr, "Setting locale to 'de_DE' failed!\n");
			return 1;
		}

		if (!strptime(date, "%B %d %H:%M:%S %Y", &tms)) {
			/* The string might contain a milliseconds value which strptime()
			   does not support. So we read the ms value into the year variable
			   before parsing the real year value (hack) */
			if (!strptime(date, "%B %d %H:%M:%S.%Y %Y", &tms))
				return 1;
			sscanf(date, "%*s %*d %*d:%*d:%*d.%3u ", &msecs);
		}
	}

	//printf("h %d m %d s %d ms %03d d %d m %d y %d\n",
	//tms.tm_hour, tms.tm_min, tms.tm_sec, msecs,
	//tms.tm_mday, tms.tm_mon+1, tms.tm_year+1900);

	tv->tv_sec = mktime(&tms);
	tv->tv_usec = msecs * 1000;

	if (tv->tv_sec < 0)
		return 1;

	return 0;
}

int main(int argc, char **argv)
{
	char buf[BUFLEN], tmp1[BUFLEN], tmp2[BUFLEN];

	FILE *infile = stdin;
	FILE *outfile = stdout;
	static int verbose;
	static struct timeval tmp_tv; /* tmp frame timestamp from ASC file */
	static struct timeval date_tv; /* date of the ASC file */
	static int dplace; /* decimal place 4, 5 or 6 or uninitialized */
	static char base; /* 'd'ec or 'h'ex */
	static char timestamps; /* 'a'bsolute or 'r'elative */
	int opt;

	while ((opt = getopt(argc, argv, "I:O:v?")) != -1) {
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

		case 'v':
			verbose = 1;
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


	while (fgets(buf, BUFLEN-1, infile)) {

		if (!dplace) { /* the representation of a valid CAN frame not known */

			/* check for base and timestamp entries in the header */
			if ((!base) &&
			    (sscanf(buf, "base %s timestamps %s", tmp1, tmp2) == 2)) {
				base = tmp1[0];
				timestamps = tmp2[0];
				if (verbose)
					printf("base %c timestamps %c\n", base, timestamps);
				if ((base != 'h') && (base != 'd')) {
					printf("invalid base %s (must be 'hex' or 'dez')!\n",
					       tmp1);
					return 1;
				}
				if ((timestamps != 'a') && (timestamps != 'r')) {
					printf("invalid timestamps %s (must be 'absolute'"
					       " or 'relative')!\n", tmp2);
					return 1;
				}
				continue;
			}

			/* check for the original logging date in the header */ 
			if ((!date_tv.tv_sec) &&
			    (!strncmp(buf, "date", 4))) {

				if (get_date(&date_tv, &buf[9])) { /* skip 'date day ' */
					fprintf(stderr, "Not able to determine original log "
						"file date. Using current time.\n");
					/* use current date as default */
					gettimeofday(&date_tv, NULL);
				}
				if (verbose)
					printf("date %lu => %s", date_tv.tv_sec, ctime(&date_tv.tv_sec));
				continue;
			}

			/* check for decimal places length in valid CAN frames */
			if (sscanf(buf, "%lu.%s %s ", &tmp_tv.tv_sec, tmp2,
				   tmp1) != 3)
				continue; /* dplace remains zero until first found CAN frame */

			dplace = strlen(tmp2);
			if (verbose)
				printf("decimal place %d, e.g. '%s'\n", dplace,
				       tmp2);
			if (dplace < 4 || dplace > 6) {
				printf("invalid dplace %d (must be 4, 5 or 6)!\n",
				       dplace);
				return 1;
			}
		}

		/* the representation of a valid CAN frame is known here */
		/* so try to get CAN frames and ErrorFrames and convert them */

		/* check classic CAN format or the CANFD tag which can take both types */
		if (sscanf(buf, "%lu.%lu %s ", &tmp_tv.tv_sec,  &tmp_tv.tv_usec, tmp1) == 3){
			if (!strncmp(tmp1, "CANFD", 5))
				eval_canfd(buf, &date_tv, timestamps, dplace, outfile);
			else
				eval_can(buf, &date_tv, timestamps, base, dplace, outfile);
		}
	}
	fclose(outfile);
	fclose(infile);
	return 0;
}
