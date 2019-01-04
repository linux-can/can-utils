// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018 Pengutronix, Oleksij Rempel <o.rempel@pengutronix.de>
 */

#include <err.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <inttypes.h>
#include <net/if.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libj1939.h"
#define J1939_MAX_ETP_PACKET_SIZE (7 * 0x00ffffff)
#define JCAT_BUF_SIZE (1000 * 1024)

/*
 * min()/max()/clamp() macros that also do
 * strict type-checking.. See the
 * "unnecessary" pointer comparison.
 */
#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })

struct jcat_priv {
	int sock;
	int infile;
	int outfile;
	unsigned int todo_send;
	int todo_prio;

	bool valid_peername;
	bool todo_recv;
	bool todo_filesize;
	bool todo_connect;

	struct sockaddr_can sockname;
	struct sockaddr_can peername;
};

static const char help_msg[] =
	"jcat: netcat tool for j1939\n"
	"Usage: jcat FROM TO\n"
	" FROM / TO	- or [IFACE][:[SA][,[PGN][,NAME]]]\n"
	"Options:\n"
	" -i <infile>	(default stdin)\n"
	" -s[=LEN]	Initial send of LEN bytes dummy data\n"
	" -r		Receive data\n"
	"\n"
	"Example:\n"
	"jcat -i some_file_to_send  can0:0x80 :0x90,0x12300\n"
	"jcat can0:0x90 -r > /tmp/some_file_to_receive\n"
	"\n"
	;

static const char optstring[] = "?i:vs::rp:cnw::";


static void jcat_init_sockaddr_can(struct sockaddr_can *sac)
{
	sac->can_family = AF_CAN;
	sac->can_addr.j1939.addr = J1939_NO_ADDR;
	sac->can_addr.j1939.name = J1939_NO_NAME;
	sac->can_addr.j1939.pgn = J1939_NO_PGN;
}

static int jcat_sendfile(struct jcat_priv *priv, int out_fd, int in_fd,
			 off_t *offset, size_t count)
{
	int ret = EXIT_SUCCESS;
	off_t orig = 0;
	char *buf;
	size_t to_read, num_read, num_sent, tot_sent, buf_size;
	int round = 0;

	buf_size = min((size_t)J1939_MAX_ETP_PACKET_SIZE, count);
	buf = malloc(buf_size);
	if (!buf) {
		warn("can't allocate buf");
		ret = EXIT_FAILURE;
		goto do_nofree;
	}

	if (!offset) {

		/* Save current file offset and set offset to value in '*offset' */

		orig = lseek(in_fd, 0, SEEK_CUR);
		if (orig == -1) {
			ret = EXIT_FAILURE;
			goto do_free;
		}
		if (lseek(in_fd, *offset, SEEK_SET) == -1) {
			ret = EXIT_FAILURE;
			goto do_free;
		}
	}

	tot_sent = 0;

	while (count > 0) {
		to_read = min(buf_size, count);

		num_read = read(in_fd, buf, to_read);
		if (num_read == -1) {
			ret = EXIT_FAILURE;
			goto do_free;
		}
		if (num_read == 0)
			break; /* EOF */

		if (priv->valid_peername && !priv->todo_connect)
			num_sent = sendto(out_fd, buf, num_read, 0,
					(void *)&priv->peername,
					sizeof(priv->peername));
		else
			num_sent = send(out_fd, buf, num_read, 0);

		if (num_sent == -1) {
			warn("sendfile: write() transferr error");
			ret = EXIT_FAILURE;
			goto do_free;
		}

		if (num_sent == 0) /* Should never happen */ {
			warn("sendfile: write() transferred 0 bytes");
			ret = EXIT_FAILURE;
			goto do_free;
		}

		if (num_sent != num_read) {
			warn("sendfile: write() not full transfer: %zi %zi",
			     num_sent, num_read);
			ret = EXIT_FAILURE;
			goto do_free;
		}

		round++;
		count -= num_sent;
		tot_sent += num_sent;
	}

	if (!offset) {
		/* Return updated file offset in '*offset', and reset the file offset
		   to the value it had when we were called. */

		*offset = lseek(in_fd, 0, SEEK_CUR);
		if (*offset == -1) {
			ret = EXIT_FAILURE;
			goto do_free;
		}

		if (lseek(in_fd, orig, SEEK_SET) == -1) {
			ret = EXIT_FAILURE;
			goto do_free;
		}
	}

do_free:
	free(buf);
do_nofree:
	return ret;
}

static size_t jcat_get_file_size(int fd)
{
	off_t offset;

	offset = lseek(fd, 0, SEEK_END);
	if (offset == -1)
		error(1, errno, "%s lseek()\n", __func__);

	if (lseek(fd, 0, SEEK_SET) == -1)
		error(1, errno, "%s lseek() start\n", __func__);

	return offset;
}

static int jcat_send(struct jcat_priv *priv)
{

	if (priv->todo_filesize)
		priv->todo_send = jcat_get_file_size(priv->infile);

	if (!priv->todo_send)
		return EXIT_FAILURE;

	return jcat_sendfile(priv, priv->sock, priv->infile, NULL,
			     priv->todo_send);
}

static int jcat_recv_one(struct jcat_priv *priv, uint8_t *buf, size_t buf_size)
{
	socklen_t peernamelen;
	int ret;

	peernamelen = sizeof(priv->peername);
	ret = recvfrom(priv->sock, buf, buf_size, 0,
			(void *)&priv->peername, &peernamelen);
	if (ret < 0) {
		warn("recvfrom()");
		return EXIT_FAILURE;
	}

	ret = write(priv->outfile, buf, ret);
	if (ret < 0) {
		warn("write stdout()");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

static int jcat_recv(struct jcat_priv *priv)
{
	int ret = EXIT_SUCCESS;
	size_t buf_size;
	uint8_t *buf;

	buf_size = J1939_MAX_ETP_PACKET_SIZE;
	buf = malloc(buf_size);
	if (!buf) {
		warn("can't allocate rx buf");
		return EXIT_FAILURE;;
	}

	while (priv->todo_recv) {
		ret = jcat_recv_one(priv, buf, buf_size);
		if (ret)
			break;
	}

	free(buf);
	return ret;
}

static int jcat_sock_prepare(struct jcat_priv *priv)
{
	int ret;

	/* open socket */
	priv->sock = socket(PF_CAN, SOCK_DGRAM, CAN_J1939);
	if (priv->sock < 0) {
		warn("socket(j1939)");
		return EXIT_FAILURE;
	}

	if (priv->todo_prio >= 0) {
		ret = setsockopt(priv->sock, SOL_CAN_J1939, SO_J1939_SEND_PRIO,
				&priv->todo_prio, sizeof(priv->todo_prio));
		if (ret < 0) {
			warn("set priority %i", priv->todo_prio);
			return EXIT_FAILURE;
		}
	}

	ret = bind(priv->sock, (void *)&priv->sockname, sizeof(priv->sockname));
	if (ret < 0) {
		warn("bind()");
		return EXIT_FAILURE;
	}

	if (!priv->todo_connect)
		return EXIT_SUCCESS;

	if (!priv->valid_peername) {
		warn("no peername supplied");
		return EXIT_FAILURE;
	}
	ret = connect(priv->sock, (void *)&priv->peername,
		      sizeof(priv->peername));
	if (ret < 0) {
		warn("connect()");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

static int jcat_parse_args(struct jcat_priv *priv, int argc, char *argv[])
{
	int opt;

	/* argument parsing */
	while ((opt = getopt(argc, argv, optstring)) != -1)
	switch (opt) {
	case 'i':
		priv->infile = open(optarg, O_RDONLY);
		if (priv->infile == -1)
			error(EXIT_FAILURE, errno, "can't open input file");
		priv->todo_filesize = 1;
		break;
	case 's':
		priv->todo_send = strtoul(optarg ?: "8", NULL, 0);
		break;
	case 'r':
		priv->todo_recv = 1;
		break;
	case 'p':
		priv->todo_prio = strtoul(optarg, NULL, 0);
		break;
	case 'c':
		priv->todo_connect = 1;
		break;
	default:
		fputs(help_msg, stderr);
		return EXIT_FAILURE;
	}

	if (argv[optind]) {
		if (strcmp("-", argv[optind]))
			libj1939_parse_canaddr(argv[optind], &priv->sockname);
		optind++;
	}

	if (argv[optind]) {
		if (strcmp("-", argv[optind])) {
			libj1939_parse_canaddr(argv[optind], &priv->peername);
			priv->valid_peername = 1;
		}
		optind++;
	}

	return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
	struct jcat_priv *priv;
	int ret;

	priv = malloc(sizeof(*priv));
	if (!priv)
		error(EXIT_FAILURE, errno, "can't allocate priv");

	bzero(priv, sizeof(*priv));

	priv->todo_prio = -1;
	priv->infile = STDIN_FILENO;
	priv->outfile = STDOUT_FILENO;

	jcat_init_sockaddr_can(&priv->sockname);
	jcat_init_sockaddr_can(&priv->peername);

	ret = jcat_parse_args(priv, argc, argv);
	if (ret)
		return ret;

	ret = jcat_sock_prepare(priv);
	if (ret)
		return ret;

	if (priv->todo_recv)
		ret = jcat_recv(priv);
	else
		ret = jcat_send(priv);

	close(priv->infile);
	return ret;
}

