/*
 * libsocketcan.c
 *
 * (C) 2009 Luotao Fu <l.fu@pengutronix.de> 
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>

#include <linux/rtnetlink.h>
#include <linux/netlink.h>

#include <libsocketcan.h>

#define parse_rtattr_nested(tb, max, rta) \
	(parse_rtattr((tb), (max), RTA_DATA(rta), RTA_PAYLOAD(rta)))

#define NLMSG_TAIL(nmsg) \
	((struct rtattr *) (((void *) (nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

#define IFLA_CAN_MAX	(__IFLA_CAN_MAX - 1)

#define IF_UP 1
#define IF_DOWN 2

#define GET_STATE 1
#define GET_RESTART_MS 2
#define GET_BITTIMING 3
#define GET_CTRLMODE 4
#define GET_CLOCK 5
#define GET_BITTIMING_CONST 6

struct get_req {
	struct nlmsghdr n;
	struct rtgenmsg g;
};

struct set_req {
	struct nlmsghdr n;
	struct ifinfomsg i;
	char buf[1024];
};

struct req_info {
	__u8 restart;
	__u8 disable_autorestart;
	__u32 restart_ms;
	struct can_ctrlmode *ctrlmode;
	struct can_bittiming *bittiming;
};

static void
parse_rtattr(struct rtattr **tb, int max, struct rtattr *rta, int len)
{
	memset(tb, 0, sizeof(*tb) * max);
	while (RTA_OK(rta, len)) {
		if (rta->rta_type <= max) {
			tb[rta->rta_type] = rta;
		}

		rta = RTA_NEXT(rta, len);
	}
}

static int addattr32(struct nlmsghdr *n, size_t maxlen, int type, __u32 data)
{
	int len = RTA_LENGTH(4);
	struct rtattr *rta;

	if (NLMSG_ALIGN(n->nlmsg_len) + len > maxlen) {
		fprintf(stderr,
			"addattr32: Error! max allowed bound %zu exceeded\n",
			maxlen);
		return -1;
	}

	rta = NLMSG_TAIL(n);
	rta->rta_type = type;
	rta->rta_len = len;
	memcpy(RTA_DATA(rta), &data, 4);
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + len;

	return 0;
}

static int addattr_l(struct nlmsghdr *n, size_t maxlen, int type,
		     const void *data, int alen)
{
	int len = RTA_LENGTH(alen);
	struct rtattr *rta;

	if (NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len) > maxlen) {
		fprintf(stderr,
			"addattr_l ERROR: message exceeded bound of %zu\n",
			maxlen);
		return -1;
	}

	rta = NLMSG_TAIL(n);
	rta->rta_type = type;
	rta->rta_len = len;
	memcpy(RTA_DATA(rta), data, alen);
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);

	return 0;
}

static int send_mod_request(int fd, struct nlmsghdr *n)
{
	int status;
	struct sockaddr_nl nladdr;
	struct nlmsghdr *h;

	struct iovec iov = {
		.iov_base = (void *)n,
		.iov_len = n->nlmsg_len
	};
	struct msghdr msg = {
		.msg_name = &nladdr,
		.msg_namelen = sizeof(nladdr),
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};
	char buf[16384];

	memset(&nladdr, 0, sizeof(nladdr));

	nladdr.nl_family = AF_NETLINK;
	nladdr.nl_pid = 0;
	nladdr.nl_groups = 0;

	n->nlmsg_seq = 0;
	n->nlmsg_flags |= NLM_F_ACK;

	status = sendmsg(fd, &msg, 0);

	if (status < 0) {
		perror("Cannot talk to rtnetlink");
		return -1;
	}

	iov.iov_base = buf;
	while (1) {
		iov.iov_len = sizeof(buf);
		status = recvmsg(fd, &msg, 0);
		for (h = (struct nlmsghdr *)buf; (size_t) status >= sizeof(*h);) {
			int len = h->nlmsg_len;
			int l = len - sizeof(*h);
			if (l < 0 || len > status) {
				if (msg.msg_flags & MSG_TRUNC) {
					fprintf(stderr, "Truncated message\n");
					return -1;
				}
				fprintf(stderr,
					"!!!malformed message: len=%d\n", len);
				return -1;
			}

			if (h->nlmsg_type == NLMSG_ERROR) {
				struct nlmsgerr *err =
				    (struct nlmsgerr *)NLMSG_DATA(h);
				if ((size_t) l < sizeof(struct nlmsgerr)) {
					fprintf(stderr, "ERROR truncated\n");
				} else {
					errno = -err->error;
					if (errno == 0)
						return 0;

					perror("RTNETLINK answers");
				}
				return -1;
			}
			status -= NLMSG_ALIGN(len);
			h = (struct nlmsghdr *)((char *)h + NLMSG_ALIGN(len));
		}
	}

	return 0;
}

static int send_dump_request(int fd, int family, int type)
{
	struct get_req req;

	memset(&req, 0, sizeof(req));

	req.n.nlmsg_len = sizeof(req);
	req.n.nlmsg_type = type;
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH;
	req.n.nlmsg_pid = 0;
	req.n.nlmsg_seq = 0;

	req.g.rtgen_family = family;

	return send(fd, (void *)&req, sizeof(req), 0);
}

static int open_nl_sock()
{
	int fd;
	int sndbuf = 32768;
	int rcvbuf = 32768;
	unsigned int addr_len;
	struct sockaddr_nl local;

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd < 0) {
		perror("Cannot open netlink socket");
		return -1;
	}

	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (void *)&sndbuf, sizeof(sndbuf));

	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void *)&rcvbuf, sizeof(rcvbuf));

	memset(&local, 0, sizeof(local));
	local.nl_family = AF_NETLINK;
	local.nl_groups = 0;

	if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
		perror("Cannot bind netlink socket");
		return -1;
	}

	addr_len = sizeof(local);
	if (getsockname(fd, (struct sockaddr *)&local, &addr_len) < 0) {
		perror("Cannot getsockname");
		return -1;
	}
	if (addr_len != sizeof(local)) {
		fprintf(stderr, "Wrong address length %d\n", addr_len);
		return -1;
	}
	if (local.nl_family != AF_NETLINK) {
		fprintf(stderr, "Wrong address family %d\n", local.nl_family);
		return -1;
	}
	return fd;
}

static int do_get_nl_link(int fd, __u8 acquire, const char *name, void *res)
{
	struct sockaddr_nl peer;

	char cbuf[64];
	char nlbuf[1024 * 8];

	int ret = -1;

	struct iovec iov = {
		.iov_base = (void *)nlbuf,
		.iov_len = sizeof(nlbuf),
	};

	struct msghdr msg = {
		.msg_name = (void *)&peer,
		.msg_namelen = sizeof(peer),
		.msg_iov = &iov,
		.msg_iovlen = 1,
		.msg_control = &cbuf,
		.msg_controllen = sizeof(cbuf),
		.msg_flags = 0,
	};
	struct nlmsghdr *nl_msg;
	ssize_t msglen;

	struct rtattr *linkinfo[IFLA_INFO_MAX + 1];
	struct rtattr *can_attr[IFLA_CAN_MAX + 1];

	if (send_dump_request(fd, AF_PACKET, RTM_GETLINK) < 0) {
		perror("Cannot send dump request");
		return ret;
	}

	if ((msglen = recvmsg(fd, &msg, 0)) <= 0) {
		perror("Receive error");
		return ret;
	}
	size_t u_msglen = (size_t) msglen;
	/* Check to see if the buffers in msg get truncated */
	if (msg.msg_namelen != sizeof(peer) ||
	    (msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
		fprintf(stderr, "Uhoh... truncated message.\n");
		return ret;
	}

	for (nl_msg = (struct nlmsghdr *)nlbuf;
	     NLMSG_OK(nl_msg, u_msglen);
	     nl_msg = NLMSG_NEXT(nl_msg, u_msglen)) {
		int type = nl_msg->nlmsg_type;
		int len;
		if (type != RTM_NEWLINK)
			continue;

		struct ifinfomsg *ifi = NLMSG_DATA(nl_msg);
		struct rtattr *tb[IFLA_MAX + 1];

		len =
		    nl_msg->nlmsg_len - NLMSG_LENGTH(sizeof(struct ifaddrmsg));
		parse_rtattr(tb, IFLA_MAX, IFLA_RTA(ifi), len);

		if (strncmp
		    ((char *)RTA_DATA(tb[IFLA_IFNAME]), name,
		     sizeof(name)) != 0)
			continue;

		if (tb[IFLA_LINKINFO])
			parse_rtattr_nested(linkinfo,
					    IFLA_INFO_MAX, tb[IFLA_LINKINFO]);
		else
			continue;

		if (!linkinfo[IFLA_INFO_DATA]) {
			fprintf(stderr, "no link data found\n");
			return ret;
		}

		parse_rtattr_nested(can_attr, IFLA_CAN_MAX,
				    linkinfo[IFLA_INFO_DATA]);

		switch (acquire) {
		case GET_STATE:
			if (can_attr[IFLA_CAN_STATE]) {
				*((int *)res) = *((__u32 *)
						  RTA_DATA(can_attr
							   [IFLA_CAN_STATE]));
				ret = 0;
			} else {
				fprintf(stderr, "no state data found\n");
			}

			break;
		case GET_RESTART_MS:
			if (can_attr[IFLA_CAN_RESTART_MS]) {
				*((__u32 *) res) = *((__u32 *)
						     RTA_DATA(can_attr
							      [IFLA_CAN_RESTART_MS]));
				ret = 0;
			} else
				fprintf(stderr, "no restart_ms data found\n");

			break;
		case GET_BITTIMING:
			if (can_attr[IFLA_CAN_BITTIMING]) {
				memcpy(res,
				       RTA_DATA(can_attr[IFLA_CAN_BITTIMING]),
				       sizeof(struct can_bittiming));
				ret = 0;
			} else
				fprintf(stderr, "no bittiming data found\n");

			break;
		case GET_CTRLMODE:
			if (can_attr[IFLA_CAN_CTRLMODE]) {
				memcpy(res,
				       RTA_DATA(can_attr[IFLA_CAN_CTRLMODE]),
				       sizeof(struct can_ctrlmode));
				ret = 0;
			} else
				fprintf(stderr, "no ctrlmode data found\n");

			break;
		case GET_CLOCK:
			if (can_attr[IFLA_CAN_CLOCK]) {
				memcpy(res,
				       RTA_DATA(can_attr[IFLA_CAN_CLOCK]),
				       sizeof(struct can_clock));
				ret = 0;
			} else
				fprintf(stderr,
					"no clock parameter data found\n");

			break;
		case GET_BITTIMING_CONST:
			if (can_attr[IFLA_CAN_BITTIMING_CONST]) {
				memcpy(res,
				       RTA_DATA(can_attr[IFLA_CAN_BITTIMING_CONST]),
				       sizeof(struct can_bittiming_const));
				ret = 0;
			} else
				fprintf(stderr, "no bittiming_const data found\n");

			break;
		default:
			fprintf(stderr, "unknown acquire mode\n");
		}
	}
	return ret;
}

static int get_link(const char *name, __u8 acquire, void *res)
{
	int fd;
	int err = -1;

	fd = open_nl_sock();
	if (fd < 0)
		goto err_out;

	err = do_get_nl_link(fd, acquire, name, res);
	if (err < 0)
		goto close_out;

close_out:
	close(fd);
err_out:
	return err;

}

static int do_set_nl_link(int fd, __u8 if_state, const char *name,
			  struct req_info *req_info)
{
	struct set_req req;

	const char *type = "can";

	memset(&req, 0, sizeof(req));

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.n.nlmsg_type = RTM_NEWLINK;
	req.i.ifi_family = 0;

	req.i.ifi_index = if_nametoindex(name);
	if (req.i.ifi_index == 0) {
		fprintf(stderr, "Cannot find device \"%s\"\n", name);
		return -1;
	}

	if (if_state) {
		switch (if_state) {
		case IF_DOWN:
			req.i.ifi_change |= IFF_UP;
			req.i.ifi_flags &= ~IFF_UP;
			break;
		case IF_UP:
			req.i.ifi_change |= IFF_UP;
			req.i.ifi_flags |= IFF_UP;
			break;
		default:
			fprintf(stderr, "unknown state\n");
			return -1;
		}
	}

	if (req_info != NULL) {
		/* setup linkinfo section */
		struct rtattr *linkinfo = NLMSG_TAIL(&req.n);
		addattr_l(&req.n, sizeof(req), IFLA_LINKINFO, NULL, 0);
		addattr_l(&req.n, sizeof(req), IFLA_INFO_KIND, type,
			  strlen(type));
		/* setup data section */
		struct rtattr *data = NLMSG_TAIL(&req.n);
		addattr_l(&req.n, sizeof(req), IFLA_INFO_DATA, NULL, 0);

		if (req_info->restart_ms > 0 || req_info->disable_autorestart)
			addattr32(&req.n, 1024, IFLA_CAN_RESTART_MS,
				  req_info->restart_ms);

		if (req_info->restart)
			addattr32(&req.n, 1024, IFLA_CAN_RESTART, 1);

		if (req_info->bittiming != NULL) {
			addattr_l(&req.n, 1024, IFLA_CAN_BITTIMING,
				  req_info->bittiming,
				  sizeof(struct can_bittiming));
		}

		if (req_info->ctrlmode != NULL) {
			addattr_l(&req.n, 1024, IFLA_CAN_CTRLMODE,
				  req_info->ctrlmode,
				  sizeof(struct can_ctrlmode));
		}

		/* mark end of data section */
		data->rta_len = (void *)NLMSG_TAIL(&req.n) - (void *)data;

		/* mark end of link info section */
		linkinfo->rta_len =
		    (void *)NLMSG_TAIL(&req.n) - (void *)linkinfo;
	}

	return send_mod_request(fd, &req.n);
}

static int set_link(const char *name, __u8 if_state, struct req_info *req_info)
{
	int fd;
	int err = 0;

	fd = open_nl_sock();
	if (fd < 0)
		goto err_out;

	err = do_set_nl_link(fd, if_state, name, req_info);
	if (err < 0)
		goto close_out;

close_out:
	close(fd);
err_out:
	return err;
}

int can_do_start(const char *name)
{
	return set_link(name, IF_UP, NULL);
}

int can_do_stop(const char *name)
{
	return set_link(name, IF_DOWN, NULL);
}

int can_do_restart(const char *name)
{
	int fd;
	int err = -1;
	int state;
	__u32 restart_ms;

	/* first we check if we can restart the device at all */
	if ((can_get_state(name, &state)) < 0) {
		fprintf(stderr, "cannot get bustate, "
			"something is seriously wrong\n");
		goto err_out;
	} else if (state != CAN_STATE_BUS_OFF) {
		fprintf(stderr,
			"Device is not in BUS_OFF," " no use to restart\n");
		goto err_out;
	}

	if ((can_get_restart_ms(name, &restart_ms)) < 0) {
		fprintf(stderr, "cannot get restart_ms, "
			"something is seriously wrong\n");
		goto err_out;
	} else if (restart_ms > 0) {
		fprintf(stderr,
			"auto restart with %ums interval is turned on,"
			" no use to restart\n", restart_ms);
		goto err_out;
	}

	struct req_info req_info = {
		.restart = 1,
	};

	fd = open_nl_sock();
	if (fd < 0)
		goto err_out;

	err = do_set_nl_link(fd, 0, name, &req_info);
	if (err < 0)
		goto close_out;

close_out:
	close(fd);
err_out:
	return err;
}

int can_set_restart_ms(const char *name, __u32 restart_ms)
{
	struct req_info req_info = {
		.restart_ms = restart_ms,
	};

	if (restart_ms == 0)
		req_info.disable_autorestart = 1;

	return set_link(name, 0, &req_info);
}

int can_set_ctrlmode(const char *name, struct can_ctrlmode *cm)
{
	struct req_info req_info = {
		.ctrlmode = cm,
	};

	return set_link(name, 0, &req_info);
}

int can_set_bittiming(const char *name, struct can_bittiming *bt)
{
	struct req_info req_info = {
		.bittiming = bt,
	};

	return set_link(name, 0, &req_info);
}

int can_set_bitrate(const char *name, __u32 bitrate)
{
	struct can_bittiming bt;

	memset(&bt, 0, sizeof(bt));
	bt.bitrate = bitrate;

	return can_set_bittiming(name, &bt);
}

int can_set_bitrate_samplepoint(const char *name, __u32 bitrate,
				__u32 sample_point)
{
	struct can_bittiming bt;

	memset(&bt, 0, sizeof(bt));
	bt.bitrate = bitrate;
	bt.sample_point = sample_point;

	return can_set_bittiming(name, &bt);
}

int can_get_state(const char *name, int *state)
{
	return get_link(name, GET_STATE, state);
}

int can_get_restart_ms(const char *name, __u32 *restart_ms)
{
	return get_link(name, GET_RESTART_MS, restart_ms);
}

int can_get_bittiming(const char *name, struct can_bittiming *bt)
{
	return get_link(name, GET_BITTIMING, bt);
}

int can_get_ctrlmode(const char *name, struct can_ctrlmode *cm)
{
	return get_link(name, GET_CTRLMODE, cm);
}

int can_get_clock(const char *name, struct can_clock *clock)
{
	return get_link(name, GET_CLOCK, clock);
}

int can_get_bittiming_const(const char *name, struct can_bittiming_const *btc)
{
	return get_link(name, GET_BITTIMING_CONST, btc);
}

