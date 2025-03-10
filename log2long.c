/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * log2long.c - convert compact CAN frame representation into user readable
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

#include <linux/can.h>
#include <net/if.h>

#include "lib.h"

#define DEVSZ 22
#define TIMESZ 25 /* sizeof("(1345212884.318850123)   ") */
#define BUFSZ (DEVSZ + AFRSZ + TIMESZ)

/* adapt sscanf() functions below on error */
#if (AFRSZ != 6300)
#error "AFRSZ value does not fit sscanf restrictions!"
#endif
#if (DEVSZ != 22)
#error "DEVSZ value does not fit sscanf restrictions!"
#endif
#if (TIMESZ != 25)
#error "TIMESZ value does not fit sscanf restrictions!"
#endif

int main(void)
{
	static char buf[BUFSZ], timestamp[TIMESZ], device[DEVSZ], afrbuf[AFRSZ];
	static cu_t cu;
	int mtu;

	while (fgets(buf, BUFSZ-1, stdin)) {

		if (strlen(buf) >= BUFSZ-2) {
			fprintf(stderr, "line too long for input buffer\n");
			return 1;
		}

		if (sscanf(buf, "%24s %21s %6299s", timestamp, device, afrbuf) != 3)
			return 1;

		mtu = parse_canframe(afrbuf, &cu);

		/* mark dual-use struct canfd_frame - no CAN_XL support */
		if (mtu == CAN_MTU)
			cu.fd.flags = 0;
		else if (mtu == CANFD_MTU)
			cu.fd.flags |= CANFD_FDF;
		else {
			fprintf(stderr, "read: no valid CAN CC/FD frame\n");
			return 1;
		}

		/* with ASCII output */
		snprintf_long_canframe(afrbuf, sizeof(afrbuf), &cu,
				       (CANLIB_VIEW_INDENT_SFF | CANLIB_VIEW_ASCII));

		printf("%s  %s  %s\n", timestamp, device, afrbuf);
	}

	return 0;
}
