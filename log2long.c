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

#include <net/if.h>
#include <linux/can.h>

#include "lib.h"

#define COMMENTSZ 200
#define BUFSZ (sizeof("(1345212884.318850)") + IFNAMSIZ + 4 + CL_CFSZ + COMMENTSZ) /* for one line in the logfile */

int main(void)
{
	char buf[BUFSZ], timestamp[BUFSZ], device[BUFSZ], ascframe[BUFSZ];
	struct canfd_frame cf;
	int mtu, maxdlen;

	while (fgets(buf, BUFSZ-1, stdin)) {
		if (sscanf(buf, "%s %s %s", timestamp, device, ascframe) != 3)
			return 1;

		mtu = parse_canframe(ascframe, &cf);
		if (mtu == CAN_MTU)
			maxdlen = CAN_MAX_DLEN;
		else if (mtu == CANFD_MTU)
			maxdlen = CANFD_MAX_DLEN;
		else {
			fprintf(stderr, "read: incomplete CAN frame\n");
			return 1;
		}

		sprint_long_canframe(ascframe, &cf,
				     (CANLIB_VIEW_INDENT_SFF | CANLIB_VIEW_ASCII),
				     maxdlen); /* with ASCII output */

		printf("%s  %s  %s\n", timestamp, device, ascframe);
	}

	return 0;
}
