/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * isotptun.c - IP over CAN ISO-TP (ISO15765-2) tunnel / proof-of-concept
 *
 * This program creates a Linux tunnel netdevice 'ctunX' and transfers the
 * ethernet frames inside ISO15765-2 (unreliable) datagrams on CAN.
 *
 * Use e.g. "ifconfig ctun0 123.123.123.1 pointopoint 123.123.123.2 up"
 * to create a point-to-point IP connection on CAN.
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

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <syslog.h>

#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <linux/can.h>
#include <linux/can/isotp.h>
#include <linux/if_tun.h>

/* Change this to whatever your daemon is called */
#define DAEMON_NAME "isotptun"

#define NO_CAN_ID 0xFFFFFFFFU
#define DEFAULT_NAME "ctun%d"

/* stay on 4095 bytes for the max. PDU length which is still much more than the standard ethernet MTU */
#define MAX_PDU_LENGTH 4095
#define BUF_LEN (MAX_PDU_LENGTH + 1)

static volatile int running = 1;

static void fake_syslog(int priority, const char *format, ...)
{
	va_list ap;

	fprintf(stderr, "[%d] ", priority);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

typedef void (*syslog_t)(int priority, const char *format, ...);
static syslog_t syslogger = syslog;

void perror_syslog(const char *s)
{
	const char *colon = s ? ": " : "";
	syslogger(LOG_ERR, "%s%s%s", s, colon, strerror(errno));
}

void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s [options] <CAN interface>\n\n", prg);
	fprintf(stderr, "This program creates a Linux tunnel netdevice 'ctunX' and transfers the\n");
	fprintf(stderr, "ethernet frames inside ISO15765-2 (unreliable) datagrams on CAN.\n\n");
	fprintf(stderr, "Options: -s <can_id>  (source can_id. Use 8 digits for extended IDs)\n");
	fprintf(stderr, "         -d <can_id>  (destination can_id. Use 8 digits for extended IDs)\n");
	fprintf(stderr, "         -n <name>    (name of created IP netdevice. Default: '%s')\n", DEFAULT_NAME);
	fprintf(stderr, "         -x <addr>[:<rxaddr>] (extended addressing / opt. separate rxaddr)\n");
	fprintf(stderr, "         -L <mtu>:<tx_dl>:<tx_flags> (link layer options for CAN FD)\n");
	fprintf(stderr, "         -p [tx]:[rx] (set and enable tx/rx padding bytes)\n");
	fprintf(stderr, "         -P <mode>    (check rx padding for (l)ength (c)ontent (a)ll)\n");
	fprintf(stderr, "         -t <time ns> (transmit time in nanosecs)\n");
	fprintf(stderr, "         -b <bs>      (blocksize. 0 = off)\n");
	fprintf(stderr, "         -m <val>     (STmin in ms/ns. See spec.)\n");
	fprintf(stderr, "         -w <num>     (max. wait frame transmissions.)\n");
	fprintf(stderr, "         -D           (daemonize to background when tun device created)\n");
	fprintf(stderr, "         -h           (half duplex mode.)\n");
	fprintf(stderr, "         -v           (verbose mode. Print symbols for tunneled msgs.)\n");
	fprintf(stderr, "\nCAN IDs and addresses are given and expected in hexadecimal values.\n");
	fprintf(stderr, "Use e.g. 'ifconfig ctun0 123.123.123.1 pointopoint 123.123.123.2 up'\n");
	fprintf(stderr, "to create a point-to-point IP connection on CAN.\n");
	fprintf(stderr, "\n");
}

void sigterm(int signo)
{
	running = 0;
}

int main(int argc, char **argv)
{
	fd_set rdfs;
	int s, t;
	struct sockaddr_can addr;
	struct ifreq ifr;
	static struct can_isotp_options opts;
	static struct can_isotp_fc_options fcopts;
	static struct can_isotp_ll_options llopts;
	int opt, ret;
	extern int optind, opterr, optopt;
	static int verbose;
	unsigned char buffer[BUF_LEN];
	static char name[sizeof(ifr.ifr_name)] = DEFAULT_NAME;
	int nbytes;
	int run_as_daemon = 0;

	addr.can_addr.tp.tx_id = addr.can_addr.tp.rx_id = NO_CAN_ID;

	while ((opt = getopt(argc, argv, "s:d:n:x:p:P:t:b:m:whL:vD?")) != -1) {
		switch (opt) {
		case 's':
			addr.can_addr.tp.tx_id = strtoul(optarg, (char **)NULL, 16);
			if (strlen(optarg) > 7)
				addr.can_addr.tp.tx_id |= CAN_EFF_FLAG;
			break;

		case 'd':
			addr.can_addr.tp.rx_id = strtoul(optarg, (char **)NULL, 16);
			if (strlen(optarg) > 7)
				addr.can_addr.tp.rx_id |= CAN_EFF_FLAG;
			break;

		case 'n':
			if (strlen(optarg) > sizeof(name) - 1) {
				print_usage(basename(argv[0]));
				exit(EXIT_FAILURE);
			}
			/* ensure string termination */
			memset(name, 0, sizeof(name));
			strncpy(name, optarg, sizeof(name) - 1);
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
				fprintf(stderr, "incorrect extended addr values '%s'.\n", optarg);
				print_usage(basename(argv[0]));
				exit(EXIT_FAILURE);
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
				fprintf(stderr, "incorrect padding values '%s'.\n", optarg);
				print_usage(basename(argv[0]));
				exit(EXIT_FAILURE);
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
				fprintf(stderr, "unknown padding check option '%c'.\n", optarg[0]);
				print_usage(basename(argv[0]));
				exit(EXIT_FAILURE);
			}
			break;

		case 't':
			opts.frame_txtime = strtoul(optarg, (char **)NULL, 10);
			break;

		case 'b':
			fcopts.bs = strtoul(optarg, (char **)NULL, 16) & 0xFF;
			break;

		case 'm':
			fcopts.stmin = strtoul(optarg, (char **)NULL, 16) & 0xFF;
			break;

		case 'w':
			fcopts.wftmax = strtoul(optarg, (char **)NULL, 16) & 0xFF;
			break;

		case 'h':
			opts.flags |= CAN_ISOTP_HALF_DUPLEX;
			break;

		case 'L':
			if (sscanf(optarg, "%hhu:%hhu:%hhu",
				   &llopts.mtu,
				   &llopts.tx_dl,
				   &llopts.tx_flags) != 3) {
				fprintf(stderr, "unknown link layer options '%s'.\n", optarg);
				print_usage(basename(argv[0]));
				exit(EXIT_FAILURE);
			}
			break;

		case 'v':
			verbose = 1;
			break;

		case 'D':
			run_as_daemon = 1;
			break;

		case '?':
			print_usage(basename(argv[0]));
			exit(EXIT_SUCCESS);
			break;

		default:
			fprintf(stderr, "Unknown option %c\n", opt);
			print_usage(basename(argv[0]));
			exit(EXIT_FAILURE);
			break;
		}
	}

	if ((argc - optind != 1) ||
	    (addr.can_addr.tp.tx_id == NO_CAN_ID) ||
	    (addr.can_addr.tp.rx_id == NO_CAN_ID)) {
		print_usage(basename(argv[0]));
		exit(EXIT_FAILURE);
	}
  
	if (!run_as_daemon)
		syslogger = fake_syslog;

	/* Initialize the logging interface */
	openlog(DAEMON_NAME, LOG_PID, LOG_LOCAL5);

	if ((s = socket(PF_CAN, SOCK_DGRAM, CAN_ISOTP)) < 0) {
		perror_syslog("socket");
		exit(EXIT_FAILURE);
	}

	setsockopt(s, SOL_CAN_ISOTP, CAN_ISOTP_OPTS, &opts, sizeof(opts));
	setsockopt(s, SOL_CAN_ISOTP, CAN_ISOTP_RECV_FC, &fcopts, sizeof(fcopts));

	if (llopts.tx_dl) {
		if (setsockopt(s, SOL_CAN_ISOTP, CAN_ISOTP_LL_OPTS, &llopts, sizeof(llopts)) < 0) {
			perror_syslog("link layer sockopt");
			exit(EXIT_FAILURE);
		}
	}

	addr.can_family = AF_CAN;
	addr.can_ifindex = if_nametoindex(argv[optind]);
	if (!addr.can_ifindex) {
		perror_syslog("if_nametoindex");
		close(s);
		exit(EXIT_FAILURE);
	}

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror_syslog("bind");
		close(s);
		exit(EXIT_FAILURE);
	}

	if ((t = open("/dev/net/tun", O_RDWR)) < 0) {
		perror_syslog("open tunfd");
		close(s);
		close(t);
		exit(EXIT_FAILURE);
	}

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
	/* string termination is ensured at commandline option handling */
	strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	if (ioctl(t, TUNSETIFF, (void *) &ifr) < 0) {
		perror_syslog("ioctl tunfd");
		close(s);
		close(t);
		exit(EXIT_FAILURE);
	}

	/* Now the tun device exists. We can daemonize to let the
	 * parent continue and use the network interface. */
	if (run_as_daemon) {
		if (daemon(0, 0)) {
			syslogger(LOG_ERR, "failed to daemonize");
			exit(EXIT_FAILURE);
		}
	}

	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);
	signal(SIGINT, sigterm);

	while (running) {

		FD_ZERO(&rdfs);
		FD_SET(s, &rdfs);
		FD_SET(t, &rdfs);

		if ((ret = select(t+1, &rdfs, NULL, NULL, NULL)) < 0) {
			perror_syslog("select");
			continue;
		}

		if (FD_ISSET(s, &rdfs)) {
			nbytes = read(s, buffer, BUF_LEN);
			if (nbytes < 0) {
				perror_syslog("read isotp socket");
				return -1;
			}
			if (nbytes > MAX_PDU_LENGTH)
				return -1;
			ret = write(t, buffer, nbytes);
			if (verbose) {
				if (ret < 0 && errno == EAGAIN)
					printf(";");
				else
					printf(",");
				fflush(stdout);
			}
		}

		if (FD_ISSET(t, &rdfs)) {
			nbytes = read(t, buffer, BUF_LEN);
			if (nbytes < 0) {
				perror_syslog("read tunfd");
				return -1;
			}
			if (nbytes > MAX_PDU_LENGTH)
				return -1;
			ret = write(s, buffer, nbytes);
			if (verbose) {
				if (ret < 0 && errno == EAGAIN)
					printf(":");
				else
					printf(".");
				fflush(stdout);
			}
		}
	}

	close(s);
	close(t);
	return EXIT_SUCCESS;
}
