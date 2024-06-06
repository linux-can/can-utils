// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2024 Oleksij Rempel <linux@rempel-privat.de>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/kernel.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "j1939_timedate_cmn.h"

#define J1939_TIMEDATE_SRV_MAX_EPOLL_EVENTS	10

struct j1939_timedate_srv_priv {
	int sock_nack;
	int sock_main;

	struct sockaddr_can sockname;

	struct j1939_timedate_stats stats;

	struct libj1939_cmn cmn;
};

static void gmtime_to_j1939_pgn_65254_td(struct j1939_time_date_packet *tdp)
{
	struct tm utc_tm_buf, local_tm_buf;
	int hour_offset, minute_offset;
	struct tm *utc_tm, *local_tm;
	int year_since_1985;
	time_t now;

	now = time(NULL);
	utc_tm = gmtime_r(&now, &utc_tm_buf);
	local_tm = localtime_r(&now, &local_tm_buf);

	/* Calculate the offsets */
	hour_offset = local_tm->tm_hour - utc_tm->tm_hour;
	minute_offset = local_tm->tm_min - utc_tm->tm_min;

	/* Handle date rollover */
	if (local_tm->tm_mday != utc_tm->tm_mday) {
		if (local_tm->tm_hour < 12)
			hour_offset += 24;  /* past midnight */
		else
			hour_offset -= 24;  /* before midnight */
	}

	/*
	 * Seconds (spn959):
	 *  Resolution: 0.25 /bit, 0 offset
	 *  Data Range: 0 to 250 (0 to 62.5 seconds)
	 *  Operational Range: 0 to 239 (0 to 59.75 seconds)
	 */ 
	tdp->seconds = (uint8_t)(utc_tm->tm_sec / 0.25);
	if (tdp->seconds > 239)
		tdp->seconds = 239;

	/*
	 * Minutes (spn960):
	 *  Resolution: 1 min/bit, 0 offset
	 *  Data Range: 0 to 250 (0 to 250 minutes)
	 *  Operational Range: 0 to 59 (0 to 59 minutes)
	 */
	tdp->minutes = (uint8_t)utc_tm->tm_min;
	if (tdp->minutes > 59)
		tdp->minutes = 59;

	/*
	 * Hours (spn961):
	 *  Resolution: 1 hr/bit, 0 offset
	 *  Data Range: 0 to 250 (0 to 250 hours)
	 *  Operational Range: 0 to 23 (0 to 23 hours)
	 */
	tdp->hours = (uint8_t)utc_tm->tm_hour;
	if (tdp->hours > 23)
		tdp->hours = 23;

	/*
	 * Day (spn962):
	 * Resolution: 0.25 /bit, 0 offset
	 * Data Range: 0 to 250 (0 to 62.5 days)
	 * Operational Range: 1 to 127 (0.25 to 31.75 days)
	 */
	tdp->day = (uint8_t)(utc_tm->tm_mday / 0.25);
	if (tdp->day < 1 || tdp->day > 127)
		tdp->day = 1;

	/*
	 * Month (spn963):
	 * Resolution: 1 month/bit, 0 offset
	 * Data Range: 0 to 250 (0 to 250 months)
	 * Operational Range: 1 to 12 (1 to 12 months)
	 */
	tdp->month = (uint8_t)(utc_tm->tm_mon + 1);
	if (tdp->month < 1 || tdp->month > 12)
		tdp->month = 1;

	/*
	 * Year (spn964):
	 * Resolution: 1 year/bit, 1985 offset
	 * Data Range: 0 to 250 (1985 to 2235)
	 * Operational Range: 0 to 250 (1985 to 2235)
	 */
	year_since_1985 = utc_tm->tm_year - 85;
	if (year_since_1985 < 0) {
		/* Fallback to year 1985 if year is before 1985 */
		tdp->year = 0;
	} else if (year_since_1985 > 250) {
		/* Fallback to year 2235 if year is beyond 2235 */
		tdp->year = 250;
	} else {
		tdp->year = (uint8_t)year_since_1985;
	}

	/*
	 * Local minute offset (spn1601):
	 * Resolution: 1 min/bit, -125 offset
	 * Data Range: -125 to 125 minutes
	 * Operational Range: -59 to 59 minutes
	 */
	tdp->local_minute_offset = (int8_t)minute_offset;

	/*
	 * Local hour offset (spn1602):
	 * Resolution: 1 hr/bit, -125 offset
	 * Data Range: -125 to +125 hours
	 * Operational Range: -24 to +23 hours
	 * Note: If the local hour offset parameter is  equal to 125 (0xFA),
	 * the local time then the time parameter is the local time instead of
	 * UTC.
	 */
	tdp->local_hour_offset = (int8_t)hour_offset;
}

static int j1939_timedate_srv_send_res(struct j1939_timedate_srv_priv *priv,
				       struct sockaddr_can *addr)
{
	struct sockaddr_can peername = *addr;
	struct j1939_time_date_packet tdp;
	int ret;

	gmtime_to_j1939_pgn_65254_td(&tdp);

	peername.can_addr.j1939.pgn = J1939_PGN_TD;
	ret = sendto(priv->sock_main, &tdp, sizeof(tdp), 0,
		     (struct sockaddr *)&peername, sizeof(peername));
	if (ret == -1) {
		ret = -errno;
		pr_warn("failed to send data: %i (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

// check if the received message is a request for the time and date
static int j1939_timedate_srv_process_request(struct j1939_timedate_srv_priv *priv,
					       struct j1939_timedate_msg *msg)
{

	if (msg->buf[0] != (J1939_PGN_TD & 0xff) ||
	    msg->buf[1] != ((J1939_PGN_TD >> 8) & 0xff) ||
	    msg->buf[2] != ((J1939_PGN_TD >> 16) & 0xff)) {
		/* Don't care, not a time and date request */
		return 0;
	}

	return j1939_timedate_srv_send_res(priv, &msg->peername);
}

static int j1939_timedate_srv_rx_buf(struct j1939_timedate_srv_priv *priv, struct j1939_timedate_msg *msg)
{
	pgn_t pgn = msg->peername.can_addr.j1939.pgn;
	int ret = 0;

	switch (pgn) {
	case J1939_PGN_REQUEST_PGN:
		ret = j1939_timedate_srv_process_request(priv, msg);
		break;
	default:
		pr_warn("%s: unsupported PGN: %x", __func__, pgn);
		/* Not a critical error */
		break;
	}

	return ret;
}

static int j1939_timedate_srv_rx_one(struct j1939_timedate_srv_priv *priv, int sock)
{
	struct j1939_timedate_msg *msg;
	int flags = 0;
	int ret;

	msg = malloc(sizeof(*msg));
	if (!msg) {
		pr_err("can't allocate rx msg struct\n");
		return -ENOMEM;
	}
	msg->buf_size = J1939_TIMEDATE_MAX_TRANSFER_LENGH;
	msg->peer_addr_len = sizeof(msg->peername);
	msg->sock = sock;

	ret = recvfrom(sock, &msg->buf[0], msg->buf_size, flags,
		       (struct sockaddr *)&msg->peername, &msg->peer_addr_len);

	if (ret < 0) {
		ret = -errno;
		pr_warn("recvfrom() failed: %i %s", ret, strerror(-ret));
		return ret;
	}

	if (ret < 3) {
		pr_warn("received too short message: %i", ret);
		return -EINVAL;
	}

	msg->len = ret;

	ret = j1939_timedate_srv_rx_buf(priv, msg);
	if (ret < 0) {
		pr_warn("failed to process rx buf: %i (%s)\n", ret,
			strerror(ret));
		return ret;
	}

	return 0;
}

static int j1939_timedate_srv_handle_events(struct j1939_timedate_srv_priv *priv,
					    unsigned int nfds)
{
	int ret;
	unsigned int n;

	for (n = 0; n < nfds && n < priv->cmn.epoll_events_size; ++n) {
		struct epoll_event *ev = &priv->cmn.epoll_events[n];

		if (!ev->events) {
			warn("no events");
			continue;
		}

		if (ev->events & POLLIN) {
			ret = j1939_timedate_srv_rx_one(priv, ev->data.fd);
			if (ret) {
				warn("recv one");
				return ret;
			}
		}
	}
	return 0;
}

static int j1939_timedate_srv_process_events_and_tasks(struct j1939_timedate_srv_priv *priv)
{
	int ret, nfds;

	ret = libj1939_prepare_for_events(&priv->cmn, &nfds, false);
	if (ret)
		return ret;

	if (nfds > 0) {
		ret = j1939_timedate_srv_handle_events(priv, nfds);
		if (ret)
			return ret;
	}

	return 0;
}

static int j1939_timedate_srv_sock_main_prepare(struct j1939_timedate_srv_priv *priv)
{
	struct sockaddr_can addr = priv->sockname;
	int ret;

	ret = libj1939_open_socket();
	if (ret < 0)
		return ret;

	priv->sock_main = ret;

	ret = libj1939_bind_socket(priv->sock_main, &addr);
	if (ret < 0)
		return ret;

	ret = libj1939_socket_prio(priv->sock_main,
				   J1939_TIMEDATE_PRIO_DEFAULT);
	if (ret < 0)
		return ret;

	ret = libj1939_set_broadcast(priv->sock_main);
	if (ret < 0)
		return ret;


	return libj1939_add_socket_to_epoll(priv->cmn.epoll_fd,
						priv->sock_main, EPOLLIN);
}

static int j1939_timedate_srv_sock_prepare(struct j1939_timedate_srv_priv *priv)
{
	int ret;

	ret = libj1939_create_epoll();
	if (ret < 0)
		return ret;

	priv->cmn.epoll_fd = ret;

	priv->cmn.epoll_events = calloc(J1939_TIMEDATE_SRV_MAX_EPOLL_EVENTS,
					sizeof(struct epoll_event));
	if (!priv->cmn.epoll_events)
		return -ENOMEM;

	priv->cmn.epoll_events_size = J1939_TIMEDATE_SRV_MAX_EPOLL_EVENTS;

	return j1939_timedate_srv_sock_main_prepare(priv);
}

static void j1939_timedate_srv_print_help(void)
{
	printf("Usage: j1939-timedate-srv [options]\n");
	printf("Options:\n");
	printf("  --interface <interface_name> or -i <interface_name>\n");
	printf("      Specifies the CAN interface to use (mandatory).\n");
	printf("  --local-address <local_address_hex> or -a <local_address_hex>\n");
	printf("      Specifies the local address in hexadecimal (mandatory if\n");
	printf("      local name is not provided).\n");
	printf("  --local-name <local_name_hex> or -n <local_name_hex>\n");
	printf("      Specifies the local NAME in hexadecimal (mandatory if\n");
	printf("      local address is not provided).\n");
	printf("\n");
	printf("Note: Local address and local name are mutually exclusive and one\n");
	printf("      must be provided.\n");
	printf("\n");
	printf("Usage Examples:\n");
	printf("  Using local address:\n");
	printf("    j1939-timedate-srv -i vcan0 -a 0x90\n");
	printf("\n");
	printf("  Using local NAME:\n");
	printf("    j1939acd -r 64-95 -c /tmp/1122334455667789.jacd 1122334455667789 vcan0 &\n");
	printf("    j1939-timedate-srv -i vcan0 -n 0x1122334455667789\n");
}

static int j1939_timedate_srv_parse_args(struct j1939_timedate_srv_priv *priv,
					 int argc, char *argv[])
{
	struct sockaddr_can *local = &priv->sockname;
	bool local_address_set = false;
	bool local_name_set = false;
	bool interface_set = false;
	int long_index = 0;
	int opt;

	static struct option long_options[] = {
		{"interface", required_argument, 0, 'i'},
		{"local-address", required_argument, 0, 'a'},
		{"local-name", required_argument, 0, 'n'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "a:n:i:", long_options, &long_index)) != -1) {
		switch (opt) {
		case 'a':
			local->can_addr.j1939.addr = strtoul(optarg, NULL, 16);
			local_address_set = true;
			break;
		case 'n':
			local->can_addr.j1939.name = strtoull(optarg, NULL, 16);
			local_name_set = true;
			break;
		case 'i':
			local->can_ifindex = if_nametoindex(optarg);
			if (!local->can_ifindex) {
				pr_err("Interface %s not found. Error: %d (%s)\n",
				       optarg, errno, strerror(errno));
				return -EINVAL;
			}
			interface_set = true;
			break;
		default:
			j1939_timedate_srv_print_help();
			return -EINVAL;
		}
	}

	if (!interface_set) {
		pr_err("interface not specified");
		j1939_timedate_srv_print_help();
		return -EINVAL;
	}

	if (local_address_set && local_name_set) {
		pr_err("local address and local name or remote address and remote name are mutually exclusive");
		j1939_timedate_srv_print_help();
		return -EINVAL;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct j1939_timedate_srv_priv *priv;
	struct timespec ts;
	int ret;

	priv = malloc(sizeof(*priv));
	if (!priv)
		err(EXIT_FAILURE, "can't allocate priv");

	bzero(priv, sizeof(*priv));

	libj1939_init_sockaddr_can(&priv->sockname, J1939_PGN_REQUEST_PGN);

	ret = j1939_timedate_srv_parse_args(priv, argc, argv);
	if (ret)
		return ret;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	priv->cmn.next_send_time = ts;

	ret = j1939_timedate_srv_sock_prepare(priv);
	if (ret)
		return ret;

	while (1) {
		ret = j1939_timedate_srv_process_events_and_tasks(priv);
		if (ret)
			break;
	}

	close(priv->cmn.epoll_fd);
	free(priv->cmn.epoll_events);

	close(priv->sock_main);
	close(priv->sock_nack);

	return ret;
}

