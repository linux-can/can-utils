/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2018 Pengutronix, Oleksij Rempel <o.rempel@pengutronix.de>
 */

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <net/if.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <linux/errqueue.h>
#include <linux/net_tstamp.h>
#include <linux/netlink.h>
#include <linux/socket.h>

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


struct j1939cat_stats {
	int err;
	uint32_t tskey;
	uint32_t send;
};

struct j1939cat_priv {
	int sock;
	int infile;
	int outfile;
	size_t max_transfer;
	unsigned long repeat;
	unsigned long round;
	int todo_prio;

	bool valid_peername;
	bool todo_recv;
	bool todo_filesize;
	bool todo_connect;
	int todo_broadcast;

	unsigned long polltimeout;

	struct sockaddr_can sockname;
	struct sockaddr_can peername;

	struct sock_extended_err *serr;
	struct scm_timestamping *tss;
	struct j1939cat_stats stats;
};

static const char help_msg[] =
	"j1939cat: netcat-like tool for j1939\n"
	"Usage: j1939cat [options] FROM TO\n"
	" FROM / TO	- or [IFACE][:[SA][,[PGN][,NAME]]]\n"
	"Options:\n"
	" -i <infile>	(default stdin)\n"
	" -s <size>	Set maximal transfer size. Default: 117440505 byte\n"
	" -r		Receive data\n"
	" -P <timeout>  poll timeout in milliseconds before sending data.\n"
	"		With this option send() will be used with MSG_DONTWAIT flag.\n"
	" -R <count>	Set send repeat count. Default: 1\n"
	" -B		Allow to send and receive broadcast packets.\n"
	"\n"
	"Example:\n"
	"j1939cat -i some_file_to_send  can0:0x80 :0x90,0x12300\n"
	"j1939cat can0:0x90 -r > /tmp/some_file_to_receive\n"
	"\n"
	;

static const char optstring[] = "?hi:vs:rp:P:R:B";


static void j1939cat_init_sockaddr_can(struct sockaddr_can *sac)
{
	sac->can_family = AF_CAN;
	sac->can_addr.j1939.addr = J1939_NO_ADDR;
	sac->can_addr.j1939.name = J1939_NO_NAME;
	sac->can_addr.j1939.pgn = J1939_NO_PGN;
}

static ssize_t j1939cat_send_one(struct j1939cat_priv *priv, int out_fd,
			     const void *buf, size_t buf_size)
{
	ssize_t num_sent;
	int flags = 0;

	if (priv->polltimeout)
		flags |= MSG_DONTWAIT;

	if (priv->valid_peername && !priv->todo_connect)
		num_sent = sendto(out_fd, buf, buf_size, flags,
				  (struct sockaddr *)&priv->peername,
				  sizeof(priv->peername));
	else
		num_sent = send(out_fd, buf, buf_size, flags);

	if (num_sent == -1) {
		warn("%s: transfer error: %i", __func__, -errno);
		return -errno;
	}

	if (num_sent == 0) /* Should never happen */ {
		warn("%s: transferred 0 bytes", __func__);
		return -EINVAL;
	}

	if (num_sent > (ssize_t)buf_size) /* Should never happen */ {
		warn("%s: send more then read", __func__);
		return -EINVAL;
	}

	return num_sent;
}

static void j1939cat_print_timestamp(struct j1939cat_priv *priv, const char *name,
			      struct timespec *cur)
{
	struct j1939cat_stats *stats = &priv->stats;

	if (!(cur->tv_sec | cur->tv_nsec))
		return;

	fprintf(stderr, "  %s: %lu s %lu us (seq=%u, send=%u)",
			name, cur->tv_sec, cur->tv_nsec / 1000,
			stats->tskey, stats->send);

	fprintf(stderr, "\n");
}

static const char *j1939cat_tstype_to_str(int tstype)
{
	switch (tstype) {
	case SCM_TSTAMP_SCHED:
		return "  ENQ";
	case SCM_TSTAMP_SND:
		return "  SND";
	case SCM_TSTAMP_ACK:
		return "  ACK";
	default:
		return "  unk";
	}
}

/* Check the stats of SCM_TIMESTAMPING_OPT_STATS */
static void j1939cat_scm_opt_stats(struct j1939cat_priv *priv, void *buf, int len)
{
	struct j1939cat_stats *stats = &priv->stats;
	int offset = 0;

	while (offset < len) {
		struct nlattr *nla = (struct nlattr *) ((char *)buf + offset);

		switch (nla->nla_type) {
		case J1939_NLA_BYTES_ACKED:
			stats->send = *(uint32_t *)((char *)nla + NLA_HDRLEN);
			break;
		default:
			warnx("not supported J1939_NLA field\n");
		}

		offset += NLA_ALIGN(nla->nla_len);
	}
}

static int j1939cat_extract_serr(struct j1939cat_priv *priv)
{
	struct j1939cat_stats *stats = &priv->stats;
	struct sock_extended_err *serr = priv->serr;
	struct scm_timestamping *tss = priv->tss;

	switch (serr->ee_origin) {
	case SO_EE_ORIGIN_TIMESTAMPING:
		/*
		 * We expect here following patterns:
		 *   serr->ee_info == SCM_TSTAMP_ACK
		 *     Activated with SOF_TIMESTAMPING_TX_ACK
		 * or
		 *   serr->ee_info == SCM_TSTAMP_SCHED
		 *     Activated with SOF_TIMESTAMPING_SCHED
		 * and
		 *   serr->ee_data == tskey
		 *     session message counter which is activate
		 *     with SOF_TIMESTAMPING_OPT_ID
		 * the serr->ee_errno should be ENOMSG
		 */
		if (serr->ee_errno != ENOMSG)
			warnx("serr: expected ENOMSG, got: %i",
			      serr->ee_errno);
		stats->tskey = serr->ee_data;

		j1939cat_print_timestamp(priv, j1939cat_tstype_to_str(serr->ee_info),
				     &tss->ts[0]);

		if (serr->ee_info == SCM_TSTAMP_SCHED)
			return -EINTR;
		return 0;
	case SO_EE_ORIGIN_LOCAL:
		/*
		 * The serr->ee_origin == SO_EE_ORIGIN_LOCAL is
		 * currently used to notify about locally
		 * detected protocol/stack errors.
		 * Following patterns are expected:
		 *   serr->ee_info == J1939_EE_INFO_TX_ABORT
		 *     is used to notify about session TX
		 *     abort.
		 *   serr->ee_data == tskey
		 *     session message counter which is activate
		 *     with SOF_TIMESTAMPING_OPT_ID
		 *   serr->ee_errno == actual error reason
		 *     error reason is converted from J1939
		 *     abort to linux error name space.
		 */
		if (serr->ee_info != J1939_EE_INFO_TX_ABORT)
			warnx("serr: unknown ee_info: %i",
			      serr->ee_info);

		j1939cat_print_timestamp(priv, "  ABT", &tss->ts[0]);
		warnx("serr: tx error: %i, %s", serr->ee_errno, strerror(serr->ee_errno));

		return serr->ee_errno;
	default:
		warnx("serr: wrong origin: %u", serr->ee_origin);
	}

	return 0;
}

static int j1939cat_parse_cm(struct j1939cat_priv *priv, struct cmsghdr *cm)
{
	const size_t hdr_len = CMSG_ALIGN(sizeof(struct cmsghdr));

	if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_TIMESTAMPING) {
		priv->tss = (void *)CMSG_DATA(cm);
	} else if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SCM_TIMESTAMPING_OPT_STATS) {
		void *jstats = (void *)CMSG_DATA(cm);

		/* Activated with SOF_TIMESTAMPING_OPT_STATS */
		j1939cat_scm_opt_stats(priv, jstats, cm->cmsg_len - hdr_len);
	} else if (cm->cmsg_level == SOL_CAN_J1939 &&
		   cm->cmsg_type == SCM_J1939_ERRQUEUE) {
		priv->serr = (void *)CMSG_DATA(cm);
	} else
		warnx("serr: not supported type: %d.%d",
		      cm->cmsg_level, cm->cmsg_type);

	return 0;
}

static int j1939cat_recv_err(struct j1939cat_priv *priv)
{
	char control[100];
	struct cmsghdr *cm;
	int ret;
	struct msghdr msg = {
		.msg_control = control,
		.msg_controllen = sizeof(control),
	};

	ret = recvmsg(priv->sock, &msg, MSG_ERRQUEUE);
	if (ret == -1)
		err(EXIT_FAILURE, "recvmsg error notification: %i", errno);
	if (msg.msg_flags & MSG_CTRUNC)
		err(EXIT_FAILURE, "recvmsg error notification: truncated");

	priv->serr = NULL;
	priv->tss = NULL;

	for (cm = CMSG_FIRSTHDR(&msg); cm && cm->cmsg_len;
	     cm = CMSG_NXTHDR(&msg, cm)) {
		j1939cat_parse_cm(priv, cm);
		if (priv->serr && priv->tss)
			return j1939cat_extract_serr(priv);
	}

	return 0;
}

static int j1939cat_send_loop(struct j1939cat_priv *priv, int out_fd, char *buf,
			  size_t buf_size)
{
	struct j1939cat_stats *stats = &priv->stats;
	ssize_t count;
	char *tmp_buf = buf;
	unsigned int events = POLLOUT | POLLERR;
	bool tx_done = false;

	count = buf_size;

	while (!tx_done) {
		ssize_t num_sent = 0;

		if (priv->polltimeout) {
			struct pollfd fds = {
				.fd = priv->sock,
				.events = events,
			};
			int ret;

			ret = poll(&fds, 1, priv->polltimeout);
			if (ret == -EINTR)
				continue;
			if (ret < 0)
				return -errno;
			if (!ret)
				return -ETIME;
			if (!(fds.revents & events)) {
				warn("%s: something else is wrong", __func__);
				return -EIO;
			}

			if (fds.revents & POLLERR) {
				ret = j1939cat_recv_err(priv);
				if (ret == -EINTR)
					continue;
				if (ret)
					return ret;
				if ((priv->repeat - 1) == stats->tskey)
					tx_done = true;
			}

			if (fds.revents & POLLOUT) {
				num_sent = j1939cat_send_one(priv, out_fd, tmp_buf, count);
				if (num_sent < 0)
					return num_sent;
			}
		} else {
			num_sent = j1939cat_send_one(priv, out_fd, tmp_buf, count);
			if (num_sent < 0)
				return num_sent;
		}

		count -= num_sent;
		tmp_buf += num_sent;
		if (buf + buf_size < tmp_buf + count) {
			warn("%s: send buffer is bigger than the read buffer",
			     __func__);
			return -EINVAL;
		}
		if (!count) {
			if (priv->repeat == priv->round)
				events = POLLERR;
			else
				tx_done = true;
		}
	}
	return 0;
}

static int j1939cat_sendfile(struct j1939cat_priv *priv, int out_fd, int in_fd,
			 off_t *offset, size_t count)
{
	int ret = EXIT_SUCCESS;
	off_t orig = 0;
	char *buf;
	ssize_t num_read;
	size_t to_read, buf_size;

	buf_size = min(priv->max_transfer, count);
	buf = malloc(buf_size);
	if (!buf) {
		warn("can't allocate buf");
		ret = EXIT_FAILURE;
		goto do_nofree;
	}

	if (offset) {

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

	while (count > 0) {
		to_read = min(buf_size, count);

		num_read = read(in_fd, buf, to_read);
		if (num_read == -1) {
			ret = EXIT_FAILURE;
			goto do_free;
		}
		if (num_read == 0)
			break; /* EOF */

		ret = j1939cat_send_loop(priv, out_fd, buf, num_read);
		if (ret)
			goto do_free;

		count -= num_read;
	}

	if (offset) {
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

static size_t j1939cat_get_file_size(int fd)
{
	off_t offset;

	offset = lseek(fd, 0, SEEK_END);
	if (offset == -1)
		err(1, "%s lseek()\n", __func__);

	if (lseek(fd, 0, SEEK_SET) == -1)
		err(1, "%s lseek() start\n", __func__);

	return offset;
}

static int j1939cat_send(struct j1939cat_priv *priv)
{
	unsigned int size = 0;
	unsigned int i;
	int ret;

	if (priv->todo_filesize)
		size = j1939cat_get_file_size(priv->infile);

	if (!size)
		return EXIT_FAILURE;

	for (i = 0; i < priv->repeat; i++) {
		priv->round++;
		ret = j1939cat_sendfile(priv, priv->sock, priv->infile, NULL, size);
		if (ret)
			break;

		if (lseek(priv->infile, 0, SEEK_SET) == -1)
			err(1, "%s lseek() start\n", __func__);
	}

	return ret;
}

static int j1939cat_recv_one(struct j1939cat_priv *priv, uint8_t *buf, size_t buf_size)
{
	int ret;

	ret = recv(priv->sock, buf, buf_size, 0);
	if (ret < 0) {
		warn("recvf()");
		return EXIT_FAILURE;
	}

	ret = write(priv->outfile, buf, ret);
	if (ret < 0) {
		warn("write stdout()");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

static int j1939cat_recv(struct j1939cat_priv *priv)
{
	int ret = EXIT_SUCCESS;
	size_t buf_size;
	uint8_t *buf;

	buf_size = priv->max_transfer;
	buf = malloc(buf_size);
	if (!buf) {
		warn("can't allocate rx buf");
		return EXIT_FAILURE;;
	}

	while (priv->todo_recv) {
		ret = j1939cat_recv_one(priv, buf, buf_size);
		if (ret)
			break;
	}

	free(buf);
	return ret;
}

static int j1939cat_sock_prepare(struct j1939cat_priv *priv)
{
	unsigned int sock_opt;
	int value;
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

	value = 1;
	ret = setsockopt(priv->sock, SOL_CAN_J1939, SO_J1939_ERRQUEUE, &value,
			 sizeof(value));
	if (ret < 0) {
		warn("set recverr");
		return EXIT_FAILURE;
	}

	sock_opt = SOF_TIMESTAMPING_SOFTWARE |
		   SOF_TIMESTAMPING_OPT_CMSG |
		   SOF_TIMESTAMPING_TX_ACK |
		   SOF_TIMESTAMPING_TX_SCHED |
		   SOF_TIMESTAMPING_OPT_STATS | SOF_TIMESTAMPING_OPT_TSONLY |
		   SOF_TIMESTAMPING_OPT_ID;

	if (setsockopt(priv->sock, SOL_SOCKET, SO_TIMESTAMPING,
		       (char *) &sock_opt, sizeof(sock_opt)))
		err(1, "setsockopt timestamping");

	if (priv->todo_broadcast) {
		ret = setsockopt(priv->sock, SOL_SOCKET, SO_BROADCAST,
				 &priv->todo_broadcast,
				 sizeof(priv->todo_broadcast));
		if (ret < 0) {
			warn("setsockopt: filed to set broadcast");
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

static int j1939cat_parse_args(struct j1939cat_priv *priv, int argc, char *argv[])
{
	int opt;

	/* argument parsing */
	while ((opt = getopt(argc, argv, optstring)) != -1)
		switch (opt) {
		case 'i':
			priv->infile = open(optarg, O_RDONLY);
			if (priv->infile == -1)
				err(EXIT_FAILURE, "can't open input file");
			priv->todo_filesize = 1;
			break;
		case 's':
			priv->max_transfer = strtoul(optarg, NULL, 0);
			if (priv->max_transfer > J1939_MAX_ETP_PACKET_SIZE)
				err(EXIT_FAILURE,
				    "used value (%zu) is bigger then allowed maximal size: %u.\n",
				    priv->max_transfer,
				    J1939_MAX_ETP_PACKET_SIZE);
			break;
		case 'r':
			priv->todo_recv = 1;
			break;
		case 'p':
			priv->todo_prio = strtoul(optarg, NULL, 0);
			break;
		case 'P':
			priv->polltimeout = strtoul(optarg, NULL, 0);
			break;
		case 'c':
			priv->todo_connect = 1;
			break;
		case 'R':
			priv->repeat = strtoul(optarg, NULL, 0);
			if (priv->repeat < 1)
				err(EXIT_FAILURE,
				    "send/repeat count can't be less then 1\n");
			break;
		case 'B':
			priv->todo_broadcast = 1;
			break;
		case 'h': /*fallthrough*/
		default:
			fputs(help_msg, stderr);
			return EXIT_FAILURE;
		}

	if (argv[optind]) {
		if (strcmp("-", argv[optind]) != 0)
			libj1939_parse_canaddr(argv[optind], &priv->sockname);
		optind++;
	}

	if (argv[optind]) {
		if (strcmp("-", argv[optind]) != 0) {
			libj1939_parse_canaddr(argv[optind], &priv->peername);
			priv->valid_peername = 1;
		}
		optind++;
	}

	return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
	struct j1939cat_priv *priv;
	int ret = 0;

	priv = calloc(1, sizeof(*priv));
	if (!priv)
		err(EXIT_FAILURE, "can't allocate priv");

	priv->todo_prio = -1;
	priv->infile = STDIN_FILENO;
	priv->outfile = STDOUT_FILENO;
	priv->max_transfer = J1939_MAX_ETP_PACKET_SIZE;
	priv->polltimeout = 100000;
	priv->repeat = 1;

	j1939cat_init_sockaddr_can(&priv->sockname);
	j1939cat_init_sockaddr_can(&priv->peername);

	ret = j1939cat_parse_args(priv, argc, argv);
	if (ret)
		return ret;

	ret = j1939cat_sock_prepare(priv);
	if (ret)
		return ret;

	if (priv->todo_recv)
		ret = j1939cat_recv(priv);
	else
		ret = j1939cat_send(priv);

	close(priv->infile);
	close(priv->outfile);
	close(priv->sock);

	return ret;
}
