/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * isotprecv.c
 *
 * Copyright (c) 2008 Volkswagen Group Electronic Research
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <linux/can.h>
#include <linux/can/isotp.h>

#define NO_CAN_ID 0xFFFFFFFFU
#define BUFSIZE 67000 /* size > 66000 to check socket API internal checks */

void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s [options] <CAN interface>\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -s <can_id>   (source can_id. Use 8 digits for extended IDs)\n");
	fprintf(stderr, "         -d <can_id>   (destination can_id. Use 8 digits for extended IDs)\n");
	fprintf(stderr, "         -x <addr>[:<rxaddr>]  (extended addressing / opt. separate rxaddr)\n");
	fprintf(stderr, "         -p [tx]:[rx]  (set and enable tx/rx padding bytes)\n");
	fprintf(stderr, "         -P <mode>     (check rx padding for (l)ength (c)ontent (a)ll)\n");
	fprintf(stderr, "         -b <bs>       (blocksize. 0 = off)\n");
	fprintf(stderr, "         -m <val>      (STmin in ms/ns. See spec.)\n");
	fprintf(stderr, "         -f <time ns>  (force rx stmin value in nanosecs)\n");
	fprintf(stderr, "         -w <num>      (max. wait frame transmissions.)\n");
	fprintf(stderr, "         -l            (loop: do not exit after pdu reception.)\n");
	fprintf(stderr, "         -L <mtu>:<tx_dl>:<tx_flags>  (link layer options for CAN FD)\n");
	fprintf(stderr, "\nCAN IDs and addresses are given and expected in hexadecimal values.\n");
	fprintf(stderr, "The pdu data is written on STDOUT in space separated ASCII hex values.\n");
	fprintf(stderr, "\n");
}

int main(int argc, char **argv)
{
    int s;
    struct sockaddr_can addr;
    static struct can_isotp_options opts;
    static struct can_isotp_fc_options fcopts;
    static struct can_isotp_ll_options llopts;
    int opt, i;
    extern int optind, opterr, optopt;
    __u32 force_rx_stmin = 0;
    int loop = 0;

    unsigned char msg[BUFSIZE];
    int nbytes;

    addr.can_addr.tp.tx_id = addr.can_addr.tp.rx_id = NO_CAN_ID;

    while ((opt = getopt(argc, argv, "s:d:x:p:P:b:m:w:f:lL:?")) != -1) {
	    switch (opt) {
	    case 's':
		    addr.can_addr.tp.tx_id = strtoul(optarg, NULL, 16);
		    if (strlen(optarg) > 7)
			    addr.can_addr.tp.tx_id |= CAN_EFF_FLAG;
		    break;

	    case 'd':
		    addr.can_addr.tp.rx_id = strtoul(optarg, NULL, 16);
		    if (strlen(optarg) > 7)
			    addr.can_addr.tp.rx_id |= CAN_EFF_FLAG;
		    break;

	    case 'x':
	    {
		    int elements = sscanf(optarg, "%hhx:%hhx",
					  &opts.ext_address,
					  &opts.rx_ext_address);

		    if (elements == 1)
			    opts.flags |= CAN_ISOTP_EXTEND_ADDR;
		    else if (elements == 2)
			    opts.flags |= (CAN_ISOTP_EXTEND_ADDR | CAN_ISOTP_RX_EXT_ADDR);
		    else {
			    printf("incorrect extended addr values '%s'.\n", optarg);
			    print_usage(basename(argv[0]));
			    exit(0);
		    }
		    break;
	    }

	    case 'p':
	    {
		    int elements = sscanf(optarg, "%hhx:%hhx",
					  &opts.txpad_content,
					  &opts.rxpad_content);

		    if (elements == 1)
			    opts.flags |= CAN_ISOTP_TX_PADDING;
		    else if (elements == 2)
			    opts.flags |= (CAN_ISOTP_TX_PADDING | CAN_ISOTP_RX_PADDING);
		    else if (sscanf(optarg, ":%hhx", &opts.rxpad_content) == 1)
			    opts.flags |= CAN_ISOTP_RX_PADDING;
		    else {
			    printf("incorrect padding values '%s'.\n", optarg);
			    print_usage(basename(argv[0]));
			    exit(0);
		    }
		    break;
	    }

	    case 'P':
		    if (optarg[0] == 'l')
			    opts.flags |= CAN_ISOTP_CHK_PAD_LEN;
		    else if (optarg[0] == 'c')
			    opts.flags |= CAN_ISOTP_CHK_PAD_DATA;
		    else if (optarg[0] == 'a')
			    opts.flags |= (CAN_ISOTP_CHK_PAD_LEN | CAN_ISOTP_CHK_PAD_DATA);
		    else {
			    printf("unknown padding check option '%c'.\n", optarg[0]);
			    print_usage(basename(argv[0]));
			    exit(0);
		    }
		    break;

	    case 'b':
		    fcopts.bs = strtoul(optarg, NULL, 16) & 0xFF;
		    break;

	    case 'm':
		    fcopts.stmin = strtoul(optarg, NULL, 16) & 0xFF;
		    break;

	    case 'w':
		    fcopts.wftmax = strtoul(optarg, NULL, 16) & 0xFF;
		    break;

	    case 'f':
		    opts.flags |= CAN_ISOTP_FORCE_RXSTMIN;
		    force_rx_stmin = strtoul(optarg, NULL, 10);
		    break;

	    case 'l':
		    loop = 1;
		    break;

	    case 'L':
		    if (sscanf(optarg, "%hhu:%hhu:%hhu",
			       &llopts.mtu,
			       &llopts.tx_dl,
			       &llopts.tx_flags) != 3) {
			    printf("unknown link layer options '%s'.\n", optarg);
			    print_usage(basename(argv[0]));
			    exit(0);
		    }
		    break;

	    case '?':
		    print_usage(basename(argv[0]));
		    exit(0);
		    break;

	    default:
		    fprintf(stderr, "Unknown option %c\n", opt);
		    print_usage(basename(argv[0]));
		    exit(1);
		    break;
	    }
    }

    if ((argc - optind != 1) ||
	(addr.can_addr.tp.tx_id == NO_CAN_ID) ||
	(addr.can_addr.tp.rx_id == NO_CAN_ID)) {
	    print_usage(basename(argv[0]));
	    exit(1);
    }
  
    if ((s = socket(PF_CAN, SOCK_DGRAM, CAN_ISOTP)) < 0) {
	perror("socket");
	exit(1);
    }

    setsockopt(s, SOL_CAN_ISOTP, CAN_ISOTP_OPTS, &opts, sizeof(opts));
    setsockopt(s, SOL_CAN_ISOTP, CAN_ISOTP_RECV_FC, &fcopts, sizeof(fcopts));

    if (llopts.tx_dl) {
	if (setsockopt(s, SOL_CAN_ISOTP, CAN_ISOTP_LL_OPTS, &llopts, sizeof(llopts)) < 0) {
	    perror("link layer sockopt");
	    exit(1);
	}
    }

    if (opts.flags & CAN_ISOTP_FORCE_RXSTMIN)
	    setsockopt(s, SOL_CAN_ISOTP, CAN_ISOTP_RX_STMIN, &force_rx_stmin, sizeof(force_rx_stmin));

    addr.can_family = AF_CAN;
    addr.can_ifindex = if_nametoindex(argv[optind]);
    if (!addr.can_ifindex) {
	perror("if_nametoindex");
	exit(1);
    }

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	perror("bind");
	close(s);
	exit(1);
    }

    do {
	    nbytes = read(s, msg, BUFSIZE);
	    if (nbytes > 0 && nbytes < BUFSIZE)
		    for (i=0; i < nbytes; i++)
			    printf("%02X ", msg[i]);
	    printf("\n");
    } while (loop);

    close(s);

    return 0;
}
