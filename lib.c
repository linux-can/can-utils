/*
 *  $Id$
 */

/*
 * lib.c - library for command line tools
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
#include <stdint.h>

#include <sys/socket.h> /* for sa_family_t */
#include <linux/can.h>
#include <linux/can/error.h>

#include "lib.h"

#define CANID_DELIM '#'
#define DATA_SEPERATOR '.'

#define MAX_CANFRAME      "12345678#01.23.45.67.89.AB.CD.EF"
#define MAX_LONG_CANFRAME_SIZE 256

unsigned char asc2nibble(char c) {

	if ((c >= '0') && (c <= '9'))
		return c - '0';

	if ((c >= 'A') && (c <= 'F'))
		return c - 'A' + 10;

	if ((c >= 'a') && (c <= 'f'))
		return c - 'a' + 10;

	return 16; /* error */
}

int hexstring2candata(char *arg, struct can_frame *cf) {

	int len = strlen(arg);
	int i;
	unsigned char tmp;

	if (!len || len%2 || len > 16)
		return 1;

	for (i=0; i < len/2; i++) {

		tmp = asc2nibble(*(arg+(2*i)));
		if (tmp > 0x0F)
			return 1;

		cf->data[i] = (tmp << 4);

		tmp = asc2nibble(*(arg+(2*i)+1));
		if (tmp > 0x0F)
			return 1;

		cf->data[i] |= tmp;
	}

	return 0;
}

int parse_canframe(char *cs, struct can_frame *cf) {
	/* documentation see lib.h */

	int i, idx, dlc, len;
	unsigned char tmp;

	len = strlen(cs);
	//printf("'%s' len %d\n", cs, len);

	memset(cf, 0, sizeof(*cf)); /* init CAN frame, e.g. DLC = 0 */

	if (len < 4)
		return 1;

	if (cs[3] == CANID_DELIM) { /* 3 digits */

		idx = 4;
		for (i=0; i<3; i++){
			if ((tmp = asc2nibble(cs[i])) > 0x0F)
				return 1;
			cf->can_id |= (tmp << (2-i)*4);
		}

	} else if (cs[8] == CANID_DELIM) { /* 8 digits */

		idx = 9;
		for (i=0; i<8; i++){
			if ((tmp = asc2nibble(cs[i])) > 0x0F)
				return 1;
			cf->can_id |= (tmp << (7-i)*4);
		}
		if (!(cf->can_id & CAN_ERR_FLAG)) /* 8 digits but no errorframe?  */
			cf->can_id |= CAN_EFF_FLAG;   /* then it is an extended frame */

	} else
		return 1;

	if((cs[idx] == 'R') || (cs[idx] == 'r')){ /* RTR frame */
		cf->can_id |= CAN_RTR_FLAG;
		return 0;
	}

	for (i=0, dlc=0; i<8; i++){

		if(cs[idx] == DATA_SEPERATOR) /* skip (optional) seperator */
			idx++;

		if(idx >= len) /* end of string => end of data */
			break;

		if ((tmp = asc2nibble(cs[idx++])) > 0x0F)
			return 1;
		cf->data[i] = (tmp << 4);
		if ((tmp = asc2nibble(cs[idx++])) > 0x0F)
			return 1;
		cf->data[i] |= tmp;
		dlc++;
	}

	cf->can_dlc = dlc;

	return 0;
}

void fprint_canframe(FILE *stream , struct can_frame *cf, char *eol, int sep) {
	/* documentation see lib.h */

	char buf[sizeof(MAX_CANFRAME)+1]; /* max length */

	sprint_canframe(buf, cf, sep);
	fprintf(stream, "%s", buf);
	if (eol)
		fprintf(stream, "%s", eol);
}

void sprint_canframe(char *buf , struct can_frame *cf, int sep) {
	/* documentation see lib.h */

	int i,offset;
	int dlc = (cf->can_dlc > 8)? 8 : cf->can_dlc;

	if (cf->can_id & CAN_ERR_FLAG) {
		sprintf(buf, "%08X#", cf->can_id & (CAN_ERR_MASK|CAN_ERR_FLAG));
		offset = 9;
	} else if (cf->can_id & CAN_EFF_FLAG) {
		sprintf(buf, "%08X#", cf->can_id & CAN_EFF_MASK);
		offset = 9;
	} else {
		sprintf(buf, "%03X#", cf->can_id & CAN_SFF_MASK);
		offset = 4;
	}

	if (cf->can_id & CAN_RTR_FLAG) /* there are no ERR frames with RTR */
		sprintf(buf+offset, "R");
	else
		for (i = 0; i < dlc; i++) {
			sprintf(buf+offset, "%02X", cf->data[i]);
			offset += 2;
			if (sep && (i+1 < dlc))
				sprintf(buf+offset++, ".");
		}


}

void fprint_long_canframe(FILE *stream , struct can_frame *cf, char *eol, int view) {
	/* documentation see lib.h */

	char buf[MAX_LONG_CANFRAME_SIZE];

	sprint_long_canframe(buf, cf, view);
	fprintf(stream, "%s", buf);
	if ((view & CANLIB_VIEW_ERROR) && (cf->can_id & CAN_ERR_FLAG)) {
		snprintf_can_error_frame(buf, sizeof(buf), cf, "\n\t");
		fprintf(stream, "\n\t%s", buf);
	}
	if (eol)
		fprintf(stream, "%s", eol);
}

void sprint_long_canframe(char *buf , struct can_frame *cf, int view) {
	/* documentation see lib.h */

	int i, j, dlen, offset;
	int dlc = (cf->can_dlc > 8)? 8 : cf->can_dlc;

	if (cf->can_id & CAN_ERR_FLAG) {
		sprintf(buf, "%8X  ", cf->can_id & (CAN_ERR_MASK|CAN_ERR_FLAG));
		offset = 10;
	} else if (cf->can_id & CAN_EFF_FLAG) {
		sprintf(buf, "%8X  ", cf->can_id & CAN_EFF_MASK);
		offset = 10;
	} else {
		sprintf(buf, "%3X  ", cf->can_id & CAN_SFF_MASK);
		offset = 5;
	}

	sprintf(buf+offset, "[%d]", dlc);
	offset += 3;

	if (cf->can_id & CAN_RTR_FLAG) { /* there are no ERR frames with RTR */
		sprintf(buf+offset, " remote request");
		return;
	}

	if (view & CANLIB_VIEW_BINARY) {
		dlen = 9; /* _10101010 */
		if (view & CANLIB_VIEW_SWAP) {
			for (i = dlc - 1; i >= 0; i--) {
				buf[offset++] = (i == dlc-1)?' ':SWAP_DELIMITER;
				for (j = 7; j >= 0; j--)
					buf[offset++] = (1<<j & cf->data[i])?'1':'0';
			}
		} else {
			for (i = 0; i < dlc; i++) {
				buf[offset++] = ' ';
				for (j = 7; j >= 0; j--)
					buf[offset++] = (1<<j & cf->data[i])?'1':'0';
			}
		}
		buf[offset] = 0; /* terminate string */
	} else {
		dlen = 3; /* _AA */
		if (view & CANLIB_VIEW_SWAP) {
			for (i = dlc - 1; i >= 0; i--) {
				sprintf(buf+offset, "%c%02X",
					(i == dlc-1)?' ':SWAP_DELIMITER,
					cf->data[i]);
				offset += dlen;
			}
		} else {
			for (i = 0; i < dlc; i++) {
				sprintf(buf+offset, " %02X", cf->data[i]);
				offset += dlen;
			}
		}
	}

	if (cf->can_id & CAN_ERR_FLAG)
		sprintf(buf+offset, "%*s", dlen*(8-dlc)+13, "ERRORFRAME");
	else if (view & CANLIB_VIEW_ASCII) {
		j = dlen*(8-dlc)+4;
		if (view & CANLIB_VIEW_SWAP) {
			sprintf(buf+offset, "%*s", j, "`");
			offset += j;
			for (i = dlc - 1; i >= 0; i--)
				if ((cf->data[i] > 0x1F) && (cf->data[i] < 0x7F))
					buf[offset++] = cf->data[i];
				else
					buf[offset++] = '.';

			sprintf(buf+offset, "`");
		} else {
			sprintf(buf+offset, "%*s", j, "'");
			offset += j;
			for (i = 0; i < dlc; i++)
				if ((cf->data[i] > 0x1F) && (cf->data[i] < 0x7F))
					buf[offset++] = cf->data[i];
				else
					buf[offset++] = '.';

			sprintf(buf+offset, "'");
		}
	}
}

static const char *error_classes[] = {
	"tx-timeout",
	"lost-arbitration",
	"controller-problem",
	"protocol-violation",
	"transceiver-status",
	"no-acknowledgement-on-tx",
	"bus-off",
	"bus-error",
	"restarted-after-bus-off",
};

static const char *controller_problems[] = {
	"rx-overflow",
	"tx-overflow",
	"rx-error-warning",
	"tx-error-warning",
	"rx-error-passive",
	"tx-error-passive",
};

static const char *protocol_violation_types[] = {
	"single-bit-error",
	"frame-format-error",
	"bit-stuffing-error",
	"tx-dominant-bit-error",
	"tx-recessive-bit-error",
	"bus-overload",
	"back-to-error-active",
	"error-on-tx",
};

static const char *protocol_violation_locations[] = {
	"unspecified",
	"unspecified",
	"id.28-to-id.28",
	"start-of-frame",
	"bit-srtr",
	"bit-ide",
	"id.20-to-id.18",
	"id.17-to-id.13",
	"crc-sequence",
	"reserved-bit-0",
	"data-field",
	"data-length-code",
	"bit-rtr",
	"reserved-bit-1",
	"id.4-to-id.0",
	"id.12-to-id.5",
	"unspecified",
	"active-error-flag",
	"intermission",
	"tolerate-dominant-bits",
	"unspecified",
	"unspecified",
	"passive-error-flag",
	"error-delimiter",
	"crc-delimiter",
	"acknowledge-slot",
	"end-of-frame",
	"acknowledge-delimiter",
	"overload-flag",
	"unspecified",
	"unspecified",
	"unspecified",
};

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

static int snprintf_error_data(char *buf, size_t len, uint8_t err,
			       const char **arr, int arr_len)
{
	int i, n = 0, count = 0;

	if (!err || len <= 0)
		return 0;

	for (i = 0; i < arr_len; i++) {
		if (err & (1 << i)) {
			if (count)
				n += snprintf(buf + n, len - n, ",");
			n += snprintf(buf + n, len - n, "%s", arr[i]);
			count++;
		}
	}

	return n;
}

static int snprintf_error_lostarb(char *buf, size_t len, struct can_frame *cf)
{
	if (len <= 0)
		return 0;
	return snprintf(buf, len, "{at bit %d}", cf->data[0]);
}

static int snprintf_error_ctrl(char *buf, size_t len, struct can_frame *cf)
{
	int n = 0;

	if (len <= 0)
		return 0;

	n += snprintf(buf + n, len - n, "{");
	n += snprintf_error_data(buf + n, len - n, cf->data[1],
				controller_problems,
				ARRAY_SIZE(controller_problems));
	n += snprintf(buf + n, len - n, "}");

	return n;
}

static int snprintf_error_prot(char *buf, size_t len, struct can_frame *cf)
{
	int n = 0;

	if (len <= 0)
		return 0;

	n += snprintf(buf + n, len - n, "{{");
	n += snprintf_error_data(buf + n, len - n, cf->data[2],
				protocol_violation_types,
				ARRAY_SIZE(protocol_violation_types));
	n += snprintf(buf + n, len - n, "}{");
	if (cf->data[3] > 0 &&
	    cf->data[3] < ARRAY_SIZE(protocol_violation_locations))
		n += snprintf(buf + n, len - n, "%s",
			      protocol_violation_locations[cf->data[3]]);
	n += snprintf(buf + n, len - n, "}}");

	return n;
}

void snprintf_can_error_frame(char *buf, size_t len, struct can_frame *cf,
			      char* sep)
{
	canid_t class, mask;
	int i, n = 0, classes = 0;
	char *defsep = ",";

	if (!(cf->can_id & CAN_ERR_FLAG))
		return;

	class = cf->can_id & CAN_EFF_MASK;
	if (class > (1 << ARRAY_SIZE(error_classes))) {
		fprintf(stderr, "Error class %#x is invalid\n", class);
		return;
	}

	if (!sep)
		sep = defsep;

	for (i = 0; i < ARRAY_SIZE(error_classes); i++) {
		mask = 1 << i;
		if (class & mask) {
			if (classes)
				n += snprintf(buf + n, len - n, "%s", sep);
 			n += snprintf(buf + n, len - n, "%s", error_classes[i]);
			if (mask == CAN_ERR_LOSTARB)
				n += snprintf_error_lostarb(buf + n, len - n,
							   cf);
			if (mask == CAN_ERR_CRTL)
				n += snprintf_error_ctrl(buf + n, len - n, cf);
			if (mask == CAN_ERR_PROT)
				n += snprintf_error_prot(buf + n, len - n, cf);
			classes++;
		}
	}

	if (cf->data[6] || cf->data[7]) {
		n += snprintf(buf + n, len - n, "%s", sep);
		n += snprintf(buf + n, len - n, "error-counter-tx-rx{{%d}{%d}}",
			      cf->data[6], cf->data[7]);
	}
}
