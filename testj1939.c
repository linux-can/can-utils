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

#include <signal.h>
#include <time.h>
#include <inttypes.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <unistd.h>
#include <getopt.h>
#include <error.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/j1939.h>

static const char help_msg[] =
	"testj1939: demonstrate j1939 use\n"
	"Usage: testj1939 FROM TO\n"
	" FROM / TO	- or [IFACE][:[SA][,[PGN][,NAME]]]\n"
	"Options:\n"
	" -s[=LEN]	Initial send of LEN bytes dummy data\n"
	" -r		Receive (and print) data\n"
	" -e		Echo incoming packets back\n"
	"		This atually receives packets\n"
	" -c		Issue connect()\n"
	" -p=PRIO	Set priority to PRIO\n"
	" -n		Emit 64bit NAMEs in output\n"
	"\n"
	"Example:\n"
	"testj1939 can1 20\n"
	"\n"
	;

static const char optstring[] = "?vs::rep:cn";

static void parse_canaddr(char *spec, struct sockaddr_can *paddr)
{
	char *str;

	str = strsep(&spec, ":");
	if (strlen(str))
		paddr->can_ifindex = if_nametoindex(str);

	str = strsep(&spec, ",");
	if (str && strlen(str))
		paddr->can_addr.j1939.addr = strtoul(str, NULL, 0);

	str = strsep(&spec, ",");
	if (str && strlen(str))
		paddr->can_addr.j1939.pgn = strtoul(str, NULL, 0);

	str = strsep(&spec, ",");
	if (str && strlen(str))
		paddr->can_addr.j1939.name = strtoul(str, NULL, 0);
}

/* main */
int main(int argc, char *argv[])
{
	int ret, sock, opt, j;
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
	int todo_send = 0, todo_recv = 0, todo_echo = 0, todo_prio = -1;
	int todo_connect = 0, todo_names = 0;

	/* argument parsing */
	while ((opt = getopt(argc, argv, optstring)) != -1)
	switch (opt) {
	case 's':
		todo_send = strtoul(optarg ?: "8", NULL, 0);
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
	case 'c':
		todo_connect = 1;
		break;
	case 'n':
		todo_names = 1;
		break;
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}

	if (argv[optind]) {
		if (strcmp("-", argv[optind]))
			parse_canaddr(argv[optind], &sockname);
		++optind;
	}

	if (argv[optind]) {
		if (strcmp("-", argv[optind]))
			parse_canaddr(argv[optind], &peername);
		++optind;
	}

	/* open socket */
	sock = ret = socket(PF_CAN, SOCK_DGRAM, CAN_J1939);
	if (ret < 0)
		error(1, errno, "socket(j1939)");

	if (todo_prio >= 0) {
		ret = setsockopt(sock, SOL_CAN_J1939, SO_J1939_SEND_PRIO,
				&todo_prio, sizeof(todo_prio));
		if (ret < 0)
			error(1, errno, "set priority %i", todo_prio);
	}

	ret = bind(sock, (void *)&sockname, sizeof(sockname));
	if (ret < 0)
		error(1, errno, "bind()");

	if (todo_connect) {
		ret = connect(sock, (void *)&peername, sizeof(peername));
		if (ret < 0)
			error(1, errno, "connect()");
	}

	if (todo_send) {
		/* initialize test vector */
		for (j = 0; j < sizeof(dat); ++j)
			dat[j] = ((2*j) << 4) + ((2*j+1) & 0xf);

		/* send data */
		ret = sendto(sock, dat, todo_send, 0,
				(void *)&peername, sizeof(peername));
		if (ret < 0)
			error(1, errno, "sendto");
	}

	/* main loop */
	while (todo_echo || todo_recv) {
		/*
		 * re-use peername for storing the sender's peername of
		 * received packets
		 */
		peernamelen = sizeof(peername);
		ret = recvfrom(sock, dat, sizeof(dat), 0,
				(void *)&peername, &peernamelen);
		if (ret < 0) {
			if (EINTR == errno)
				continue;
			error(1, errno, "recvfrom()");
		}

		if (todo_echo) {
			ret = sendto(sock, dat, ret, 0,
					(void *)&peername, peernamelen);
			if (ret < 0)
				error(1, errno, "sendto");
		}
		if (todo_recv) {
			if (todo_names && peername.can_addr.j1939.name)
				printf("%016llx ", peername.can_addr.j1939.name);
			printf("%02x %05x:", peername.can_addr.j1939.addr,
					peername.can_addr.j1939.pgn);
			for (j = 0; j < ret; ++j)
				printf(" %02x", dat[j]);
			printf("\n");
		}
	}
	return 0;
}

