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
#include <limits.h>

#include <linux/can.h>
#include <linux/can/error.h>
#include <net/if.h>

#include "lib.h"

#define BUFLEN 6500 /* CAN XL mode lines can be pretty long */
#define NO_DIR '.'

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
	fprintf(stderr, "%s - convert ASC logfile to compact CAN frame logfile.\n", prg);
	fprintf(stderr, "Usage: %s\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t-I <infile>\t(default stdin)\n");
	fprintf(stderr, "\t-O <outfile>\t(default stdout)\n");
	fprintf(stderr, "\t-d (disable direction information R/T)\n");
	fprintf(stderr, "\t-v (verbose)\n");
}

static void prframe(FILE *file, struct timeval *tv, int dev,
		    cu_t *cf, char dir)
{
	static char abuf[BUFLEN];

	fprintf(file, "(%llu.%06llu) ", (unsigned long long)tv->tv_sec, (unsigned long long)tv->tv_usec);

	if (dev > 0)
		fprintf(file, "can%d ", dev-1);
	else
		fprintf(file, "canX ");

	snprintf_canframe(abuf, sizeof(abuf), cf, 0);

	if (dir == NO_DIR)
		fprintf(file, "%s\n", abuf);
	else
		fprintf(file, "%s %c\n", abuf, dir);
}

static void get_can_id(canid_t *can_id, char *idstring, int base)
{
	if (idstring[strlen(idstring)-1] == 'x') {
		*can_id = CAN_EFF_FLAG;
		idstring[strlen(idstring)-1] = 0;
	} else
		*can_id = 0;
    
	*can_id |= strtoul(idstring, NULL, base);
}

static void calc_tv(struct timeval *tv, struct timeval *read_tv,
		    struct timeval *date_tv, char timestamps, int dplace)
{
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

	if (tv->tv_usec >= 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec++;
	}
}

static void eval_can(char* buf, struct timeval *date_tvp, char timestamps,
		     char base, int dplace, int disable_dir, FILE *outfile)
{
	int interface;
	static struct timeval tv; /* current frame timestamp */
	static struct timeval read_tv; /* frame timestamp from ASC file */
	struct canfd_frame cf = { 0 };
	struct can_frame *ccf = (struct can_frame *)&cf; /* for len8_dlc */
	char rtr;
	int dlc = 0;
	int len = 0;
	int data[8];
	char idstr[21];
	char dir[5]; /* 'Rx'/'Tx'/'TxRq' plus terminating zero */
	int i, items;
	unsigned long long sec, usec;

	/* check for ErrorFrames */
	if (sscanf(buf, "%llu.%llu %d %20s",
		   &sec, &usec,
		   &interface, idstr) == 4) {
		read_tv.tv_sec = sec;
		read_tv.tv_usec = usec;

		if (!strncmp(idstr, "ErrorFrame", strlen("ErrorFrame"))) {

			/* do not know more than 'Error' */
			cf.can_id = (CAN_ERR_FLAG | CAN_ERR_BUSERROR);
			cf.len = CAN_ERR_DLC;

			calc_tv(&tv, &read_tv, date_tvp, timestamps, dplace);
			prframe(outfile, &tv, interface, (cu_t *)&cf, NO_DIR);
			fflush(outfile);
			return;
		}
	}

	/* 0.002367 1 390x Rx d 8 17 00 14 00 C0 00 08 00 */

	/* check for CAN frames with (hexa)decimal values */
	if (base == 'h')
		items = sscanf(buf, "%llu.%llu %d %20s %4s %c %x %x %x %x %x %x %x %x %x",
			       &sec, &usec, &interface,
			       idstr, dir, &rtr, &dlc,
			       &data[0], &data[1], &data[2], &data[3],
			       &data[4], &data[5], &data[6], &data[7]);
	else
		items = sscanf(buf, "%llu.%llu %d %20s %4s %c %x %d %d %d %d %d %d %d %d",
			       &sec, &usec, &interface,
			       idstr, dir, &rtr, &dlc,
			       &data[0], &data[1], &data[2], &data[3],
			       &data[4], &data[5], &data[6], &data[7]);

	read_tv.tv_sec = sec;
	read_tv.tv_usec = usec;
	if (items < 7 ) /* make sure we've read the dlc */
		return;

	/* dlc is one character hex value 0..F */
	if (dlc > CAN_MAX_RAW_DLC)
		return;

	/* retrieve real data length */
	if (dlc > CAN_MAX_DLC)
		len = CAN_MAX_DLEN;
	else
		len = dlc;

	if ((items == len + 7 ) || /* data frame */
	    ((items == 6) && (rtr == 'r')) || /* RTR without DLC */
	    ((items == 7) && (rtr == 'r'))) { /* RTR with DLC */

		/* check for CAN ID with (hexa)decimal value */
		if (base == 'h')
			get_can_id(&cf.can_id, idstr, 16);
		else
			get_can_id(&cf.can_id, idstr, 10);

		/* dlc > 8 => len == CAN_MAX_DLEN => fill len8_dlc value */
		if (dlc > CAN_MAX_DLC)
			ccf->len8_dlc = dlc;

		if (strlen(dir) != 2) /* "Rx" or "Tx" */
			return;

		if (disable_dir)
			dir[0] = NO_DIR;

		/* check for signed integer overflow */
		if (dplace == 4 && read_tv.tv_usec >= INT_MAX / 100)
			return;

		if (dplace == 5 && read_tv.tv_usec >= INT_MAX / 10)
			return;

		cf.len = len;
		if (rtr == 'r')
			cf.can_id |= CAN_RTR_FLAG;
		else
			for (i = 0; i < len; i++)
				cf.data[i] = data[i] & 0xFFU;

		calc_tv(&tv, &read_tv, date_tvp, timestamps, dplace);
		prframe(outfile, &tv, interface, (cu_t *)&cf, dir[0]);
		fflush(outfile);
	}
}

static void eval_canfd(char* buf, struct timeval *date_tvp, char timestamps,
		       int dplace, int disable_dir, FILE *outfile)
{
	int interface;
	static struct timeval tv; /* current frame timestamp */
	static struct timeval read_tv; /* frame timestamp from ASC file */
	struct canfd_frame cf = { 0 };
	unsigned char brs, esi, ctmp;
	unsigned int flags;
	int dlc, dlen = 0;
	char idstr[21];
	char dir[5]; /* 'Rx'/'Tx'/'TxRq' plus terminating zero */
	char *ptr;
	int i;
	int n = 0; /* sscanf consumed characters */
	unsigned long long sec, usec;

	/*
	 * The CANFD format is mainly in hex representation but <DataLength>
	 * and probably some content we skip anyway. Don't trust the docs!
	 */

	/*
	 * 21.671796 CANFD   1 Tx         11  msgCanFdFr1                      1 0 a 16 \
	 * 00 00 00 00 00 00 00 00 00 00 00 00 00 00 59 c0		\
	 * 100000  214   223040 80000000 46500250 460a0250 20011736 20010205
	 */

	/* check for valid line without symbolic name */
	if (sscanf(buf, "%llu.%llu %*s %d %4s %20s %hhx %hhx %x %d %n",
		   &sec, &usec, &interface,
		   dir, idstr, &brs, &esi, &dlc, &dlen, &n) != 9) {

		/* check for valid line with a symbolic name */
		if (sscanf(buf, "%llu.%llu %*s %d %4s %20s %*s %hhx %hhx %x %d %n",
			   &sec, &usec, &interface,
			   dir, idstr, &brs, &esi, &dlc, &dlen, &n) != 9) {

			/* no valid CANFD format pattern */
			return;
		}
	}
	read_tv.tv_sec = sec;
	read_tv.tv_usec = usec;

	/* check for allowed (unsigned) value ranges */
	if ((dlen > CANFD_MAX_DLEN) || (dlc > CANFD_MAX_DLC) ||
	    (brs > 1) || (esi > 1))
		return;

	if (strlen(dir) != 2) /* "Rx" or "Tx" */
		return;

	if (disable_dir)
		dir[0] = NO_DIR;

	/* check for signed integer overflow */
	if (dplace == 4 && read_tv.tv_usec >= INT_MAX / 100)
		return;

	/* check for signed integer overflow */
	if (dplace == 5 && read_tv.tv_usec >= INT_MAX / 10)
		return;

	/* don't trust ASCII content - sanitize data length */
	if (dlen != can_fd_dlc2len(can_fd_len2dlc(dlen)))
		return;

	get_can_id(&cf.can_id, idstr, 16);

	ptr = buf + n; /* start of ASCII hex frame data */

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

	if (flags & ASC_F_FDF) {
		cf.flags = CANFD_FDF;
		if (flags & ASC_F_BRS)
			cf.flags |= CANFD_BRS;
		if (flags & ASC_F_ESI)
			cf.flags |= CANFD_ESI;
	} else {
		/* yes. The 'CANFD' format supports classic CAN content! */
		if (flags & ASC_F_RTR) {
			cf.can_id |= CAN_RTR_FLAG;
			/* dlen is always 0 for classic CAN RTR frames
			   but the DLC value is valid in RTR cases */
			cf.len = dlc;
			/* sanitize payload length value */
			if (dlc > CAN_MAX_DLEN)
				cf.len = CAN_MAX_DLEN;
		}
		/* check for extra DLC when having a Classic CAN with 8 bytes payload */
		if ((cf.len == CAN_MAX_DLEN) && (dlc > CAN_MAX_DLEN) && (dlc <= CAN_MAX_RAW_DLC)) {
			struct can_frame *ccf = (struct can_frame *)&cf;

			ccf->len8_dlc = dlc;
		}
	}

	calc_tv(&tv, &read_tv, date_tvp, timestamps, dplace);
	prframe(outfile, &tv, interface, (cu_t *)&cf, dir[0]);
	fflush(outfile);

	/* No support for really strange CANFD ErrorFrames format m( */
}

static void eval_canxl_cc(char* buf, struct timeval *date_tvp, char timestamps,
			  int dplace, int disable_dir, FILE *outfile)
{
	int interface;
	static struct timeval tv; /* current frame timestamp */
	static struct timeval read_tv; /* frame timestamp from ASC file */
	struct can_frame cf = { 0 };
	unsigned char ctmp;
	unsigned int flags;
	int dlc, dlen = 0;
	char idstr[21];
	char dir[5]; /* 'Rx'/'Tx'/'TxRq' plus terminating zero */
	char *ptr;
	int i;
	int n = 0; /* sscanf consumed characters */
	unsigned long long sec, usec;

	/*
	 * 59.171614 CANXL   2 Rx   CBFF   243215   176      432  msgCanCCTest1 \
	 * f 8 e1 89 e8 c2 b9 6d 5a f1 174 00000000 00000000		\
	 * 000000050005000e 0000000000a00010 0000000a000a001d		\
	 * 0000000000a00002 000000100010000f 000000000a00001
	 */

	/* check for valid line without symbolic name */
	if (sscanf(buf,
		   "%llu.%llu %*s %d %4s " /* time, CANXL, channel, direction */
		   "%*s %*s %*s %20s " /* frame format, msg dur, bit count, ID */
		   "%x %d %n", /* DLC, Datalen */
		   &sec, &usec, &interface, dir,
		   idstr,
		   &dlc, &dlen, &n) != 7) {
		/* check for valid line with a symbolic name */
		if (sscanf(buf,
			   "%llu.%llu %*s %d %4s " /* time, CANXL, channel, direction */
			   "%*s %*s %*s %20s " /* frame format, msg dur, bit count, ID */
			   "%*s %x %d %n", /* sym name, DLC, Datalen */
			   &sec, &usec, &interface, dir,
			   idstr,
			   &dlc, &dlen, &n) != 7) {
			/* no valid CAN CC format pattern */
			return;
		}
	}

	read_tv.tv_sec = sec;
	read_tv.tv_usec = usec;

	/* check for allowed (unsigned) value ranges */
	if ((dlen > CAN_MAX_DLEN) || (dlc > CAN_MAX_RAW_DLC))
		return;

	if (strlen(dir) != 2) /* "Rx" or "Tx" */
		return;

	if (disable_dir)
		dir[0] = NO_DIR;

	/* check for signed integer overflow */
	if (dplace == 4 && read_tv.tv_usec >= INT_MAX / 100)
		return;

	/* check for signed integer overflow */
	if (dplace == 5 && read_tv.tv_usec >= INT_MAX / 10)
		return;

	get_can_id(&cf.can_id, idstr, 16);

	ptr = buf + n; /* start of ASCII hex frame data */

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

	/* skip FCRC to get Flags value */
	if (sscanf(ptr, "%*x %x ", &flags) != 1)
		return;

	if (flags & ASC_F_RTR) {
		cf.can_id |= CAN_RTR_FLAG;
		/* dlen is always 0 for classic CAN RTR frames
		   but the DLC value is valid in RTR cases */
		cf.len = dlc;
		/* sanitize payload length value */
		if (dlc > CAN_MAX_DLEN)
			cf.len = CAN_MAX_DLEN;
	}

	/* check for extra DLC when having a Classic CAN with 8 bytes payload */
	if ((cf.len == CAN_MAX_DLEN) && (dlc > CAN_MAX_DLEN) && (dlc <= CAN_MAX_RAW_DLC))
		cf.len8_dlc = dlc;

	calc_tv(&tv, &read_tv, date_tvp, timestamps, dplace);
	prframe(outfile, &tv, interface, (cu_t *)&cf, dir[0]);
	fflush(outfile);
}

static void eval_canxl_fd(char* buf, struct timeval *date_tvp, char timestamps,
			  int dplace, int disable_dir, FILE *outfile)
{
	int interface;
	static struct timeval tv; /* current frame timestamp */
	static struct timeval read_tv; /* frame timestamp from ASC file */
	struct canfd_frame cf = { 0 };
	unsigned char ctmp;
	unsigned int flags;
	int dlc, dlen = 0;
	char idstr[21];
	char dir[5]; /* 'Rx'/'Tx'/'TxRq' plus terminating zero */
	char *ptr;
	int i;
	int n = 0; /* sscanf consumed characters */
	unsigned long long sec, usec;

	/*
	 * 59.171614 CANXL   2 Rx   FBFF   243215   176      432  msgCanFDTest2 \
	 * 9 12 e1 89 e8 c2 b9 6d 5a f1 11 22 33 44 a 12345 00001240 00000000 \
	 * 000000050005000e 0000000000a00010 0000000a000a001d		\
	 * 0000000000a00002 000000100010000f 000000000a00001
	 */

	/* check for valid line without symbolic name */
	if (sscanf(buf,
		   "%llu.%llu %*s %d %4s " /* time, CANXL, channel, direction */
		   "%*s %*s %*s %20s " /* frame format, msg dur, bit count, ID */
		   "%x %d %n", /* DLC, Datalen */
		   &sec, &usec, &interface, dir,
		   idstr,
		   &dlc, &dlen, &n) != 7) {
		/* check for valid line with a symbolic name */
		if (sscanf(buf,
			   "%llu.%llu %*s %d %4s " /* time, CANXL, channel, direction */
			   "%*s %*s %*s %20s " /* frame format, msg dur, bit count, ID */
			   "%*s %x %d %n", /* sym name, DLC, Datalen */
			   &sec, &usec, &interface, dir,
			   idstr,
			   &dlc, &dlen, &n) != 7) {
			/* no valid CAN CC format pattern */
			return;
		}
	}

	read_tv.tv_sec = sec;
	read_tv.tv_usec = usec;

	/* check for allowed (unsigned) value ranges */
	if ((dlen > CANFD_MAX_DLEN) || (dlc > CANFD_MAX_DLC))
		return;

	if (strlen(dir) != 2) /* "Rx" or "Tx" */
		return;

	if (disable_dir)
		dir[0] = NO_DIR;

	/* check for signed integer overflow */
	if (dplace == 4 && read_tv.tv_usec >= INT_MAX / 100)
		return;

	/* check for signed integer overflow */
	if (dplace == 5 && read_tv.tv_usec >= INT_MAX / 10)
		return;

	get_can_id(&cf.can_id, idstr, 16);

	ptr = buf + n; /* start of ASCII hex frame data */

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

	/* skip stuff field and FCRC to get Flags value */
	if (sscanf(ptr, "%*s %*s %x ", &flags) != 1)
		return;

	if (!(flags & ASC_F_FDF))
		return;

	cf.flags = CANFD_FDF;

	if (flags & ASC_F_BRS)
		cf.flags |= CANFD_BRS;
	if (flags & ASC_F_ESI)
		cf.flags |= CANFD_ESI;

	calc_tv(&tv, &read_tv, date_tvp, timestamps, dplace);
	prframe(outfile, &tv, interface, (cu_t *)&cf, dir[0]);
	fflush(outfile);
}

static void eval_canxl_xl(char* buf, struct timeval *date_tvp, char timestamps,
			  int dplace, int disable_dir, FILE *outfile)
{
	int interface;
	static struct timeval tv; /* current frame timestamp */
	static struct timeval read_tv; /* frame timestamp from ASC file */
	struct canxl_frame cf = { 0 };
	unsigned char sdt, vcid, secbit, ctmp;
	unsigned int af, flags;
	int dlc, dlen = 0;
	char idstr[21];
	char dir[5]; /* 'Rx'/'Tx'/'TxRq' plus terminating zero */
	char *ptr;
	int i;
	int n = 0; /* sscanf consumed characters */
	unsigned long long sec, usec;

	/*
	 * 59.171614 CANXL   2 Rx   XLFF   984438   4656      432  msgCanXlTest1 \
	 * e0 0  1fe  511 1 1f96 00 00000000 e1 89 e8 c2 b9 6d 5a f1 c5 97 ( .. ) \
	 * ( .. ) c7 e3 4e f6 bf 12cfbd62 02503000 00000000 000000050005000e \
	 * 0000000000a00010 0000000a000a001d 0000000000a00002 000000100010000f \
	 * 000000000a00001
	 */

	/* check for valid line without symbolic name */
	if (sscanf(buf,
		   "%llu.%llu %*s %d %4s " /* time, CANXL, channel, direction */
		   "%*s %*s %*s %20s " /* frame format, msg dur, bit count, ID */
		   "%hhx %hhx %x %d " /* SDT, SEC, DLC, Datalen */
		   "%*s %*s %hhx %x %n", /* stuff bit count, crc, VCID, AF */
		   &sec, &usec, &interface, dir,
		   idstr,
		   &sdt, &secbit, &dlc, &dlen,
		   &vcid, &af, &n) != 11) {
		/* check for valid line with a symbolic name */
		if (sscanf(buf,
			   "%llu.%llu %*s %d %4s " /* time, CANXL, channel, direction */
			   "%*s %*s %*s %20s " /* frame format, msg dur, bit count, ID */
			   "%*s %hhx %hhx %x %d " /* sym name, SDT, SEC, DLC, Datalen */
			   "%*s %*s %hhx %x %n", /* stuff bit count, crc, VCID, AF */
			   &sec, &usec, &interface, dir,
			   idstr,
			   &sdt, &secbit, &dlc, &dlen,
			   &vcid, &af, &n) != 11) {

			/* no valid CANXL format pattern */
			return;
		}
	}

	read_tv.tv_sec = sec;
	read_tv.tv_usec = usec;

	/* check for allowed (unsigned) value ranges */
	if ((dlen > CANXL_MAX_DLEN) || (dlc > CANXL_MAX_DLC) || (secbit > 1))
		return;

	cf.sdt = sdt;
	cf.af = af;

	if (strlen(dir) != 2) /* "Rx" or "Tx" */
		return;

	if (disable_dir)
		dir[0] = NO_DIR;

	/* check for signed integer overflow */
	if (dplace == 4 && read_tv.tv_usec >= INT_MAX / 100)
		return;

	/* check for signed integer overflow */
	if (dplace == 5 && read_tv.tv_usec >= INT_MAX / 10)
		return;

	/* don't trust ASCII content - sanitize data length */
	if (dlen != dlc + 1)
		return;

	get_can_id(&cf.prio, idstr, 16);

	if ((cf.prio & CANXL_PRIO_MASK) != cf.prio)
		return;

	if (vcid)
		cf.prio |= (vcid << CANXL_VCID_OFFSET);

	ptr = buf + n; /* start of ASCII hex frame data */

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

	/* skip FCRC to get Flags value */
	if (sscanf(ptr, "%*x %x ", &flags) != 1)
		return;

	/* mandatory for CAN XL frames */
	if (!(flags & ASC_F_XLF))
		return;

	/* mark as CAN XL */
	cf.flags = CANXL_XLF;

	if (flags & ASC_F_SEC)
		cf.flags |= CANXL_SEC;

	if (flags & ASC_F_RES)
		cf.flags |= CANXL_RRS;

	calc_tv(&tv, &read_tv, date_tvp, timestamps, dplace);
	prframe(outfile, &tv, interface, (cu_t *)&cf, dir[0]);
	fflush(outfile);

	/* No support for CAN XL ErrorFrames */
}

static void eval_canxl(char* buf, struct timeval *date_tvp, char timestamps,
		       int dplace, int disable_dir, FILE *outfile)
{
	int interface;
	char dir[5]; /* 'Rx'/'Tx'/'TxRq' plus terminating zero */
	char frfo[5]; /* frame format 'CBFF'/'CEFF'/'FBFF'/'FEFF'/'XLFF' plus terminating zero */
	unsigned long long sec, usec;

	/*
	 * The CANXL format is mainly in hex representation but <DataLength>
	 * and probably some content we skip anyway. Check out the new spec:
	 * CAN, Log & Trigger ASC Logging Format Spec V 1.4.17 of 2024-05-21
	 */

	/*
	 * This is a CAN XL ("XLFF") example for the CANXL Message Event:
	 *
	 * 59.171614 CANXL   2 Rx   XLFF   984438   4656      432  msgCanXlTest1 \
	 * e0 0  1fe  511 1 1f96 00 00000000 e1 89 e8 c2 b9 6d 5a f1 c5 97 ( .. ) \
	 * ( .. ) c7 e3 4e f6 bf 12cfbd62 02503000 00000000 000000050005000e \
	 * 0000000000a00010 0000000a000a001d 0000000000a00002 000000100010000f \
	 * 000000000a00001
	 */

	/* check for valid line until frame format tag */
	if (sscanf(buf, "%llu.%llu %*s %d %4s %4s ",
		   &sec, &usec, &interface, dir, frfo) != 5)
		return; /* no valid CANXL format pattern */

	if (strlen(dir) != 2) /* "Rx" or "Tx" */
		return;

	if (strlen(frfo) != 4) /* 'CBFF'/'CEFF'/'FBFF'/'FEFF'/'XLFF' */
		return;

	if (!strncmp(frfo, "XLFF", 4))
		eval_canxl_xl(buf, date_tvp, timestamps, dplace, disable_dir, outfile);
	else if (!strncmp(frfo, "FBFF", 4))
		eval_canxl_fd(buf, date_tvp, timestamps, dplace, disable_dir, outfile);
	else if (!strncmp(frfo, "FEFF", 4))
		eval_canxl_fd(buf, date_tvp, timestamps, dplace, disable_dir, outfile);
	else if (!strncmp(frfo, "CBFF", 4))
		eval_canxl_cc(buf, date_tvp, timestamps, dplace, disable_dir, outfile);
	else if (!strncmp(frfo, "CEFF", 4))
		eval_canxl_cc(buf, date_tvp, timestamps, dplace, disable_dir, outfile);
}

static int get_date(struct timeval *tv, char *date)
{
	struct tm tms;
	unsigned int msecs = 0;

	if ((strcasestr(date, " am ") != NULL) || (strcasestr(date, " pm ") != NULL)) {
		/* assume EN/US date due to existing am/pm field */

		if (!setlocale(LC_TIME, "en_US")) {
			fprintf(stderr, "Setting locale to 'en_US' failed!\n");
			return 1;
		}

		if (!strptime(date, "%B %d %I:%M:%S %p %Y", &tms)) {
			/*
			 * The string might contain a milliseconds value which strptime()
			 * does not support. So we read the ms value into the year variable
			 * before parsing the real year value (hack)
			 */
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
			/*
			 * The string might contain a milliseconds value which strptime()
			 * does not support. So we read the ms value into the year variable
			 * before parsing the real year value (hack)
			 */
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
	char buf[BUFLEN], tmp1[10], tmp2[10];

	FILE *infile = stdin;
	FILE *outfile = stdout;
	static int verbose, disable_dir;
	static struct timeval date_tv; /* date of the ASC file */
	static int dplace; /* decimal place 4, 5 or 6 or uninitialized */
	static char base; /* 'd'ec or 'h'ex */
	static char timestamps; /* 'a'bsolute or 'r'elative */
	int opt;
	unsigned long long sec, usec;

	while ((opt = getopt(argc, argv, "I:O:dv?")) != -1) {
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

		case 'd':
			disable_dir = 1;
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
			    (sscanf(buf, "base %9s timestamps %9s", tmp1, tmp2) == 2)) {
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
					printf("date %llu => %s", (unsigned long long)date_tv.tv_sec, ctime(&date_tv.tv_sec));
				continue;
			}

			/* check for decimal places length in valid CAN frames */
			if (sscanf(buf, "%llu.%9s %9s ", &sec, tmp2,
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

		/*
		 * The representation of a valid CAN frame is known here.
		 * So try to get CAN frames and ErrorFrames and convert them.
		 */

		/* check classic CAN format or the CANFD/CANXL tag which can take different types */
		if (sscanf(buf, "%llu.%llu %9s ", &sec,  &usec, tmp1) == 3) {
			if (!strncmp(tmp1, "CANXL", 5))
				eval_canxl(buf, &date_tv, timestamps, dplace, disable_dir, outfile);
			else if (!strncmp(tmp1, "CANFD", 5))
				eval_canfd(buf, &date_tv, timestamps, dplace, disable_dir, outfile);
			else
				eval_can(buf, &date_tv, timestamps, base, dplace, disable_dir, outfile);
		}
	}
	fclose(outfile);
	fclose(infile);
	return 0;
}
