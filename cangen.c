/*
 *  $Id$
 */

/*
 * cangen.c - CAN frames generator for testing purposes
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <libgen.h>
#include <time.h>
#include <errno.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <net/if.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include "lib.h"

#define DEFAULT_GAP 200 /* ms */

extern int optind, opterr, optopt;

static volatile int running = 1;

void print_usage(char *prg)
{
    fprintf(stderr, "\n%s: generate random CAN frames\n\n", prg);
    fprintf(stderr, "Usage: %s [can-interface]\n", prg);
    fprintf(stderr, "Options: -g <ms>       (gap in milli seconds)  "
	    "default: %d\n", DEFAULT_GAP);
    fprintf(stderr, "         -e            (extended frame mode)   "
	    "default: standard frame format \n");
    fprintf(stderr, "         -I            (fixed CAN ID)          "
	    "default: 0x123\n");
    fprintf(stderr, "         -D            (fixed CAN Data)        "
	    "default: 01 23 45 67 89 AB CD EF\n");
    fprintf(stderr, "         -L            (fixed CAN DLC)         "
	    "default: 8\n");
    fprintf(stderr, "         -f <canframe> (other fixed CAN frame) "
	    "default: 123#0123456789ABCDEF\n");
    fprintf(stderr, "         -x            (disable echo)      "
	    "default: standard echo\n");
    fprintf(stderr, "         -v            (verbose)               "
	    "default: don't print sent frames\n");
}

void sigterm(int signo)
{
    running = 0;
}

int main(int argc, char **argv)
{
    unsigned long gap = DEFAULT_GAP; 
    unsigned char extended = 0;
    unsigned char fix_id = 0;
    unsigned char fix_data = 0;
    unsigned char fix_dlc = 0;
    unsigned char default_frame = 1;
    unsigned char echo_disable = 0;
    unsigned char verbose = 0;

    int opt;
    int s; /* socket */

    struct sockaddr_can addr;
    static struct can_frame frame;
    int nbytes;
    struct ifreq ifr;

    struct timespec ts;

    signal(SIGTERM, sigterm);
    signal(SIGHUP, sigterm);
    signal(SIGINT, sigterm);

    while ((opt = getopt(argc, argv, "g:eIDLf:xv")) != -1) {
	switch (opt) {
	case 'g':
	    gap = strtoul(optarg, NULL, 10);
	    break;

	case 'e':
	    extended = 1;
	    break;

	case 'I':
	    fix_id = 1;
	    break;

	case 'D':
	    fix_data = 1;
	    break;

	case 'L':
	    fix_dlc = 1;
	    break;

	case 'f':
	    default_frame = 0;
	    if (parse_canframe(optarg, &frame)) {
		fprintf(stderr, "'%s' is a wrong CAN frame format.\n", optarg);
		exit(1);
	    }
	    break;

	case 'v':
	    verbose = 1;
	    break;

	case 'x':
	    echo_disable = 1;
	    break;

	default:
	    print_usage(basename(argv[0]));
	    exit(1);
	    break;
	}
    }

    if (optind == argc) {
	print_usage(basename(argv[0]));
	exit(0);
    }

    ts.tv_sec = gap / 1000;
    ts.tv_nsec = (gap % 1000) * 1000000;


    if (default_frame) {
	if (extended)
	    frame.can_id = 0x12345678 | CAN_EFF_FLAG;
	else
	    frame.can_id = 0x123;

	frame.can_dlc = 8;

	frame.data[0] = 0x01;
	frame.data[1] = 0x23;
	frame.data[2] = 0x45;
	frame.data[3] = 0x67;
	frame.data[4] = 0x89;
	frame.data[5] = 0xAB;
	frame.data[6] = 0xCD;
	frame.data[7] = 0xEF;
    }

    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
	perror("socket");
	return 1;
    }

    addr.can_family = AF_CAN;

    strcpy(ifr.ifr_name, argv[optind]);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
	perror("SIOCGIFINDEX");
	return 1;
    }
    addr.can_ifindex = ifr.ifr_ifindex;

    /* disable default receive filter on this RAW socket */
    /* This is obsolete as we do not read from the socket at all, but for */
    /* this reason we can remove the receive list in the Kernel to save a */
    /* little (really a very little!) CPU usage.                          */
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);

    if (echo_disable) {
	int echo = 0;

	setsockopt(s, SOL_CAN_RAW, CAN_RAW_ECHO,
		   &echo, sizeof(echo));
    }

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	perror("bind");
	return 1;
    }

    while (running) {

	if (!fix_id) {
	    frame.can_id = random();
	    if (extended) {
		frame.can_id &= CAN_EFF_MASK;
		frame.can_id |= CAN_EFF_FLAG;
	    } else
		frame.can_id &= CAN_SFF_MASK;
	}

	if (!fix_dlc) {
	    frame.can_dlc = random() & 0xF;
	    if (frame.can_dlc & 8)
		frame.can_dlc = 8; /* for about 50% of the frames */
	}

	if (!fix_data) {
	    /* that's what the 64 bit alignment of data[] is for ... :) */
	    *(unsigned long*)(&frame.data[0]) = random();
	    *(unsigned long*)(&frame.data[4]) = random();
	}

	if ((nbytes = write(s, &frame, sizeof(struct can_frame))) < 0) {
	    perror("write");
	    return 1;
	} else if (nbytes < sizeof(struct can_frame)) {
	    fprintf(stderr, "write: incomplete CAN frame\n");
	    return 1;
	}

	if (gap) /* gap == 0 => performance test :-] */
	    if (nanosleep(&ts, NULL))
		return 1;
		    
	if (verbose)
#if 0
	    fprint_long_canframe(stdout, &frame, "\n", 1);
#else
	    fprint_canframe(stdout, &frame, "\n", 1);
#endif
    }

    close(s);

    return 0;
}
