/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
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
 * Send feedback to <linux-can@vger.kernel.org>
 *
 */

#include <fcntl.h>
#include <getopt.h>
#include <linux/sockios.h>
#include <linux/tty.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

void print_usage(char *prg)
{
	fprintf(stderr, "%s - userspace tool for serial line CAN interface driver SLCAN.\n", prg);
	fprintf(stderr, "\nUsage: %s [options] tty\n\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -o          (send open command 'O\\r')\n");
	fprintf(stderr, "         -l          (send listen only command 'L\\r', overrides -o)\n");
	fprintf(stderr, "         -c          (send close command 'C\\r')\n");
	fprintf(stderr, "         -f          (read status flags with 'F\\r' to reset error states)\n");
	fprintf(stderr, "         -s <speed>  (set CAN speed 0..8)\n");
	fprintf(stderr, "         -b <btr>    (set bit time register value)\n");
	fprintf(stderr, "         -d          (only detach line discipline)\n");
	fprintf(stderr, "         -w          (attach - wait for keypress - detach)\n");
	fprintf(stderr, "         -n <name>   (assign created netdevice name)\n");
	fprintf(stderr, "\n"
			"    <speed>          Bitrate\n"
			"          0            10 Kbit/s\n"
			"          1            20 Kbit/s\n"
			"          2            50 Kbit/s\n"
			"          3           100 Kbit/s\n"
			"          4           125 Kbit/s\n"
			"          5           250 Kbit/s\n"
			"          6           500 Kbit/s\n"
			"          7           800 Kbit/s\n"
			"          8          1000 Kbit/s\n"
			"\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "slcan_attach -w -o -f -s6 -c /dev/ttyS1\n\n");
	fprintf(stderr, "slcan_attach /dev/ttyS1\n\n");
	fprintf(stderr, "slcan_attach -d /dev/ttyS1\n\n");
	fprintf(stderr, "slcan_attach -w -n can15 /dev/ttyS1\n\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int fd;
	int ldisc = N_SLCAN;
	int detach = 0;
	int waitkey = 0;
	int send_open = 0;
	int send_listen = 0;
	int send_close = 0;
	int send_read_status_flags = 0;
	char *speed = NULL;
	char *btr = NULL;
	char buf[20];
	static struct ifreq ifr;
	char *tty;
	char *name = NULL;
	int opt;

	while ((opt = getopt(argc, argv, "ldwocfs:b:n:?")) != -1) {
		switch (opt) {
		case 'd':
			detach = 1;
			break;

		case 'w':
			waitkey = 1;
			break;

		case 'o':
			send_open = 1;
			break;

		case 'l':
			send_listen = 1;
			break;

		case 'c':
			send_close = 1;
			break;

		case 'f':
			send_read_status_flags = 1;
			break;

		case 's':
			speed = optarg;
			if (strlen(speed) > 1)
				print_usage(argv[0]);
			break;

		case 'b':
			btr = optarg;
			if (strlen(btr) > 8)
				print_usage(argv[0]);
			break;

		case 'n':
			name = optarg;
			if (strlen(name) > sizeof(ifr.ifr_newname) - 1)
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
			sprintf(buf, "C\rS%s\r", speed);
			if (write(fd, buf, strlen(buf)) <= 0) {
				perror("write");
				exit(EXIT_FAILURE);
			}
		}

		if (btr) {
			sprintf(buf, "C\rs%s\r", btr);
			if (write(fd, buf, strlen(buf)) <= 0) {
				perror("write");
				exit(EXIT_FAILURE);
			}
		}

		if (send_read_status_flags) {
			sprintf(buf, "F\r");
			if (write(fd, buf, strlen(buf)) <= 0) {
				perror("write");
				exit(EXIT_FAILURE);
			}
		}

		if (send_listen) {
			sprintf(buf, "L\r");
			if (write(fd, buf, strlen(buf)) <= 0) {
				perror("write");
				exit(EXIT_FAILURE);
			}
		} else if (send_open) {
			sprintf(buf, "O\r");
			if (write(fd, buf, strlen(buf)) <= 0) {
				perror("write");
				exit(EXIT_FAILURE);
			}
		}

		/* set slcan line discipline on given tty */
		if (ioctl (fd, TIOCSETD, &ldisc) < 0) {
			perror("ioctl TIOCSETD");
			exit(1);
		}

		/* retrieve the name of the created CAN netdevice */
		if (ioctl (fd, SIOCGIFNAME, ifr.ifr_name) < 0) {
			perror("ioctl SIOCGIFNAME");
			exit(1);
		}

		printf("attached tty %s to netdevice %s\n", tty, ifr.ifr_name);

		/* try to rename the created device if requested */
		if (name) {
			int s = socket(PF_INET, SOCK_DGRAM, 0);

			printf("rename netdevice %s to %s ... ", buf, name);

			if (s < 0)
				perror("socket for interface rename");
			else {
				/* current slcan%d name is still in ifr.ifr_name */
				memset (ifr.ifr_newname, 0, sizeof(ifr.ifr_newname));
				strncpy (ifr.ifr_newname, name, sizeof(ifr.ifr_newname) - 1);

				if (ioctl(s, SIOCSIFNAME, &ifr) < 0)
					printf("failed!\n");
				else
					printf("ok.\n");

				close(s);
			}
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
			if (write(fd, buf, strlen(buf)) <= 0) {
				perror("write");
				exit(EXIT_FAILURE);
			}
		}
	}

	close(fd);

	return 0;
}
