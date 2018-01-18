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
	" -v		Print relevant API calls\n"
	" -s[=LEN]	Initial send of LEN bytes dummy data\n"
	" -r		Receive (and print) data\n"
	" -e		Echo incoming packets back\n"
	"		This atually receives packets\n"
	" -c		Issue connect()\n"
	" -p=PRIO	Set priority to PRIO\n"
	" -n		Emit 64bit NAMEs in output\n"
	" -w[TIME]	Return after TIME (default 1) seconds\n"
	"\n"
	"Example:\n"
	"testj1939 can1 20\n"
	"\n"
	;

static const char optstring[] = "?vs::rep:cnw::";

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

static const char *canaddr2str(const struct sockaddr_can *paddr)
{
	static char buf[128];
	char *str = buf;
	char ifname[IF_NAMESIZE];

	if (paddr->can_ifindex)
		str += sprintf(str, "%s", if_indextoname(paddr->can_ifindex, ifname));
	*str++ = ':';

	if (paddr->can_addr.j1939.addr != J1939_NO_ADDR)
		str += sprintf(str, "%02x", paddr->can_addr.j1939.addr);
	*str++ = ',';
	if (paddr->can_addr.j1939.pgn != J1939_NO_PGN)
		str += sprintf(str, "%05x", paddr->can_addr.j1939.pgn);
	*str++ = ',';
	if (paddr->can_addr.j1939.name != J1939_NO_NAME)
		str += sprintf(str, "%016llx", paddr->can_addr.j1939.name);
	*str++ = 0;
	return buf;
}

static void onsigalrm(int sig)
{
	error(0, 0, "exit as requested");
	exit(0);
}

static void schedule_oneshot_itimer(double delay)
{
	struct itimerval it = {};

	it.it_value.tv_sec = delay;
	it.it_value.tv_usec = (long)(delay * 1e6) % 1000000;
	if (setitimer(ITIMER_REAL, &it, NULL) < 0)
		error(1, errno, "schedule itimer %.3lfs", delay);
}

/* main */
int main(int argc, char *argv[])
{
	int ret, sock, opt, j, verbose;
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
	int todo_send = 0, todo_recv = 0, todo_echo = 0, todo_prio = -1;
	int todo_connect = 0, todo_names = 0, todo_wait = 0;

	/* argument parsing */
	while ((opt = getopt(argc, argv, optstring)) != -1)
	switch (opt) {
	case 'v':
		verbose = 1;
		break;
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
		if (strcmp("-", argv[optind]))
			parse_canaddr(argv[optind], &sockname);
		++optind;
	}

	if (argv[optind]) {
		if (strcmp("-", argv[optind])) {
			parse_canaddr(argv[optind], &peername);
			valid_peername = 1;
		}
		++optind;
	}

	/* open socket */
	if (verbose)
		fprintf(stderr, "- socket(PF_CAN, SOCK_DGRAM, CAN_J1939);\n");
	sock = ret = socket(PF_CAN, SOCK_DGRAM, CAN_J1939);
	if (ret < 0)
		error(1, errno, "socket(j1939)");

	if (todo_prio >= 0) {
		if (verbose)
			fprintf(stderr, "- setsockopt(, SOL_CAN_J1939, SO_J1939_SEND_PRIO, &%i);\n", todo_prio);
		ret = setsockopt(sock, SOL_CAN_J1939, SO_J1939_SEND_PRIO,
				&todo_prio, sizeof(todo_prio));
		if (ret < 0)
			error(1, errno, "set priority %i", todo_prio);
	}

	if (verbose)
		fprintf(stderr, "- bind(, %s, %zi);\n", canaddr2str(&sockname), sizeof(sockname));
	ret = bind(sock, (void *)&sockname, sizeof(sockname));
	if (ret < 0)
		error(1, errno, "bind()");

	if (todo_connect) {
		if (!valid_peername)
			error(1, 0, "no peername supplied");
		if (verbose)
			fprintf(stderr, "- connect(, %s, %zi);\n", canaddr2str(&peername), sizeof(peername));
		ret = connect(sock, (void *)&peername, sizeof(peername));
		if (ret < 0)
			error(1, errno, "connect()");
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
				fprintf(stderr, "- sendto(, <dat>, %i, 0, %s, %zi);\n", todo_send, canaddr2str(&peername), sizeof(peername));
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
			error(1, errno, "sendto");
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
			error(1, errno, "recvfrom()");
		}

		if (todo_echo) {
			if (verbose)
				fprintf(stderr, "- sendto(, <dat>, %i, 0, %s, %i);\n", ret, canaddr2str(&peername), peernamelen);
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
	if (todo_wait)
		for (;;)
			sleep(1);
	return 0;
}

