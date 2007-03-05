/*
 *  $Id$
 */

/*
 * slcan_attach.c - userspace tool for serial line CAN interface driver SLCAN
 *
 * Copyright (c) 2002-2005 Volkswagen Group Electronic Research
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, the following disclaimer and
 *    the referenced file 'COPYING'.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2 as distributed in the 'COPYING'
 * file from the main directory of the linux kernel source.
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
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <linux/tty.h>   /* thanks for cleanup since 2.6.21 */
//#include <asm/termios.h> /* ldiscs for each arch up to 2.6.20 */

#ifndef N_SLCAN
#define N_SLCAN 16 /* bad hack until it's not inside the Kernel */
#endif

void usage(char *name)
{
	fprintf(stderr, "Usage: %s [-d] [-l ldisc] tty\n", name);
	exit(1);
}

int main(int argc, char **argv)
{
	int fd;
	int ldisc = N_SLCAN; /* default */
	int detach = 0;
	char *tty;
	int opt;

	while ((opt = getopt(argc, argv, "l:d")) != -1) {
		switch (opt) {
		case 'l':
			ldisc = atoi(optarg);
			break;

		case 'd':
			detach = 1;
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	if (argc - optind != 1)
		usage(argv[0]);

	tty = argv[optind];

	if ((fd = open (tty, O_RDONLY | O_NOCTTY)) < 0) {
		perror(tty);
		exit(1);
	}

	if (detach)
		ldisc = N_TTY;

	if (ioctl (fd, TIOCSETD, &ldisc) < 0) {
		perror("ioctl");
		exit(1);
	}

	close(fd);

	return 0;
}
