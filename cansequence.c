#include <can_config.h>

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
enum {
	VERSION_OPTION = CHAR_MAX + 1,
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
		" -e  --extended		send extended frame\n"
		" -i, --identifier=ID	CAN Identifier (default = %u)\n"
		" -r, --receive		work as receiver\n"
		"     --loop=COUNT	send message COUNT times\n"
		" -p  --poll		use poll(2) to wait for buffer space while sending\n"
		" -q  --quit <num>	quit if <num> wrong sequences are encountered\n"
		" -v, --verbose		be verbose (twice to be even more verbose\n"
		" -h  --help		this help\n"
		"     --version		print version information and exit\n",
		prg, CAN_ID_DEFAULT);
}

static void sigterm(int signo)
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
	struct cmsghdr *cmsg;
	const int dropmonitor_on = 1;
	bool sequence_init = true;
	unsigned int seq_wrap = 0;
	uint8_t sequence = 0;
	ssize_t nbytes;

	if (setsockopt(s, SOL_SOCKET, SO_RXQ_OVFL,
		       &dropmonitor_on, sizeof(dropmonitor_on)) < 0) {
		perror("setsockopt() SO_RXQ_OVFL not supported by your Linux Kernel");
	}

	/* enable recv. now */
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, filter, sizeof(filter))) {
		perror("setsockopt()");
		exit(EXIT_FAILURE);
	}

	while ((infinite || loopcount--) && running) {
		msg.msg_iov[0].iov_len = sizeof(frame);
		msg.msg_controllen = sizeof(ctrlmsg);
		msg.msg_flags = 0;
		nbytes = recvmsg(s, &msg, 0);

		if (nbytes < 0) {
			perror("read()");
			exit(EXIT_FAILURE);
		}

		if (sequence_init) {
			sequence_init = false;
			sequence = frame.data[0];
		}

		if (verbose > 1)
			printf("received frame. sequence number: %d\n", frame.data[0]);

		if (frame.data[0] != sequence) {
			uint32_t overflows = 0;

			drop_count++;

			for (cmsg = CMSG_FIRSTHDR(&msg);
			     cmsg && (cmsg->cmsg_level == SOL_SOCKET);
			     cmsg = CMSG_NXTHDR(&msg,cmsg)) {
				if (cmsg->cmsg_type == SO_RXQ_OVFL) {
					memcpy(&overflows, CMSG_DATA(cmsg), sizeof(overflows));
					break;
				}
			}

			fprintf(stderr, "[%d] received wrong sequence count. expected: %d, got: %d, socket overflows: %u\n",
				drop_count, sequence, frame.data[0], overflows);

			if (drop_count == drop_until_quit)
				exit(EXIT_FAILURE);

			sequence = frame.data[0];
		}

		sequence++;
		if (verbose && !sequence)
			printf("sequence wrap around (%d)\n", seq_wrap++);

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
				if (err == -1 && errno != -EINTR) {
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

		(unsigned char)frame.data[0]++;
		sequence++;

		if (verbose && !sequence)
			printf("sequence wrap around (%d)\n", seq_wrap++);
	}
}

int main(int argc, char **argv)
{
	struct ifreq ifr;
	struct sockaddr_can addr;
	char *interface = "can0";
	int family = PF_CAN, type = SOCK_RAW, proto = CAN_RAW;
	int extended = 0;
	int receive = 0;
	int opt;

	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);

	struct option long_options[] = {
		{ "extended",	no_argument,		0, 'e' },
		{ "help",	no_argument,		0, 'h' },
		{ "poll",	no_argument,		0, 'p' },
		{ "quit",	optional_argument,	0, 'q' },
		{ "receive",	no_argument,		0, 'r' },
		{ "verbose",	no_argument,		0, 'v' },
		{ "version",	no_argument,		0, VERSION_OPTION},
		{ "identifier",	required_argument,	0, 'i' },
		{ "loop",	required_argument,	0, 'l' },
		{ 0,		0,			0, 0},
	};

	while ((opt = getopt_long(argc, argv, "ehpq:rvi:l:", long_options, NULL)) != -1) {
		switch (opt) {
		case 'e':
			extended = true;
			break;

		case 'h':
			print_usage(basename(argv[0]));
			exit(EXIT_SUCCESS);
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

		case 'r':
			receive = true;
			break;

		case 'v':
			verbose++;
			break;

		case VERSION_OPTION:
			printf("cansequence %s\n", VERSION);
			exit(EXIT_SUCCESS);
			break;

		case 'l':
			if (optarg) {
				loopcount = strtoul(optarg, NULL, 0);
				infinite = false;
			} else {
				infinite = true;
			}
			break;

		case 'i':
			filter->can_id = strtoul(optarg, NULL, 0);
			break;

		default:
			fprintf(stderr, "Unknown option %c\n", opt);
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
