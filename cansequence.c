#include <can_config.h>

#include <getopt.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

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
		" -f, --family=FAMILY	Protocol family (default PF_CAN = %d)\n"
		" -t, --type=TYPE	Socket type, see man 2 socket (default SOCK_RAW = %d)\n"
		" -p, --protocol=PROTO	CAN protocol (default CAN_RAW = %d)\n"
		" -r, --receive		work as receiver\n"
		"     --loop=COUNT	send message COUNT times\n"
		" -q  --quit		quit if a wrong sequence is encountered\n"
		" -v, --verbose		be verbose (twice to be even more verbose\n"
		" -h  --help		this help\n"
		"     --version		print version information and exit\n",
		prg, PF_CAN, SOCK_RAW, CAN_RAW);
}

void sigterm(int signo)
{
	running = 0;
}

int main(int argc, char **argv)
{
	struct can_frame frame;
	struct ifreq ifr;
	struct sockaddr_can addr;
	char *interface = "can0";
	unsigned char sequence = 0;
	int family = PF_CAN, type = SOCK_RAW, proto = CAN_RAW;
	int loopcount = 1, infinite = 1;
	int nbytes;
	int opt;
	int receive = 0;
	int sequence_init = 1;
	int verbose = 0, quit = 0;

	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);

	struct option long_options[] = {
		{ "help",	no_argument,		0, 'h' },
		{ "family",	required_argument,	0, 'f' },
		{ "protocol",	required_argument,	0, 'p' },
		{ "type",	required_argument,	0, 't' },
		{ "version",	no_argument,		0, VERSION_OPTION},
		{ "receive",	no_argument,		0, 'r'},
		{ "quit",	no_argument,		0, 'q'},
		{ "loop",	required_argument,	0, 'l'},
		{ "verbose",	no_argument,		0, 'v'},
		{ 0,		0,			0, 0},
	};

	while ((opt = getopt_long(argc, argv, "f:t:p:vrlhq", long_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			print_usage(basename(argv[0]));
			exit(0);

		case 'f':
			family = strtoul(optarg, NULL, 0);
			break;

		case 't':
			type = strtoul(optarg, NULL, 0);
			break;

		case 'p':
			proto = strtoul(optarg, NULL, 0);
			break;

		case 'l':
			if (optarg) {
				loopcount = strtoul(optarg, NULL, 0);
				infinite = 0;
			} else
				infinite = 1;
			break;

		case 'r':
			receive = 1;
			break;

		case 'q':
			quit = 1;
			break;

		case 'v':
			verbose++;
			break;

		case VERSION_OPTION:
			printf("cansequence %s\n", VERSION);
			exit(0);

		default:
			fprintf(stderr, "Unknown option %c\n", opt);
			break;
		}
	}

	if (argv[optind] != NULL)
		interface = argv[optind];

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

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	if (receive) {
		while ((infinite || loopcount--) && running) {
			nbytes = read(s, &frame, sizeof(struct can_frame));
			if (nbytes < 0) {
				perror("read");
				return 1;
			} else {
				if (sequence_init) {
					sequence_init = 0;
					sequence = frame.data[0];
				}
				if (verbose > 1)
					printf("received frame. sequence number: %d\n", frame.data[0]);
				if (frame.data[0] != sequence) {
					printf("received wrong sequence count. expected: %d, got: %d\n",
					       sequence, frame.data[0]);
					if (quit)
						exit(1);
					sequence = frame.data[0];
				}
				if (verbose && !sequence)
					printf("sequence wrap around\n");
				sequence++;
			}
		}
	} else {
		frame.can_dlc = 1;
		frame.can_id = 2;
		frame.data[0] = 0;
		while ((infinite || loopcount--) && running) {
			if (verbose > 1)
				printf("sending frame. sequence number: %d\n", sequence);
			if (verbose && !sequence)
				printf("sequence wrap around\n");
			if (write(s, &frame, sizeof(frame)) < 0) {
				perror("write");
				break;
			}
			(unsigned char)frame.data[0]++;
			sequence++;
		}
	}
	return 0;
}
