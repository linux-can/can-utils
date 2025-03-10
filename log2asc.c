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

/* relevant flags in Flags field */
#define ASC_F_RTR 0x00000010
#define ASC_F_FDF 0x00001000
#define ASC_F_BRS 0x00002000
#define ASC_F_ESI 0x00004000
#define ASC_F_XLF 0x00400000
#define ASC_F_RES 0x00800000
#define ASC_F_SEC 0x01000000

extern int optind, opterr, optopt;

static void print_usage(char *prg)
{
	fprintf(stderr, "%s - convert compact CAN frame logfile to ASC logfile.\n", prg);
	fprintf(stderr, "Usage: %s <options> [can-interfaces]\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -I <infile>   (default stdin)\n");
	fprintf(stderr, "         -O <outfile>  (default stdout)\n");
	fprintf(stderr, "         -4  (reduce decimal place to 4 digits)\n");
	fprintf(stderr, "         -n  (set newline to cr/lf - default lf)\n");
	fprintf(stderr, "         -f  (use CANFD format also for CAN CC)\n");
	fprintf(stderr, "         -x  (use CANXL format also for CAN CC/FD)\n");
	fprintf(stderr, "         -r  (suppress dlc for RTR frames - pre v8.5 tools)\n");
}

static void can_asc(struct can_frame *cf, int devno, int nortrdlc,
		    char *extra_info, FILE *outfile)
{
	unsigned int i, dlc;
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

		fprintf(outfile, "%-15s %-4s ", id, dir);

		if (cf->len == CAN_MAX_DLC &&
		    cf->len8_dlc > CAN_MAX_DLC &&
		    cf->len8_dlc <= CAN_MAX_RAW_DLC)
			dlc = cf->len8_dlc;
		else
			dlc = cf->len;

		if (cf->can_id & CAN_RTR_FLAG) {
			if (nortrdlc)
				fprintf(outfile, "r"); /* RTR frame */
			else
				fprintf(outfile, "r %X", dlc); /* RTR frame */
		} else {
			fprintf(outfile, "d %X", dlc); /* data frame */

			for (i = 0; i < cf->len; i++) {
				fprintf(outfile, " %02X", cf->data[i]);
			}
		}
	}
}

static void canfd_asc(struct canfd_frame *cf, int devno, int mtu,
		      char *extra_info, FILE *outfile)
{
	unsigned int i;
	char id[10];
	char *dir = "Rx";
	unsigned int flags = 0;
	unsigned int dlen = cf->len;
	unsigned int dlc = can_fd_len2dlc(dlen);

	/* check for extra info */
	if (strlen(extra_info) > 0) {
		/* only the first char is defined so far */
		if (extra_info[0] == 'T')
			dir = "Tx";
	}

	fprintf(outfile, "CANFD %3d %-4s ", devno, dir); /* 3 column channel number right aligned */

	sprintf(id, "%X%c", cf->can_id & CAN_EFF_MASK,
		(cf->can_id & CAN_EFF_FLAG)?'x':' ');
	fprintf(outfile, "%11s                                  ", id);
	fprintf(outfile, "%c ", (cf->flags & CANFD_BRS)?'1':'0');
	fprintf(outfile, "%c ", (cf->flags & CANFD_ESI)?'1':'0');

	/* check for extra DLC when having a Classic CAN with 8 bytes payload */
	if ((mtu == CAN_MTU) && (dlen == CAN_MAX_DLEN)) {
		struct can_frame *ccf = (struct can_frame *)cf;

		if ((ccf->len8_dlc > CAN_MAX_DLEN) && (ccf->len8_dlc <= CAN_MAX_RAW_DLC))
			dlc = ccf->len8_dlc;
	}

	fprintf(outfile, "%x ", dlc);

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

	for (i = 0; i < dlen; i++) {
		fprintf(outfile, " %02X", cf->data[i]);
	}

	fprintf(outfile, " %8d %4d %8X 0 0 0 0 0", 130000, 130, flags);
}

static void canxl_asc(cu_t *cu, int devno, int mtu,
		      char *extra_info, FILE *outfile)
{
	char id[10];
	char *dir = "Rx";
	char *frametype;
	unsigned char *dataptr;
	unsigned int i, dlen, dlc, flags = 0;

	/* check for extra info */
	if (strlen(extra_info) > 0) {
		/* only the first char is defined so far */
		if (extra_info[0] == 'T')
			dir = "Tx";
	}

	switch (mtu) {
	case CANXL_MTU:
		sprintf(id, "%X", cu->xl.prio & CANXL_PRIO_MASK);
		frametype = "XLFF";

		dataptr = &cu->xl.data[0];
		dlen = cu->xl.len;
		dlc = dlen - 1;
		flags = (ASC_F_XLF | ASC_F_FDF | ASC_F_BRS);

		if (cu->xl.flags & CANXL_SEC)
			flags |= ASC_F_SEC;
		if (cu->xl.flags & CANXL_RRS)
			flags |= ASC_F_RES;
		break;

	case CANFD_MTU:
		if (cu->fd.can_id & CAN_EFF_FLAG) {
			sprintf(id, "%Xx", cu->fd.can_id & CAN_EFF_MASK);
			frametype = "FEFF";
		} else {
			sprintf(id, "%X", cu->fd.can_id & CAN_SFF_MASK);
			frametype = "FBFF";
		}

		dataptr = &cu->fd.data[0];
		dlen = cu->fd.len;
		dlc = can_fd_len2dlc(dlen);
		flags = ASC_F_FDF;
		if (cu->fd.flags & CANFD_BRS)
			flags |= ASC_F_BRS;
		if (cu->fd.flags & CANFD_ESI)
			flags |= ASC_F_ESI;
		break;

	case CAN_MTU:
		if (cu->cc.can_id & CAN_EFF_FLAG) {
			sprintf(id, "%Xx", cu->cc.can_id & CAN_EFF_MASK);
			frametype = "CEFF";
		} else {
			sprintf(id, "%X", cu->cc.can_id & CAN_SFF_MASK);
			frametype = "CBFF";
		}

		dataptr = &cu->cc.data[0];
		dlen = cu->cc.len;
		dlc = dlen ;

		/* check for extra DLC when having a Classic CAN with 8 bytes payload */
		if ((dlen == CAN_MAX_DLEN) && (cu->cc.len8_dlc > CAN_MAX_DLEN) &&
		    (cu->cc.len8_dlc <= CAN_MAX_RAW_DLC))
			dlc = cu->cc.len8_dlc;

		if (cu->cc.can_id & CAN_RTR_FLAG) {
			/* no data length but dlc for RTR frames */
			dlen = 0;
			flags = ASC_F_RTR;
		}
		break;

	default:
		return;
	}

	fprintf(outfile, "CANXL %3d %-4s ", devno, dir); /* 3 column channel number and direction */

	fprintf(outfile, "%s   984438   4656 ", frametype); /* frame type / msg duration / bit count */

	fprintf(outfile, "%9s                                  ", id); /* ID / symbolic name (empty) */

	if (mtu == CANXL_MTU) /* SDT, SEC bit for CAN XL only */
		fprintf(outfile, "%02x %d ", cu->xl.sdt, (cu->xl.flags & CANXL_SEC)?1:0);

	fprintf(outfile, "%x %d", dlc, dlen); /* DLC and data length */

	if (mtu == CANXL_MTU) /* SBC / PCRC / VCID / AF */
		fprintf(outfile, " 1 1f96 %02x %08x",
			(unsigned char)((cu->xl.prio >> CANXL_VCID_OFFSET) & CANXL_VCID_VAL_MASK),
			cu->xl.af);

	for (i = 0; i < dlen; i++) {
		fprintf(outfile, " %02x", dataptr[i]);
	}

	if (mtu == CANFD_MTU) /* stuff field */
		fprintf(outfile, " 8");

	fprintf(outfile, " 123123 %08x %08x", flags, 0); /* fcsc, msg flags, msg flags ext */

	fprintf(outfile, /* bitrate settings for CC/FD/XL */
		" 000000050005000e 0000000000a00010"
		" 0000000a000a001d 0000000000a00002"
		" 000000100010000f 0000000000a00001");
}

#define DEVSZ 22
#define EXTRASZ 20
#define TIMESZ sizeof("(1345212884.318850)   ")
#define BUFSZ (DEVSZ + AFRSZ + EXTRASZ + TIMESZ)

/* adapt sscanf() functions below on error */
#if (AFRSZ != 6300)
#error "AFRSZ value does not fit sscanf restrictions!"
#endif
#if (DEVSZ != 22)
#error "DEVSZ value does not fit sscanf restrictions!"
#endif
#if (EXTRASZ != 20)
#error "EXTRASZ value does not fit sscanf restrictions!"
#endif

int main(int argc, char **argv)
{
	static char buf[BUFSZ], device[DEVSZ], afrbuf[AFRSZ], extra_info[EXTRASZ];

	static cu_t cu;
	static struct timeval tv, start_tv;
	FILE *infile = stdin;
	FILE *outfile = stdout;
	static int maxdev, devno, i, crlf, fdfmt, xlfmt, nortrdlc, d4, opt, mtu;
	int print_banner = 1;
	unsigned long long sec, usec;

	while ((opt = getopt(argc, argv, "I:O:4nfxr?")) != -1) {
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

		case 'x':
			xlfmt = 1;
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

		if (sscanf(buf, "(%llu.%llu) %21s %6299s %19s", &sec, &usec,
			   device, afrbuf, extra_info) != 5) {

			/* do not evaluate the extra info */
			extra_info[0] = 0;

			if (sscanf(buf, "(%llu.%llu) %21s %6299s", &sec, &usec,
				   device, afrbuf) != 4) {
				fprintf(stderr, "incorrect line format in logfile\n");
				return 1;
			}
		}
		tv.tv_sec = sec;

		/*
		 * ensure the fractions of seconds are 6 or 9 decimal places long to catch
		 * 3rd party or handcrafted logfiles that treat the timestamp as float
		 */
		switch (strchr(buf, ')') - strchr(buf, '.')) {
		case 7: //6
			tv.tv_usec = usec;
			break;
		case 10: //9
			tv.tv_usec = usec / 1000;
			break;
		default:
			fprintf(stderr, "timestamp format in logfile requires 6 or 9 decimal places\n");
			return 1;
		}

		if (print_banner) { /* print banner */
			print_banner = 0;
			start_tv = tv;
			fprintf(outfile, "date %s", ctime(&start_tv.tv_sec));
			fprintf(outfile, "base hex  timestamps absolute%s",
				(crlf)?"\r\n":"\n");
			fprintf(outfile, "no internal events logged%s",
				(crlf)?"\r\n":"\n");
			fprintf(outfile, "// version 18.2.0%s", (crlf)?"\r\n":"\n");
			fprintf(outfile, "// Measurement UUID: cc9c7b54-68ae-"
				"46d2-a43a-6aa87df7dd74%s", (crlf)?"\r\n":"\n");
		}

		for (i = 0, devno = 0; i < maxdev; i++) {
			if (!strcmp(device, argv[optind+i])) {
				devno = i + 1; /* start with channel '1' */
				break;
			}
		}

		if (devno) { /* only convert for selected CAN devices */

			mtu = parse_canframe(afrbuf, &cu);

			/* no error message frames in non CAN CC frames */
			if ((mtu != CAN_MTU) && (cu.cc.can_id & CAN_ERR_FLAG))
				continue;

			tv.tv_sec  = tv.tv_sec - start_tv.tv_sec;
			tv.tv_usec = tv.tv_usec - start_tv.tv_usec;
			if (tv.tv_usec < 0)
				tv.tv_sec--, tv.tv_usec += 1000000;
			if (tv.tv_sec < 0)
				tv.tv_sec = tv.tv_usec = 0;

			if (d4)
				fprintf(outfile, "%4llu.%04llu ", (unsigned long long)tv.tv_sec, (unsigned long long)tv.tv_usec/100);
			else
				fprintf(outfile, "%4llu.%06llu ", (unsigned long long)tv.tv_sec, (unsigned long long)tv.tv_usec);

			if ((mtu == CAN_MTU) && (fdfmt == 0) && (xlfmt == 0))
				can_asc(&cu.cc, devno, nortrdlc, extra_info, outfile);
			else if ((mtu != CANXL_MTU) && (xlfmt == 0))
				canfd_asc(&cu.fd, devno, mtu, extra_info, outfile);
			else
				canxl_asc(&cu, devno, mtu, extra_info, outfile);

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
