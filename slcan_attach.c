/*
 *  $Id$
 */

/*
 * slcan_attach.c - userspace tool for serial line CAN interface driver SLCAN
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
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/ioctl.h>

#define LDISC_N_SLCAN 17 /* default slcan line discipline since Kernel 2.6.25 */

void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s [options] tty\n\n", prg);
	fprintf(stderr, "Options: -o         (send open command 'O\\r')\n");
	fprintf(stderr, "         -c         (send close command 'C\\r')\n");
	fprintf(stderr, "         -s <speed> (set CAN speed 0..8)\n");
	fprintf(stderr, "         -b <btr>   (set bit time register value)\n");
	fprintf(stderr, "         -d         (only detach line discipline)\n");
	fprintf(stderr, "         -w         (attach - wait for keypess - detach)\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "slcan_attach -w -o -s6 -c /dev/ttyS1\n");
	fprintf(stderr, "slcan_attach /dev/ttyS1\n");
	fprintf(stderr, "slcan_attach -d /dev/ttyS1\n");
	fprintf(stderr, "\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int fd;
	int ldisc = LDISC_N_SLCAN;
	int detach = 0;
	int waitkey = 0;
	int send_open = 0;
	int send_close = 0;
	char *speed = NULL;
	char *btr = NULL;
	char buf[10];
	char *tty;
	int opt;

	while ((opt = getopt(argc, argv, "l:dwocs:b:?")) != -1) {
		switch (opt) {
		case 'l':
			fprintf(stderr, "Ignored option '-l'\n");
			break;

		case 'd':
			detach = 1;
			break;

		case 'w':
			waitkey = 1;
			break;

		case 'o':
			send_open = 1;
			break;

		case 'c':
			send_close = 1;
			break;

		case 's':
			speed = optarg;
			if (strlen(speed) > 1)
				print_usage(argv[0]);
			break;

		case 'b':
			btr = optarg;
			if (strlen(btr) > 6)
				print_usage(argv[0]);
			break;

		case '?':
		default:
			print_usage(argv[0]);
			break;
		}
	}

	if (argc - optind != 1)
		print_usage(argv[0]);

	tty = argv[optind];

	if ((fd = open (tty, O_WRONLY | O_NOCTTY)) < 0) {
		perror(tty);
		exit(1);
	}

	if (waitkey || !detach) {

		if (speed) {
			sprintf(buf, "S%s\r", speed);
			write(fd, buf, strlen(buf));
		}

		if (btr) {
			sprintf(buf, "s%s\r", btr);
			write(fd, buf, strlen(buf));
		}

		if (send_open) {
			sprintf(buf, "O\r");
			write(fd, buf, strlen(buf));
		}

		if (ioctl (fd, TIOCSETD, &ldisc) < 0) {
			perror("ioctl");
			exit(1);
		}
	}

	if (waitkey) {
		printf("Press any key to detach %s ...\n", tty);
		getchar();
	}

	if (waitkey || detach) {
		ldisc = N_TTY;
		if (ioctl (fd, TIOCSETD, &ldisc) < 0) {
			perror("ioctl");
			exit(1);
		}

		if (send_close) {
			sprintf(buf, "C\r");
			write(fd, buf, strlen(buf));
		}
	}

	close(fd);

	return 0;
}
