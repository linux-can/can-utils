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
 * Send feedback to <socketcan-users@lists.berlios.de>
 *
 */

#include <stdio.h>
#include <string.h>

#include <sys/socket.h> /* for sa_family_t */
#include <linux/can.h>

#include "lib.h"

#define CANID_DELIM '#'
#define DATA_SEPERATOR '.'

#define MAX_CANFRAME      "12345678#01.23.45.67.89.AB.CD.EF"
#define MAX_LONG_CANFRAME "12345678  [8] 01 23 45 67 89 AB CD EF   '........'"

static int asc2nibble(char c) {

    if ((c >= '0') && (c <= '9'))
	return c - '0';

    if ((c >= 'A') && (c <= 'F'))
	return c - 'A' + 10;

    if ((c >= 'a') && (c <= 'f'))
	return c - 'a' + 10;

    return 16; /* error */
}

int parse_canframe(char *cs, struct can_frame *cf) {
    /* documentation see lib.h */

    int i, idx, dlc, len, tmp;

    len = strlen(cs);
    //printf("'%s' len %d\n", cs, len);

    memset(cf, 0, sizeof(*cf)); /* init CAN frame, e.g. DLC = 0 */

    if (len < 4)
	return 1;

    if (!((cs[3] == CANID_DELIM) || (cs[8] == CANID_DELIM)))
	return 1;

    if (cs[8] == CANID_DELIM) { /* 8 digits */

	idx = 9;
	for (i=0; i<8; i++){
	    if ((tmp = asc2nibble(cs[i])) > 0x0F)
		return 1;
	    cf->can_id |= (tmp << (7-i)*4);
	}
	if (!(cf->can_id & CAN_ERR_FLAG)) /* 8 digits but no errorframe?  */
	    cf->can_id |= CAN_EFF_FLAG;   /* then it is an extended frame */

    } else { /* 3 digits */

	idx = 4;
	for (i=0; i<3; i++){
	    if ((tmp = asc2nibble(cs[i])) > 0x0F)
		return 1;
	    cf->can_id |= (tmp << (2-i)*4);
	}
    }

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
	for (i = 0; i < cf->can_dlc; i++) {
	    sprintf(buf+offset, "%02X", cf->data[i]);
	    offset += 2;
	    if (sep && (i+1 < cf->can_dlc))
		sprintf(buf+offset++, ".");
	}


}

void fprint_long_canframe(FILE *stream , struct can_frame *cf, char *eol, int ascii) {
    /* documentation see lib.h */

    char buf[sizeof(MAX_LONG_CANFRAME)+1]; /* max length */

    sprint_long_canframe(buf, cf, ascii);
    fprintf(stream, "%s", buf);
    if (eol)
	fprintf(stream, "%s", eol);
}

void sprint_long_canframe(char *buf , struct can_frame *cf, int ascii) {
    /* documentation see lib.h */

    int i, offset;

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

    sprintf(buf+offset, "[%d]", cf->can_dlc);
    offset += 3;

    if (cf->can_id & CAN_RTR_FLAG) /* there are no ERR frames with RTR */
	sprintf(buf+offset, " remote request");
    else {
	for (i = 0; i < cf->can_dlc; i++) {
	    sprintf(buf+offset, " %02X", cf->data[i]);
	    offset += 3;
	}
	if (cf->can_id & CAN_ERR_FLAG)
	    sprintf(buf+offset, "%*s", 3*(8-cf->can_dlc)+13, "ERRORFRAME");
	else if (ascii) {
	    sprintf(buf+offset, "%*s", 3*(8-cf->can_dlc)+4, "'");
	    offset += 3*(8-cf->can_dlc)+4;

	    for (i = 0; i < cf->can_dlc; i++)
		if ((cf->data[i] > 0x1F) && (cf->data[i] < 0x7F))
		    buf[offset++] = cf->data[i];
		else
		    buf[offset++] = '.';
	    sprintf(buf+offset, "'");
	} 
    }
}

