/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2011 EIA Electronics
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
#include <err.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/j1939.h>

#include "libj1939.h"

static const char help_msg[] =
	"jacd: An SAE J1939 address claiming daemon" "\n"
	"Usage: jacd [options] NAME [INTF]" "\n"
	"\n"
	"  -v, --verbose		Increase verbosity" "\n"
	"  -r, --range=RANGE	Ranges of source addresses" "\n"
	"			e.g. 80,50-100,200-210 (defaults to 0-253)" "\n"
	"  -c, --cache=FILE	Cache file to save/restore the source address" "\n"
	"  -a, --address=ADDRESS	Start with Source Address ADDRESS" "\n"
	"  -p, --prefix=STR	Prefix to use when logging" "\n"
	"\n"
	"NAME is the 64bit nodename" "\n"
	"\n"
	"Example:" "\n"
	"jacd -r 100,80-120 -c /tmp/1122334455667788.jacd 1122334455667788" "\n"
	;

#ifdef _GNU_SOURCE
static struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "verbose", no_argument, NULL, 'v', },
	{ "range", required_argument, NULL, 'r', },
	{ "cache", required_argument, NULL, 'c', },
	{ "address", required_argument, NULL, 'a', },
	{ "prefix", required_argument, NULL, 'p', },
	{ },
};
#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif
static const char optstring[] = "vr:c:a:p:?";

/* byte swap functions */
static inline int host_is_little_endian(void)
{
	static const uint16_t endian_test = 1;
	return *(const uint8_t *)&endian_test;
}

static __attribute__((unused)) void bswap(void *vptr, int size)
{
	uint8_t *p0, *pe;
	uint8_t tmp;

	p0 = vptr;
	pe = &p0[size-1];
	for (; p0 < pe; ++p0, --pe) {
		tmp = *p0;
		*p0 = *pe;
		*pe = tmp;
	}
}

/* rate-limiting for errors */
static inline int must_warn(int ret)
{
	if (ret >= 0)
		return 0;
	switch (errno) {
	case EINTR:
	case ENOBUFS:
		return 0;
	}
	return 1;
}

/* global variables */
static char default_range[] = "0x80-0xfd";
static const char default_intf[] = "can0";

static struct {
	int verbose;
	const char *cachefile;

	const char *intf;
	char *ranges;
	uint64_t name;
	uint8_t current_sa;
	uint8_t last_sa;
	int sig_term;
	int sig_alrm;
	int sig_usr1;
	int state;
		#define STATE_INITIAL 0
		#define STATE_REQ_SENT 1
		#define STATE_REQ_PENDING 2 /* wait 1250 msec for first claim */
		#define STATE_OPERATIONAL 3
} s = {
	.intf = default_intf,
	.ranges = default_range,
	.current_sa = J1939_IDLE_ADDR,
	.last_sa = J1939_NO_ADDR,
};

struct {
	uint64_t name;
	int flags;
		#define F_USE	0x01
		#define F_SEEN	0x02
} addr[J1939_IDLE_ADDR /* =254 */];

/* lookup by name */
static int lookup_name(uint64_t name)
{
	int j;

	for (j = 0; j < J1939_IDLE_ADDR; ++j) {
		if (addr[j].name == name)
			return j;
	}
	return J1939_IDLE_ADDR;

}

/* parse address range */
static int parse_range(char *str)
{
	char *tok, *endp;
	int a0, ae;
	int j, cnt;

	cnt = 0;
	for (tok = strtok(str, ",;"); tok; tok = strtok(NULL, ",;")) {
		a0 = ae = strtoul(tok, &endp, 0);
		if (endp <= tok)
			err(1, "parsing range '%s'", tok);
		if (*endp == '-') {
			tok = endp+1;
			ae = strtoul(tok, &endp, 0);
			if (endp <= tok)
				err(1, "parsing addr '%s'", tok);
			if (ae < a0)
				ae = a0;
		}
		for (j = a0; j <= ae; ++j, ++cnt) {
			if (j == J1939_IDLE_ADDR)
				break;
			addr[j].flags |= F_USE;
		}
	}
	return cnt;
}

/* j1939 socket */
static const struct j1939_filter filt[] = {
	{
		.pgn = J1939_PGN_ADDRESS_CLAIMED,
		.pgn_mask = J1939_PGN_PDU1_MAX,
	}, {
		.pgn = J1939_PGN_REQUEST,
		.pgn_mask = J1939_PGN_PDU1_MAX,
	}, {
		.pgn = 0x0fed8,
		.pgn_mask = J1939_PGN_MAX,
	},
};

static int open_socket(const char *device, uint64_t name)
{
	int ret, sock;
	int value;
	struct sockaddr_can saddr = {
		.can_family = AF_CAN,
		.can_addr.j1939 = {
			.name = name,
			.addr = J1939_IDLE_ADDR,
			.pgn = J1939_NO_PGN,
		},
		.can_ifindex = if_nametoindex(s.intf),
	};

	if (s.verbose)
		fprintf(stderr, "- socket(PF_CAN, SOCK_DGRAM, CAN_J1939);\n");
	sock = ret = socket(PF_CAN, SOCK_DGRAM, CAN_J1939);
	if (ret < 0)
		err(1, "socket(j1939)");

	if (s.verbose)
		fprintf(stderr, "- setsockopt(, SOL_CAN_J1939, SO_J1939_FILTER, <filter>, %zd);\n", sizeof(filt));
	ret = setsockopt(sock, SOL_CAN_J1939, SO_J1939_FILTER,
			&filt, sizeof(filt));
	if (ret < 0)
		err(1, "setsockopt filter");

	value = 1;
	if (s.verbose)
		fprintf(stderr, "- setsockopt(, SOL_SOCKET, SO_BROADCAST, %d, %zd);\n", value, sizeof(value));
	ret = setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
			&value, sizeof(value));
	if (ret < 0)
		err(1, "setsockopt set broadcast");

	if (s.verbose)
		fprintf(stderr, "- bind(, %s, %zi);\n", libj1939_addr2str(&saddr), sizeof(saddr));
	ret = bind(sock, (void *)&saddr, sizeof(saddr));
	if (ret < 0)
		err(1, "bind()");
	return sock;
}

/* real IO function */
static int repeat_address(int sock, uint64_t name)
{
	int ret;
	uint8_t dat[8];
	static const struct sockaddr_can saddr = {
		.can_family = AF_CAN,
		.can_addr.j1939 = {
			.pgn = J1939_PGN_ADDRESS_CLAIMED,
			.addr = J1939_NO_ADDR,
		},
	};

	memcpy(dat, &name, 8);
	if (!host_is_little_endian())
		bswap(dat, 8);
	if (s.verbose)
		fprintf(stderr, "- send(, %" PRId64 ", 8, 0);\n", name);
	ret = sendto(sock, dat, sizeof(dat), 0, (const struct sockaddr *)&saddr,
		     sizeof(saddr));
	if (must_warn(ret))
		err(1, "send address claim for 0x%02x", s.last_sa);
	return ret;
}
static int claim_address(int sock, uint64_t name, int sa)
{
	int ret;
	struct sockaddr_can saddr = {
		.can_family = AF_CAN,
		.can_addr.j1939 = {
			.name = name,
			.addr = sa,
			.pgn = J1939_NO_PGN,
		},
		.can_ifindex = if_nametoindex(s.intf),
	};

	if (s.verbose)
		fprintf(stderr, "- bind(, %s, %zi);\n", libj1939_addr2str(&saddr), sizeof(saddr));
	ret = bind(sock, (void *)&saddr, sizeof(saddr));
	if (ret < 0)
		err(1, "rebind with sa 0x%02x", sa);
	s.last_sa = sa;
	return repeat_address(sock, name);
}

static int request_addresses(int sock)
{
	static const uint8_t dat[3] = { 0, 0xee, 0, };
	int ret;
	static const struct sockaddr_can saddr = {
		.can_family = AF_CAN,
		.can_addr.j1939.pgn = J1939_PGN_REQUEST,
		.can_addr.j1939.addr = J1939_NO_ADDR,
	};

	if (s.verbose)
		fprintf(stderr, "- sendto(, { 0, 0xee, 0, }, %zi, 0, %s, %zi);\n", sizeof(dat), libj1939_addr2str(&saddr), sizeof(saddr));
	ret = sendto(sock, dat, sizeof(dat), 0, (void *)&saddr, sizeof(saddr));
	if (must_warn(ret))
		err(1, "send request for address claims");
	return ret;
}

/* real policy */
static int choose_new_sa(uint64_t name, int sa)
{
	int j, cnt;

	/* test current entry */
	if ((sa < J1939_IDLE_ADDR) && (addr[sa].flags & F_USE)) {
		j = sa;
		if (!addr[j].name || (addr[j].name == name) || (addr[j].name > name))
			return j;
	}
	/* take first empty spot */
	for (j = 0; j < J1939_IDLE_ADDR; ++j) {
		if (!(addr[j].flags & F_USE))
			continue;
		if (!addr[j].name || (addr[j].name == name))
			return j;
	}

	/*
	 * no empty spot found
	 * take next (relative to @sa) spot that we can
	 * successfully contest
	 */
	j = sa + 1;
	for (cnt = 0; cnt < J1939_IDLE_ADDR; ++j, ++cnt) {
		if (j >= J1939_IDLE_ADDR)
			j = 0;
		if (!(addr[j].flags & F_USE))
			continue;
		if (name < addr[j].name)
			return j;
	}
	return J1939_IDLE_ADDR;
}

/* signa handling */
static void sighandler(int sig, siginfo_t *info, void *vp)
{
	switch (sig) {
	case SIGINT:
	case SIGTERM:
		s.sig_term = 1;
		break;
	case SIGALRM:
		s.sig_alrm = 1;
		break;
	case SIGUSR1:
		s.sig_usr1 = 1;
		break;
	}
}

static void install_signal(int sig)
{
	int ret;
	struct sigaction sigact = {
		.sa_sigaction = sighandler,
		.sa_flags = SA_SIGINFO,
	};

	sigfillset(&sigact.sa_mask);
	ret = sigaction(sig, &sigact, NULL);
	if (ret < 0)
		err(1, "sigaction for signal %i", sig);
}

static void schedule_itimer(int msec)
{
	int ret;
	struct itimerval val = {};

	val.it_value.tv_sec = msec / 1000;
	val.it_value.tv_usec = (msec % 1000) * 1000;

	s.sig_alrm = 0;
	do {
		ret = setitimer(ITIMER_REAL, &val, NULL);
	} while ((ret < 0) && (errno == EINTR));
	if (ret < 0)
		err(1, "setitimer %i msec", msec);
}

/* dump status */
static inline int addr_status_mine(int sa)
{
	if (sa == s.current_sa)
		return '*';
	else if (addr[sa].flags & F_USE)
		return '+';
	else
		return '-';
}

static void dump_status(void)
{
	int j;

	for (j = 0; j < J1939_IDLE_ADDR; ++j) {
		if (!addr[j].flags && !addr[j].name)
			continue;
		fprintf(stdout, "%02x: %c", j, addr_status_mine(j));
		if (addr[j].name)
			fprintf(stdout, " %016llx", (long long)addr[j].name);
		else
			fprintf(stdout, " -");
		fprintf(stdout, "\n");
	}
	fflush(stdout);
}

/* cache file */
static void save_cache(void)
{
	FILE *fp;
	time_t t;

	if (!s.cachefile)
		return;
	fp = fopen(s.cachefile, "w");
	if (!fp)
		err(1, "fopen %s, w", s.cachefile);

	time(&t);
	fprintf(fp, "# saved on %s\n", ctime(&t));
	fprintf(fp, "\n");
	fprintf(fp, "0x%02x\n", s.current_sa);
	fclose(fp);
}

static void restore_cache(void)
{
	FILE *fp;
	int ret;
	char *endp;
	char *line = 0;
	size_t sz = 0;

	if (!s.cachefile)
		return;
	fp = fopen(s.cachefile, "r");
	if (!fp) {
		if (ENOENT == errno)
			return;
		err(1, "fopen %s, r", s.cachefile);
	}
	while (!feof(fp)) {
		ret = getline(&line, &sz, fp);
		if (ret <= 0)
			continue;
		if (line[0] == '#')
			continue;
		ret = strtoul(line, &endp, 0);
		if ((endp > line) && (ret >= 0) && (ret <= J1939_IDLE_ADDR)) {
			s.current_sa = ret;
			break;
		}
	}
	fclose(fp);
	if (line)
		free(line);
}

/* main */
int main(int argc, char *argv[])
{
	int ret, sock, sock_rx, pgn, sa, opt;
	socklen_t slen;
	uint8_t dat[9];
	struct sockaddr_can saddr;
	uint64_t cmd_name;

#ifdef _GNU_SOURCE
	program_invocation_name = program_invocation_short_name;
#endif
	/* argument parsing */
	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'v':
		++s.verbose;
		break;
	case 'c':
		s.cachefile = optarg;
		break;
	case 'r':
		s.ranges = optarg;
		break;
	case 'a':
		s.current_sa = strtoul(optarg, 0, 0);
		break;
	case 'p':
#ifdef _GNU_SOURCE
		if (asprintf(&program_invocation_name, "%s.%s", program_invocation_short_name, optarg) < 0)
			err(1, "asprintf(program invocation name)");
#else
		err(0, "compile with -D_GNU_SOURCE to use -p");
#endif
		break;
	default:
		fputs(help_msg, stderr);
		exit(1);
		break;
	}
	if (argv[optind])
		s.name = strtoull(argv[optind++], 0, 16);
	if (argv[optind])
		s.intf = argv[optind++];

	/* args done */

	restore_cache();

	ret = parse_range(s.ranges);
	if (!ret)
		err(1, "no addresses in range");

	if ((s.current_sa < J1939_IDLE_ADDR) && !(addr[s.current_sa].flags & F_USE)) {
		if (s.verbose)
			err(0, "forget saved address 0x%02x", s.current_sa);
		s.current_sa = J1939_IDLE_ADDR;
	}

	if (s.verbose)
		err(0, "ready for %s:%016llx", s.intf, (long long)s.name);
	if (!s.intf || !s.name)
		err(1, "bad arguments");
	ret = sock = open_socket(s.intf, s.name);
	sock_rx = open_socket(s.intf, s.name);

	install_signal(SIGTERM);
	install_signal(SIGINT);
	install_signal(SIGALRM);
	install_signal(SIGUSR1);
	install_signal(SIGUSR2);

	while (!s.sig_term) {
		if (s.sig_usr1) {
			s.sig_usr1 = 0;
			dump_status();
		}
		switch (s.state) {
		case STATE_INITIAL:
			ret = request_addresses(sock);
			if (ret < 0)
				err(1, "could not sent initial request");
			s.state = STATE_REQ_SENT;
			break;
		case STATE_REQ_PENDING:
			if (!s.sig_alrm)
				break;
			s.sig_alrm = 0;
			/* claim addr */
			sa = choose_new_sa(s.name, s.current_sa);
			if (sa == J1939_IDLE_ADDR)
				err(1, "no free address to use");
			ret = claim_address(sock, s.name, sa);
			if (ret < 0)
				schedule_itimer(50);
			s.state = STATE_OPERATIONAL;
			break;
		case STATE_OPERATIONAL:
			if (s.sig_alrm) {
				s.sig_alrm = 0;
				ret = repeat_address(sock, s.name);
				if (ret < 0)
					schedule_itimer(50);
			}
			break;
		}

		slen = sizeof(saddr);
		ret = recvfrom(sock_rx, dat, sizeof(dat), 0, (void *)&saddr, &slen);
		if (ret < 0) {
			if (EINTR == errno)
				continue;
			err(1, "recvfrom()");
		}
		switch (saddr.can_addr.j1939.pgn) {
		case J1939_PGN_REQUEST:
			if (ret < 3)
				break;
			pgn = dat[0] + (dat[1] << 8) + ((dat[2] & 0x03) << 16);
			if (pgn != J1939_PGN_ADDRESS_CLAIMED)
				/* not interested */
				break;
			if (s.state == STATE_REQ_SENT) {
				if (s.verbose)
					err(0, "request sent, pending for 1250 ms");
				schedule_itimer(1250);
				s.state = STATE_REQ_PENDING;
			} else if (s.state == STATE_OPERATIONAL) {
				ret = claim_address(sock, s.name, s.current_sa);
				if (ret < 0)
					schedule_itimer(50);
			}
			break;
		case J1939_PGN_ADDRESS_CLAIMED:
			if (saddr.can_addr.j1939.addr >= J1939_IDLE_ADDR) {
				sa = lookup_name(saddr.can_addr.j1939.name);
				if (sa < J1939_IDLE_ADDR)
					addr[sa].name = 0;
				break;
			}
			sa = lookup_name(saddr.can_addr.j1939.name);
			if ((sa != saddr.can_addr.j1939.addr) && (sa < J1939_IDLE_ADDR))
				/* update cache */
				addr[sa].name = 0;

			/* shortcut */
			sa = saddr.can_addr.j1939.addr;
			addr[sa].name = saddr.can_addr.j1939.name;
			addr[sa].flags |= F_SEEN;

			if (s.name == saddr.can_addr.j1939.name) {
				/* ourselves, disable itimer */
				s.current_sa = sa;
				if (s.verbose)
					err(0, "claimed 0x%02x", sa);
			} else if (sa == s.current_sa) {
				if (s.verbose)
					err(0, "address collision for 0x%02x", sa);
				if (s.name > saddr.can_addr.j1939.name) {
					sa = choose_new_sa(s.name, sa);
					if (sa == J1939_IDLE_ADDR) {
						err(0, "no address left");
						/* put J1939_IDLE_ADDR in cache file */
						s.current_sa = sa;
						goto done;
					}
				}
				ret = claim_address(sock, s.name, sa);
				if (ret < 0)
					schedule_itimer(50);
			}
			break;
		case 0x0fed8:
			if (!host_is_little_endian())
				bswap(dat, 8);
			memcpy(&cmd_name, dat, 8);
			if (cmd_name == s.name) {
				ret = claim_address(sock, s.name, dat[8]);
				if (ret < 0)
					schedule_itimer(50);
			}
			break;
		}
	}
done:
	if (s.verbose)
		err(0, "shutdown");
	claim_address(sock, s.name, J1939_IDLE_ADDR);
	save_cache();
	return 0;
}

