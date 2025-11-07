/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * cansend.c - send CAN-frames via CAN_RAW sockets
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include "lib.h"

static void print_usage(char *prg)
{
	fprintf(stderr,
		"%s - send CAN-frames via CAN_RAW sockets.\n"
		"\n"
		"Usage: %s <device> <can_frame>.\n"
		"\n"
		"<can_frame>:\n"
		" <can_id>#{data}          for CAN CC (Classical CAN 2.0B) data frames\n"
		" <can_id>#R{len}          for CAN CC (Classical CAN 2.0B) data frames\n"
		" <can_id>#{data}_{dlc}    for CAN CC (Classical CAN 2.0B) data frames\n"
		" <can_id>#R{len}_{dlc}    for CAN CC (Classical CAN 2.0B) data frames\n"
		" <can_id>##<flags>{data}  for CAN FD frames\n"
		" <vcid><prio>#<flags>:<sdt>:<af>#<data> for CAN XL frames\n"
		"\n"
		"<can_id>:\n"
		" 3 (SFF) or 8 (EFF) hex chars\n"
		"{data}:\n"
		" 0..8 (0..64 CAN FD) ASCII hex-values (optionally separated by '.')\n"
		"{len}:\n"
		" an optional 0..8 value as RTR frames can contain a valid dlc field\n"
		"_{dlc}:\n"
		" an optional 9..F data length code value when payload length is 8\n"
		"<flags>:\n"
		" a single ASCII Hex value (0 .. F) which defines canfd_frame.flags:\n"
		" %x CANFD_BRS\n"
		" %x CANFD_ESI\n"
		" %x CANFD_FDF\n"
		"\n"
		"<vcid>:\n"
		" 2 hex chars - virtual CAN network identifier (00 .. FF)\n"
		"<prio>:\n"
		" 3 hex chars - 11 bit priority value (000 .. 7FF)\n"
		"<flags>:\n"
		" 2 hex chars values (00 .. FF) which defines canxl_frame.flags\n"
		"<sdt>:\n"
		" 2 hex chars values (00 .. FF) which defines canxl_frame.sdt\n"
		"<af>:\n"
		" 8 hex chars - 32 bit acceptance field (canxl_frame.af)\n"
		"<data>:\n"
		" 1..2048 ASCII hex-values (optionally separated by '.')\n"
		"\n"
		"Examples:\n"
		"  5A1#11.2233.44556677.88 / 123#DEADBEEF / 5AA# / 123##1 / 213##311223344 /\n"
		"  1F334455#1122334455667788_B / 123#R / 00000123#R3 / 333#R8_E /\n"
		"  45123#81:00:12345678#11223344.556677 / 00242#81:07:40000123#112233\n"
		"\n",
		prg, prg,
		CANFD_BRS, CANFD_ESI, CANFD_FDF);
}

int main(int argc, char **argv)
{
	int s; /* can raw socket */
	int required_mtu;
	int mtu;
	int enable_canfx = 1;
	struct sockaddr_can addr;
	struct can_raw_vcid_options vcid_opts = {
		.flags = CAN_RAW_XL_VCID_TX_PASS,
	};
	static cu_t cu;
	struct ifreq ifr;

	/* check command line options */
	if (argc != 3) {
		print_usage(argv[0]);
		return 1;
	}

	/* parse CAN frame */
	required_mtu = parse_canframe(argv[2], &cu);
	if (!required_mtu) {
		fprintf(stderr, "\nWrong CAN-frame format!\n\n");
		print_usage(argv[0]);
		return 1;
	}

	/* open socket */
	if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
		perror("socket");
		return 1;
	}

	strncpy(ifr.ifr_name, argv[1], IFNAMSIZ - 1);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';
	ifr.ifr_ifindex = if_nametoindex(ifr.ifr_name);
	if (!ifr.ifr_ifindex) {
		perror("if_nametoindex");
		return 1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;

	if (required_mtu > (int)CAN_MTU) {
		/* check if the frame fits into the CAN netdevice */
		if (ioctl(s, SIOCGIFMTU, &ifr) < 0) {
			perror("SIOCGIFMTU");
			return 1;
		}
		mtu = ifr.ifr_mtu;

		if (mtu == (int)CANFD_MTU) {
			/* interface is ok - try to switch the socket into CAN FD mode */
			if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
				       &enable_canfx, sizeof(enable_canfx))){
				printf("error when enabling CAN FD support\n");
				return 1;
			}
		}

		if (mtu >= (int)CANXL_MIN_MTU) {
			/* interface is ok - try to switch the socket into CAN XL mode */
			if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_XL_FRAMES,
				       &enable_canfx, sizeof(enable_canfx))){
				printf("error when enabling CAN XL support\n");
				return 1;
			}
			/* try to enable the CAN XL VCID pass through mode */
			if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_XL_VCID_OPTS,
				       &vcid_opts, sizeof(vcid_opts))) {
				printf("error when enabling CAN XL VCID pass through\n");
				return 1;
			}
		}
	}

	/* ensure discrete CAN FD length values 0..8, 12, 16, 20, 24, 32, 64 */
	if (required_mtu == CANFD_MTU)
		cu.fd.len = can_fd_dlc2len(can_fd_len2dlc(cu.fd.len));

	/* CAN XL frames need real frame length for sending */
	if (required_mtu == CANXL_MTU)
		required_mtu = CANXL_HDR_SIZE + cu.xl.len;

	/*
	 * disable default receive filter on this RAW socket This is
	 * obsolete as we do not read from the socket at all, but for
	 * this reason we can remove the receive list in the Kernel to
	 * save a little (really a very little!) CPU usage.
	 */
	setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	/* send frame */
	if (write(s, &cu, required_mtu) != required_mtu) {
		perror("write");
		return 1;
	}

	close(s);

	return 0;
}
