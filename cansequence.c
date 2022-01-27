/* SPDX-License-Identifier: GPL-2.0-only */
// Copyright (c) 2007, 2008, 2009, 2010, 2014, 2015, 2019 Pengutronix,
//		 Marc Kleine-Budde <kernel@pengutronix.de>
// Copyright (c) 2005 Pengutronix,
//		 Sascha Hauer <kernel@pengutronix.de>

#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/if.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#define CAN_ID_DEFAULT	(2)

extern int optind, opterr, optopt;

static int s = -1;
static bool running = true;
static bool infinite = true;
static unsigned int drop_until_quit;
static unsigned int drop_count;
static bool use_poll = false;

static unsigned int loopcount = 1;
static int verbose;

static struct can_frame frame = {
	.can_dlc = 1,
};
static struct can_filter filter[] = {
	{ .can_id = CAN_ID_DEFAULT, },
};

static void print_usage(char *prg)
{
	fprintf(stderr, "Usage: %s [<can-interface>] [Options]\n"
		"\n"
		"cansequence sends CAN messages with a rising sequence number as payload.\n"
		"When the -r option is given, cansequence expects to receive these messages\n"
		"and prints an error message if a wrong sequence number is encountered.\n"
		"The main purpose of this program is to test the reliability of CAN links.\n"
		"\n"
		"Options:\n"
		" -e, --extended		send extended frame\n"
		" -i, --identifier=ID	CAN Identifier (default = %u)\n"
		"     --loop=COUNT	send message COUNT times\n"
		" -p, --poll		use poll(2) to wait for buffer space while sending\n"
		" -q, --quit <num>	quit if <num> wrong sequences are encountered\n"
		" -r, --receive		work as receiver\n"
		" -v, --verbose		be verbose (twice to be even more verbose\n"
		" -h, --help		this help\n"
		"     --version		print version information and exit\n",
		prg, CAN_ID_DEFAULT);
}

static void sig_handler(int signo)
{
	running = false;
}


static void do_receive()
{
	uint8_t ctrlmsg[CMSG_SPACE(sizeof(struct timeval)) + CMSG_SPACE(sizeof(__u32))];
	struct iovec iov = {
		.iov_base = &frame,
	};
	struct msghdr msg = {
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = &ctrlmsg,
	};
	const int dropmonitor_on = 1;
	bool sequence_init = true;
	unsigned int sequence_wrap = 0;
	uint32_t sequence_mask = 0xff;
	uint32_t sequence_rx = 0;
	uint32_t sequence_delta = 0;
	uint32_t sequence = 0;
	unsigned int overflow_old = 0;
	can_err_mask_t err_mask = CAN_ERR_MASK;

	if (setsockopt(s, SOL_SOCKET, SO_RXQ_OVFL,
		       &dropmonitor_on, sizeof(dropmonitor_on)) < 0) {
		perror("setsockopt() SO_RXQ_OVFL not supported by your Linux Kernel");
	}

	/* enable recv. of error messages */
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof(err_mask))) {
		perror("setsockopt()");
		exit(EXIT_FAILURE);
	}

	/* enable recv. now */
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, filter, sizeof(filter))) {
		perror("setsockopt()");
		exit(EXIT_FAILURE);
	}

	while ((infinite || loopcount--) && running) {
		ssize_t nbytes;

		msg.msg_iov[0].iov_len = sizeof(frame);
		msg.msg_controllen = sizeof(ctrlmsg);
		msg.msg_flags = 0;

		nbytes = recvmsg(s, &msg, 0);
		if (nbytes < 0) {
			perror("read()");
			exit(EXIT_FAILURE);
		}

		if (frame.can_id & CAN_ERR_FLAG) {
			fprintf(stderr,
				"sequence CNT: %6u, ERRORFRAME %7x   %02x %02x %02x %02x %02x %02x %02x %02x\n",
				sequence, frame.can_id,
				frame.data[0], frame.data[1], frame.data[2], frame.data[3],
				frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
			continue;
		}

		sequence_rx = frame.data[0];

		if (sequence_init) {
			sequence_init = false;
			sequence = sequence_rx;
		}

		sequence_delta = (sequence_rx - sequence) & sequence_mask;
		if (sequence_delta) {
			struct cmsghdr *cmsg;
			uint32_t overflow = 0;
			uint32_t overflow_delta;

			drop_count++;

			for (cmsg = CMSG_FIRSTHDR(&msg);
			     cmsg && (cmsg->cmsg_level == SOL_SOCKET);
			     cmsg = CMSG_NXTHDR(&msg,cmsg)) {
				if (cmsg->cmsg_type == SO_RXQ_OVFL) {
					memcpy(&overflow, CMSG_DATA(cmsg), sizeof(overflow));
					break;
				}
			}

			overflow_delta = overflow - overflow_old;

			fprintf(stderr,
				"sequence CNT: %6u, RX: %6u    expected: %3u    missing: %4u    skt overfl d: %4u a: %4u    delta: %3u    incident: %u\n",
				sequence, sequence_rx,
				sequence & sequence_mask, sequence_delta,
				overflow_delta, overflow,
				sequence_delta - overflow_delta,
				drop_count);

			if (drop_count == drop_until_quit)
				exit(EXIT_FAILURE);

			sequence = sequence_rx;
			overflow_old = overflow;
		} else 	if (verbose > 1) {
			printf("sequence CNT: %6u, RX: %6u\n", sequence, sequence_rx);
		}

		sequence++;
		if (verbose && !(sequence & sequence_mask))
			printf("sequence wrap around (%d)\n", sequence_wrap++);

	}
}

static void do_send()
{
	unsigned int seq_wrap = 0;
	uint8_t sequence = 0;

	while ((infinite || loopcount--) && running) {
		ssize_t len;

		if (verbose > 1)
			printf("sending frame. sequence number: %d\n", sequence);

	again:
		len = write(s, &frame, sizeof(frame));
		if (len == -1) {
			switch (errno) {
			case ENOBUFS: {
				int err;
				struct pollfd fds[] = {
					{
						.fd	= s,
						.events	= POLLOUT,
					},
				};

				if (!use_poll) {
					perror("write");
					exit(EXIT_FAILURE);
				}

				err = poll(fds, 1, 1000);
				if (err == 0 || (err == -1 && errno != -EINTR)) {
					perror("poll()");
					exit(EXIT_FAILURE);
				}
			}
			case EINTR:	/* fallthrough */
				goto again;
			default:
				perror("write");
				exit(EXIT_FAILURE);
			}
		}

		frame.data[0]++;
		sequence++;

		if (verbose && !sequence)
			printf("sequence wrap around (%d)\n", seq_wrap++);
	}
}

int main(int argc, char **argv)
{
	struct sigaction act = {
		.sa_handler = sig_handler,
	};
	struct ifreq ifr;
	struct sockaddr_can addr;
	char *interface = "can0";
	int family = PF_CAN, type = SOCK_RAW, proto = CAN_RAW;
	int extended = 0;
	int receive = 0;
	int opt;

	sigaction(SIGINT, &act, NULL);
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGHUP, &act, NULL);

	struct option long_options[] = {
		{ "extended",	no_argument,		0, 'e' },
		{ "identifier",	required_argument,	0, 'i' },
		{ "loop",	required_argument,	0, 'l' },
		{ "poll",	no_argument,		0, 'p' },
		{ "quit",	optional_argument,	0, 'q' },
		{ "receive",	no_argument,		0, 'r' },
		{ "verbose",	no_argument,		0, 'v' },
		{ "help",	no_argument,		0, 'h' },
		{ 0,		0,			0, 0},
	};

	while ((opt = getopt_long(argc, argv, "ei:pq::rvh", long_options, NULL)) != -1) {
		switch (opt) {
		case 'e':
			extended = true;
			break;

		case 'i':
			filter->can_id = strtoul(optarg, NULL, 0);
			break;

		case 'r':
			receive = true;
			break;

		case 'l':
			if (optarg) {
				loopcount = strtoul(optarg, NULL, 0);
				infinite = false;
			} else {
				infinite = true;
			}
			break;

		case 'p':
			use_poll = true;
			break;

		case 'q':
			if (optarg)
				drop_until_quit = strtoul(optarg, NULL, 0);
			else
				drop_until_quit = 1;
			break;

		case 'v':
			verbose++;
			break;

		case 'h':
			print_usage(basename(argv[0]));
			exit(EXIT_SUCCESS);
			break;

		default:
			print_usage(basename(argv[0]));
			exit(EXIT_FAILURE);
			break;
		}
	}

	if (argv[optind] != NULL)
		interface = argv[optind];

	if (extended) {
		filter->can_mask = CAN_EFF_MASK;
		filter->can_id  &= CAN_EFF_MASK;
		filter->can_id  |= CAN_EFF_FLAG;
	} else {
		filter->can_mask = CAN_SFF_MASK;
		filter->can_id  &= CAN_SFF_MASK;
	}
	frame.can_id = filter->can_id;
	filter->can_mask |= CAN_EFF_FLAG;

	printf("interface = %s, family = %d, type = %d, proto = %d\n",
	       interface, family, type, proto);

	s = socket(family, type, proto);
	if (s < 0) {
		perror("socket()");
		exit(EXIT_FAILURE);
	}

	addr.can_family = family;
	strncpy(ifr.ifr_name, interface, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFINDEX, &ifr)) {
		perror("ioctl()");
		exit(EXIT_FAILURE);
	}
	addr.can_ifindex = ifr.ifr_ifindex;

	/* first don't recv. any msgs */
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0)) {
		perror("setsockopt()");
		exit(EXIT_FAILURE);
	}

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind()");
		exit(EXIT_FAILURE);
	}

	if (receive)
		do_receive();
	else
		do_send();

	exit(EXIT_SUCCESS);
}
