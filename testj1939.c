/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013 EIA Electronics
 *
 * Authors:
 * Kurt Van Dijck <kurt.van.dijck@eia.be>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 */

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <err.h>
#include <getopt.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "libj1939.h"

static const char help_msg[] =
	"testj1939: demonstrate j1939 use\n"
	"Usage: testj1939 [OPTIONS] FROM TO\n"
	" FROM / TO	- or [IFACE][:[SA][,[PGN][,NAME]]]\n"
	"Options:\n"
	" -v		Print relevant API calls\n"
	" -s[=LEN]	Initial send of LEN bytes dummy data\n"
	" -r		Receive (and print) data\n"
	" -e		Echo incoming packets back\n"
	"		This actually receives packets\n"
	" -c		Issue connect()\n"
	" -p=PRIO	Set priority to PRIO\n"
	" -P		Promiscuous mode. Allow to receive all packets\n"
	" -b		Do normal bind with SA+1 and rebind with actual SA\n"
	" -B		Allow to send and receive broadcast packets.\n"
	" -o		Omit bind\n"
	" -n		Emit 64bit NAMEs in output\n"
	" -w[TIME]	Return after TIME (default 1) seconds\n"
	"\n"
	"Examples:\n"
	"testj1939 can1 20\n"
	"\n"
	;

static const char optstring[] = "?vbBPos::rep:cnw::";

static void onsigalrm(int sig)
{
	err(0, "exit as requested");
	exit(0);
}

static void schedule_oneshot_itimer(double delay)
{
	struct itimerval it = { 0 };

	it.it_value.tv_sec = delay;
	it.it_value.tv_usec = (long)(delay * 1e6) % 1000000;
	if (setitimer(ITIMER_REAL, &it, NULL) < 0)
		err(1, "schedule itimer %.3lfs", delay);
}

/* main */
int main(int argc, char *argv[])
{
	int ret, sock, opt;
	unsigned int j;
	int verbose = 0;
	socklen_t peernamelen;
	struct sockaddr_can sockname = {
		.can_family = AF_CAN,
		.can_addr.j1939 = {
			.addr = J1939_NO_ADDR,
			.name = J1939_NO_NAME,
			.pgn = J1939_NO_PGN,
		},
	}, peername = {
		.can_family = AF_CAN,
		.can_addr.j1939 = {
			.addr = J1939_NO_ADDR,
			.name = J1939_NO_NAME,
			.pgn = J1939_NO_PGN,
		},
	};
	uint8_t dat[128];
	int valid_peername = 0;
	unsigned int todo_send = 0;
	int todo_recv = 0, todo_echo = 0, todo_prio = -1;
	int todo_connect = 0, todo_names = 0, todo_wait = 0, todo_rebind = 0;
	int todo_broadcast = 0, todo_promisc = 0;
	int no_bind = 0;

	/* argument parsing */
	while ((opt = getopt(argc, argv, optstring)) != -1)
		switch (opt) {
		case 'v':
			verbose = 1;
			break;
		case 's':
			todo_send = strtoul(optarg ?: "8", NULL, 0);
			if (todo_send > sizeof(dat))
				err(1, "Unsupported size. max: %zu",
				    sizeof(dat));
			break;
		case 'r':
			todo_recv = 1;
			break;
		case 'e':
			todo_echo = 1;
			break;
		case 'p':
			todo_prio = strtoul(optarg, NULL, 0);
			break;
		case 'P':
			todo_promisc = 1;
			break;
		case 'c':
			todo_connect = 1;
			break;
		case 'n':
			todo_names = 1;
			break;
		case 'b':
			todo_rebind = 1;
			break;
		case 'B':
			todo_broadcast = 1;
			break;
		case 'o':
			no_bind = 1;
			break;
		case 'w':
			schedule_oneshot_itimer(strtod(optarg ?: "1", NULL));
			signal(SIGALRM, onsigalrm);
			todo_wait = 1;
			break;
		default:
			fputs(help_msg, stderr);
			exit(1);
			break;
		}

	if (argv[optind]) {
		if (strcmp("-", argv[optind]) != 0)
			libj1939_parse_canaddr(argv[optind], &sockname);
		++optind;
	}

	if (todo_rebind)
		sockname.can_addr.j1939.addr++;

	if (argv[optind]) {
		if (strcmp("-", argv[optind]) != 0) {
			libj1939_parse_canaddr(argv[optind], &peername);
			valid_peername = 1;
		}
		++optind;
	}

	/* open socket */
	if (verbose)
		fprintf(stderr, "- socket(PF_CAN, SOCK_DGRAM, CAN_J1939);\n");
	sock = ret = socket(PF_CAN, SOCK_DGRAM, CAN_J1939);
	if (ret < 0)
		err(1, "socket(j1939)");

	if (todo_promisc) {
		if (verbose)
			fprintf(stderr, "- setsockopt(, SOL_SOCKET, SO_J1939_PROMISC, %d, %zd);\n",
				todo_promisc, sizeof(todo_promisc));
		ret = setsockopt(sock, SOL_CAN_J1939, SO_J1939_PROMISC,
				 &todo_promisc, sizeof(todo_promisc));
		if (ret < 0)
			err(1, "setsockopt: filed to set promiscuous mode");
	}

	if (todo_broadcast) {
		if (verbose)
			fprintf(stderr, "- setsockopt(, SOL_SOCKET, SO_BROADCAST, %d, %zd);\n",
				todo_broadcast, sizeof(todo_broadcast));
		ret = setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
				 &todo_broadcast, sizeof(todo_broadcast));
		if (ret < 0)
			err(1, "setsockopt: filed to set broadcast");
	}

	if (todo_prio >= 0) {
		if (verbose)
			fprintf(stderr, "- setsockopt(, SOL_CAN_J1939, SO_J1939_SEND_PRIO, &%i);\n", todo_prio);
		ret = setsockopt(sock, SOL_CAN_J1939, SO_J1939_SEND_PRIO,
				&todo_prio, sizeof(todo_prio));
		if (ret < 0)
			err(1, "set priority %i", todo_prio);
	}

	if (!no_bind) {

		if (verbose)
			fprintf(stderr, "- bind(, %s, %zi);\n", libj1939_addr2str(&sockname), sizeof(sockname));
		ret = bind(sock, (void *)&sockname, sizeof(sockname));
		if (ret < 0)
			err(1, "bind()");

		if (todo_rebind) {
			/* rebind with actual SA */
			sockname.can_addr.j1939.addr--;

			if (verbose)
				fprintf(stderr, "- bind(, %s, %zi);\n", libj1939_addr2str(&sockname), sizeof(sockname));
			ret = bind(sock, (void *)&sockname, sizeof(sockname));
			if (ret < 0)
				err(1, "re-bind()");
		}
	}

	if (todo_connect) {
		if (!valid_peername)
			err(1, "no peername supplied");
		if (verbose)
			fprintf(stderr, "- connect(, %s, %zi);\n", libj1939_addr2str(&peername), sizeof(peername));
		ret = connect(sock, (void *)&peername, sizeof(peername));
		if (ret < 0)
			err(1, "connect()");
	}

	if (todo_send) {
		/* initialize test vector */
		for (j = 0; j < sizeof(dat); ++j)
			dat[j] = ((2*j) << 4) + ((2*j+1) & 0xf);

		/* send data */
		/*
		 * when using connect, do not provide additional
		 * destination information and use send()
		 */
		if (valid_peername && !todo_connect) {
			if (verbose)
				fprintf(stderr, "- sendto(, <dat>, %i, 0, %s, %zi);\n", todo_send, libj1939_addr2str(&peername), sizeof(peername));
			ret = sendto(sock, dat, todo_send, 0,
					(void *)&peername, sizeof(peername));
		} else {
			/*
			 * we may do sendto(sock, dat, todo_send, 0, NULL, 0)
			 * as well, but using send() demonstrates the API better
			 */
			if (verbose)
				fprintf(stderr, "- send(, <dat>, %i, 0);\n", todo_send);
			ret = send(sock, dat, todo_send, 0);
		}

		if (ret < 0)
			err(1, "sendto");
	}

	/* main loop */
	if ((todo_echo || todo_recv) && verbose)
		fprintf(stderr, "- while (1)\n");
	while (todo_echo || todo_recv) {
		/*
		 * re-use peername for storing the sender's peername of
		 * received packets
		 */
		if (verbose)
			fprintf(stderr, "- recvfrom(, <dat>, %zi, 0, &<peername>, %zi);\n", sizeof(peername), sizeof(peername));
		peernamelen = sizeof(peername);
		ret = recvfrom(sock, dat, sizeof(dat), 0,
				(void *)&peername, &peernamelen);
		if (ret < 0) {
			if (EINTR == errno) {
				if (verbose)
					fprintf(stderr, "-\t<interrupted>\n");
				continue;
			}
			err(1, "recvfrom()");
		}

		if (todo_echo) {
			if (verbose)
				fprintf(stderr, "- sendto(, <dat>, %i, 0, %s, %i);\n", ret, libj1939_addr2str(&peername), peernamelen);
			ret = sendto(sock, dat, ret, 0,
					(void *)&peername, peernamelen);
			if (ret < 0)
				err(1, "sendto");
		}
		if (todo_recv) {
			int i;

			if (todo_names && peername.can_addr.j1939.name)
				printf("%016llx ", peername.can_addr.j1939.name);
			printf("%02x %05x:", peername.can_addr.j1939.addr,
					peername.can_addr.j1939.pgn);
			for (i = 0, j = 0; i < ret; ++i, j++) {
				if (j == 8) {
					printf("\n%05x    ", i);
					j = 0;
				}
				printf(" %02x", dat[i]);
			}
			printf("\n");
		}
	}
	if (todo_wait)
		for (;;)
			sleep(1);
	return 0;
}

