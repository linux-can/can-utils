#include <can_config.h>

#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
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

extern int optind, opterr, optopt;

static int s = -1;
static int running = 1;

enum {
	VERSION_OPTION = CHAR_MAX + 1,
};

#define CAN_ID_DEFAULT	(2)

void print_usage(char *prg)
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
		" -q  --quit		quit if a wrong sequence is encountered\n"
		" -v, --verbose		be verbose (twice to be even more verbose\n"
		" -h  --help		this help\n"
		"     --version		print version information and exit\n",
		prg, CAN_ID_DEFAULT);
}

void sigterm(int signo)
{
	running = 0;
}

int main(int argc, char **argv)
{
	struct ifreq ifr;
	struct sockaddr_can addr;
	struct can_frame frame = {
		.can_dlc = 1,
	};
	struct can_filter filter[] = {
		{
			.can_id = CAN_ID_DEFAULT,
		},
	};
	char *interface = "can0";
	unsigned char sequence = 0;
	int seq_wrap = 0;
	int family = PF_CAN, type = SOCK_RAW, proto = CAN_RAW;
	int loopcount = 1, infinite = 1;
	int use_poll = 0;
	int extended = 0;
	int nbytes;
	int opt;
	int receive = 0;
	int sequence_init = 1;
	int verbose = 0, quit = 0;
	int exit_value = EXIT_SUCCESS;

	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);

	struct option long_options[] = {
		{ "extended",	no_argument,		0, 'e' },
		{ "help",	no_argument,		0, 'h' },
		{ "poll",	no_argument,		0, 'p' },
		{ "quit",	no_argument,		0, 'q' },
		{ "receive",	no_argument,		0, 'r' },
		{ "verbose",	no_argument,		0, 'v' },
		{ "version",	no_argument,		0, VERSION_OPTION},
		{ "identifier",	required_argument,	0, 'i' },
		{ "loop",	required_argument,	0, 'l' },
		{ 0,		0,			0, 0},
	};

	while ((opt = getopt_long(argc, argv, "ehpqrvi:l:", long_options, NULL)) != -1) {
		switch (opt) {
		case 'e':
			extended = 1;
			break;

		case 'h':
			print_usage(basename(argv[0]));
			exit(EXIT_SUCCESS);
			break;

		case 'p':
			use_poll = 1;
			break;

		case 'q':
			quit = 1;
			break;

		case 'r':
			receive = 1;
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
				infinite = 0;
			} else
				infinite = 1;
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
		perror("socket");
		return 1;
	}

	addr.can_family = family;
	strncpy(ifr.ifr_name, interface, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFINDEX, &ifr)) {
		perror("ioctl");
		return 1;
	}
	addr.can_ifindex = ifr.ifr_ifindex;

	/* first don't recv. any msgs */
	if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0)) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	if (receive) {
		/* enable recv. now */
		if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, filter, sizeof(filter))) {
			perror("setsockopt");
			exit(EXIT_FAILURE);
		}

		while ((infinite || loopcount--) && running) {
			nbytes = read(s, &frame, sizeof(struct can_frame));
			if (nbytes < 0) {
				perror("read");
				return 1;
			}

			if (sequence_init) {
				sequence_init = 0;
				sequence = frame.data[0];
			}

			if (verbose > 1)
				printf("received frame. sequence number: %d\n", frame.data[0]);

			if (frame.data[0] != sequence) {
				printf("received wrong sequence count. expected: %d, got: %d\n",
				       sequence, frame.data[0]);
				if (quit) {
					exit_value = EXIT_FAILURE;
					break;
				}
				sequence = frame.data[0];
			}

			sequence++;
			if (verbose && !sequence)
				printf("sequence wrap around (%d)\n", seq_wrap++);

		}
	} else {
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

	exit(exit_value);
}
