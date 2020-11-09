/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <linux/can.h>
#include <linux/can/error.h>
#include <sys/socket.h> /* for sa_family_t */

#include "lib.h"

#define CANID_DELIM '#'
#define CC_DLC_DELIM '_'
#define DATA_SEPERATOR '.'

const char hex_asc_upper[] = "0123456789ABCDEF";

#define hex_asc_upper_lo(x)	hex_asc_upper[((x) & 0x0F)]
#define hex_asc_upper_hi(x)	hex_asc_upper[((x) & 0xF0) >> 4]

static inline void put_hex_byte(char *buf, __u8 byte)
{
	buf[0] = hex_asc_upper_hi(byte);
	buf[1] = hex_asc_upper_lo(byte);
}

static inline void _put_id(char *buf, int end_offset, canid_t id)
{
	/* build 3 (SFF) or 8 (EFF) digit CAN identifier */
	while (end_offset >= 0) {
		buf[end_offset--] = hex_asc_upper_lo(id);
		id >>= 4;
	}
}

#define put_sff_id(buf, id) _put_id(buf, 2, id)
#define put_eff_id(buf, id) _put_id(buf, 7, id)

/* CAN DLC to real data length conversion helpers */

static const unsigned char dlc2len[] = {0, 1, 2, 3, 4, 5, 6, 7,
					8, 12, 16, 20, 24, 32, 48, 64};

/* get data length from raw data length code (DLC) */
unsigned char can_fd_dlc2len(unsigned char dlc)
{
	return dlc2len[dlc & 0x0F];
}

static const unsigned char len2dlc[] = {0, 1, 2, 3, 4, 5, 6, 7, 8,		/* 0 - 8 */
					9, 9, 9, 9,				/* 9 - 12 */
					10, 10, 10, 10,				/* 13 - 16 */
					11, 11, 11, 11,				/* 17 - 20 */
					12, 12, 12, 12,				/* 21 - 24 */
					13, 13, 13, 13, 13, 13, 13, 13,		/* 25 - 32 */
					14, 14, 14, 14, 14, 14, 14, 14,		/* 33 - 40 */
					14, 14, 14, 14, 14, 14, 14, 14,		/* 41 - 48 */
					15, 15, 15, 15, 15, 15, 15, 15,		/* 49 - 56 */
					15, 15, 15, 15, 15, 15, 15, 15};	/* 57 - 64 */

/* map the sanitized data length to an appropriate data length code */
unsigned char can_fd_len2dlc(unsigned char len)
{
	if (len > 64)
		return 0xF;

	return len2dlc[len];
}

unsigned char asc2nibble(char c) {

	if ((c >= '0') && (c <= '9'))
		return c - '0';

	if ((c >= 'A') && (c <= 'F'))
		return c - 'A' + 10;

	if ((c >= 'a') && (c <= 'f'))
		return c - 'a' + 10;

	return 16; /* error */
}

int hexstring2data(char *arg, unsigned char *data, int maxdlen) {

	int len = strlen(arg);
	int i;
	unsigned char tmp;

	if (!len || len%2 || len > maxdlen*2)
		return 1;

	memset(data, 0, maxdlen);

	for (i=0; i < len/2; i++) {

		tmp = asc2nibble(*(arg+(2*i)));
		if (tmp > 0x0F)
			return 1;

		data[i] = (tmp << 4);

		tmp = asc2nibble(*(arg+(2*i)+1));
		if (tmp > 0x0F)
			return 1;

		data[i] |= tmp;
	}

	return 0;
}

int parse_canframe(char *cs, struct canfd_frame *cf) {
	/* documentation see lib.h */

	int i, idx, dlen, len;
	int maxdlen = CAN_MAX_DLEN;
	int ret = CAN_MTU;
	unsigned char tmp;

	len = strlen(cs);
	//printf("'%s' len %d\n", cs, len);

	memset(cf, 0, sizeof(*cf)); /* init CAN FD frame, e.g. LEN = 0 */

	if (len < 4)
		return 0;

	if (cs[3] == CANID_DELIM) { /* 3 digits */

		idx = 4;
		for (i=0; i<3; i++){
			if ((tmp = asc2nibble(cs[i])) > 0x0F)
				return 0;
			cf->can_id |= (tmp << (2-i)*4);
		}

	} else if (cs[8] == CANID_DELIM) { /* 8 digits */

		idx = 9;
		for (i=0; i<8; i++){
			if ((tmp = asc2nibble(cs[i])) > 0x0F)
				return 0;
			cf->can_id |= (tmp << (7-i)*4);
		}
		if (!(cf->can_id & CAN_ERR_FLAG)) /* 8 digits but no errorframe?  */
			cf->can_id |= CAN_EFF_FLAG;   /* then it is an extended frame */

	} else
		return 0;

	if((cs[idx] == 'R') || (cs[idx] == 'r')){ /* RTR frame */
		cf->can_id |= CAN_RTR_FLAG;

		/* check for optional DLC value for CAN 2.0B frames */
		if(cs[++idx] && (tmp = asc2nibble(cs[idx++])) <= CAN_MAX_DLEN) {
			cf->len = tmp;

			/* check for optional raw DLC value for CAN 2.0B frames */
			if ((tmp == CAN_MAX_DLEN) && (cs[idx++] == CC_DLC_DELIM)) {
				tmp = asc2nibble(cs[idx]);
				if ((tmp > CAN_MAX_DLEN) && (tmp <= CAN_MAX_RAW_DLC)) {
					struct can_frame *ccf = (struct can_frame *)cf;

					ccf->len8_dlc = tmp;
				}
			}
		}
		return ret;
	}

	if (cs[idx] == CANID_DELIM) { /* CAN FD frame escape char '##' */

		maxdlen = CANFD_MAX_DLEN;
		ret = CANFD_MTU;

		/* CAN FD frame <canid>##<flags><data>* */
		if ((tmp = asc2nibble(cs[idx+1])) > 0x0F)
			return 0;

		cf->flags = tmp;
		idx += 2;
	}

	for (i=0, dlen=0; i < maxdlen; i++){

		if(cs[idx] == DATA_SEPERATOR) /* skip (optional) separator */
			idx++;

		if(idx >= len) /* end of string => end of data */
			break;

		if ((tmp = asc2nibble(cs[idx++])) > 0x0F)
			return 0;
		cf->data[i] = (tmp << 4);
		if ((tmp = asc2nibble(cs[idx++])) > 0x0F)
			return 0;
		cf->data[i] |= tmp;
		dlen++;
	}
	cf->len = dlen;

	/* check for extra DLC when having a Classic CAN with 8 bytes payload */
	if ((maxdlen == CAN_MAX_DLEN) && (dlen == CAN_MAX_DLEN) && (cs[idx++] == CC_DLC_DELIM)) {
		unsigned char dlc = asc2nibble(cs[idx]);

		if ((dlc > CAN_MAX_DLEN) && (dlc <= CAN_MAX_RAW_DLC)) {
			struct can_frame *ccf = (struct can_frame *)cf;

			ccf->len8_dlc = dlc;
		}
	}

	return ret;
}

void fprint_canframe(FILE *stream , struct canfd_frame *cf, char *eol, int sep, int maxdlen) {
	/* documentation see lib.h */

	char buf[CL_CFSZ]; /* max length */

	sprint_canframe(buf, cf, sep, maxdlen);
	fprintf(stream, "%s", buf);
	if (eol)
		fprintf(stream, "%s", eol);
}

void sprint_canframe(char *buf , struct canfd_frame *cf, int sep, int maxdlen) {
	/* documentation see lib.h */

	int i,offset;
	int len = (cf->len > maxdlen) ? maxdlen : cf->len;

	if (cf->can_id & CAN_ERR_FLAG) {
		put_eff_id(buf, cf->can_id & (CAN_ERR_MASK|CAN_ERR_FLAG));
		buf[8] = '#';
		offset = 9;
	} else if (cf->can_id & CAN_EFF_FLAG) {
		put_eff_id(buf, cf->can_id & CAN_EFF_MASK);
		buf[8] = '#';
		offset = 9;
	} else {
		put_sff_id(buf, cf->can_id & CAN_SFF_MASK);
		buf[3] = '#';
		offset = 4;
	}

	/* standard CAN frames may have RTR enabled. There are no ERR frames with RTR */
	if (maxdlen == CAN_MAX_DLEN && cf->can_id & CAN_RTR_FLAG) {
		buf[offset++] = 'R';
		/* print a given CAN 2.0B DLC if it's not zero */
		if (cf->len && cf->len <= CAN_MAX_DLEN) {
			buf[offset++] = hex_asc_upper_lo(cf->len);

			/* check for optional raw DLC value for CAN 2.0B frames */
			if (cf->len == CAN_MAX_DLEN) {
				struct can_frame *ccf = (struct can_frame *)cf;

				if ((ccf->len8_dlc > CAN_MAX_DLEN) && (ccf->len8_dlc <= CAN_MAX_RAW_DLC)) {
					buf[offset++] = CC_DLC_DELIM;
					buf[offset++] = hex_asc_upper_lo(ccf->len8_dlc);
				}
			}
		}

		buf[offset] = 0;
		return;
	}

	if (maxdlen == CANFD_MAX_DLEN) {
		/* add CAN FD specific escape char and flags */
		buf[offset++] = '#';
		buf[offset++] = hex_asc_upper_lo(cf->flags);
		if (sep && len)
			buf[offset++] = '.';
	}

	for (i = 0; i < len; i++) {
		put_hex_byte(buf + offset, cf->data[i]);
		offset += 2;
		if (sep && (i+1 < len))
			buf[offset++] = '.';
	}

	/* check for extra DLC when having a Classic CAN with 8 bytes payload */
	if ((maxdlen == CAN_MAX_DLEN) && (len == CAN_MAX_DLEN)) {
		struct can_frame *ccf = (struct can_frame *)cf;
		unsigned char dlc = ccf->len8_dlc;

		if ((dlc > CAN_MAX_DLEN) && (dlc <= CAN_MAX_RAW_DLC)) {
			buf[offset++] = CC_DLC_DELIM;
			buf[offset++] = hex_asc_upper_lo(dlc);
		}
	}

	buf[offset] = 0;
}

void fprint_long_canframe(FILE *stream , struct canfd_frame *cf, char *eol, int view, int maxdlen) {
	/* documentation see lib.h */

	char buf[CL_LONGCFSZ];

	sprint_long_canframe(buf, cf, view, maxdlen);
	fprintf(stream, "%s", buf);
	if ((view & CANLIB_VIEW_ERROR) && (cf->can_id & CAN_ERR_FLAG)) {
		snprintf_can_error_frame(buf, sizeof(buf), cf, "\n\t");
		fprintf(stream, "\n\t%s", buf);
	}
	if (eol)
		fprintf(stream, "%s", eol);
}

void sprint_long_canframe(char *buf , struct canfd_frame *cf, int view, int maxdlen) {
	/* documentation see lib.h */

	int i, j, dlen, offset;
	int len = (cf->len > maxdlen)? maxdlen : cf->len;

	/* initialize space for CAN-ID and length information */
	memset(buf, ' ', 15);

	if (cf->can_id & CAN_ERR_FLAG) {
		put_eff_id(buf, cf->can_id & (CAN_ERR_MASK|CAN_ERR_FLAG));
		offset = 10;
	} else if (cf->can_id & CAN_EFF_FLAG) {
		put_eff_id(buf, cf->can_id & CAN_EFF_MASK);
		offset = 10;
	} else {
		if (view & CANLIB_VIEW_INDENT_SFF) {
			put_sff_id(buf + 5, cf->can_id & CAN_SFF_MASK);
			offset = 10;
		} else {
			put_sff_id(buf, cf->can_id & CAN_SFF_MASK);
			offset = 5;
		}
	}

	/* The len value is sanitized by maxdlen (see above) */
	if (maxdlen == CAN_MAX_DLEN) {
		if (view & CANLIB_VIEW_LEN8_DLC) {
			struct can_frame *ccf = (struct can_frame *)cf;
			unsigned char dlc = ccf->len8_dlc;

			/* fall back to len if we don't have a valid DLC > 8 */
			if (!((len == CAN_MAX_DLEN) && (dlc > CAN_MAX_DLEN) &&
			      (dlc <= CAN_MAX_RAW_DLC)))
				dlc = len;

			buf[offset + 1] = '{';
			buf[offset + 2] = hex_asc_upper[dlc];
			buf[offset + 3] = '}';
		} else {
			buf[offset + 1] = '[';
			buf[offset + 2] = len + '0';
			buf[offset + 3] = ']';
		}

		/* standard CAN frames may have RTR enabled */
		if (cf->can_id & CAN_RTR_FLAG) {
			sprintf(buf+offset+5, " remote request");
			return;
		}
	} else {
		buf[offset] = '[';
		buf[offset + 1] = (len/10) + '0';
		buf[offset + 2] = (len%10) + '0';
		buf[offset + 3] = ']';
	}
	offset += 5;

	if (view & CANLIB_VIEW_BINARY) {
		dlen = 9; /* _10101010 */
		if (view & CANLIB_VIEW_SWAP) {
			for (i = len - 1; i >= 0; i--) {
				buf[offset++] = (i == len-1)?' ':SWAP_DELIMITER;
				for (j = 7; j >= 0; j--)
					buf[offset++] = (1<<j & cf->data[i])?'1':'0';
			}
		} else {
			for (i = 0; i < len; i++) {
				buf[offset++] = ' ';
				for (j = 7; j >= 0; j--)
					buf[offset++] = (1<<j & cf->data[i])?'1':'0';
			}
		}
	} else {
		dlen = 3; /* _AA */
		if (view & CANLIB_VIEW_SWAP) {
			for (i = len - 1; i >= 0; i--) {
				if (i == len-1)
					buf[offset++] = ' ';
				else
					buf[offset++] = SWAP_DELIMITER;

				put_hex_byte(buf + offset, cf->data[i]);
				offset += 2;
			}
		} else {
			for (i = 0; i < len; i++) {
				buf[offset++] = ' ';
				put_hex_byte(buf + offset, cf->data[i]);
				offset += 2;
			}
		}
	}

	buf[offset] = 0; /* terminate string */

	/*
	 * The ASCII & ERRORFRAME output is put at a fixed len behind the data.
	 * For now we support ASCII output only for payload length up to 8 bytes.
	 * Does it make sense to write 64 ASCII byte behind 64 ASCII HEX data on the console?
	 */
	if (len > CAN_MAX_DLEN)
		return;

	if (cf->can_id & CAN_ERR_FLAG)
		sprintf(buf+offset, "%*s", dlen*(8-len)+13, "ERRORFRAME");
	else if (view & CANLIB_VIEW_ASCII) {
		j = dlen*(8-len)+4;
		if (view & CANLIB_VIEW_SWAP) {
			sprintf(buf+offset, "%*s", j, "`");
			offset += j;
			for (i = len - 1; i >= 0; i--)
				if ((cf->data[i] > 0x1F) && (cf->data[i] < 0x7F))
					buf[offset++] = cf->data[i];
				else
					buf[offset++] = '.';

			sprintf(buf+offset, "`");
		} else {
			sprintf(buf+offset, "%*s", j, "'");
			offset += j;
			for (i = 0; i < len; i++)
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
	"back-to-error-active",
};

static const char *protocol_violation_types[] = {
	"single-bit-error",
	"frame-format-error",
	"bit-stuffing-error",
	"tx-dominant-bit-error",
	"tx-recessive-bit-error",
	"bus-overload",
	"active-error",
	"error-on-tx",
};

static const char *protocol_violation_locations[] = {
	"unspecified",
	"unspecified",
	"id.28-to-id.21",
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

static int snprintf_error_lostarb(char *buf, size_t len, const struct canfd_frame *cf)
{
	if (len <= 0)
		return 0;
	return snprintf(buf, len, "{at bit %d}", cf->data[0]);
}

static int snprintf_error_ctrl(char *buf, size_t len, const struct canfd_frame *cf)
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

static int snprintf_error_prot(char *buf, size_t len, const struct canfd_frame *cf)
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

void snprintf_can_error_frame(char *buf, size_t len, const struct canfd_frame *cf,
                  const char* sep)
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

	for (i = 0; i < (int)ARRAY_SIZE(error_classes); i++) {
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
