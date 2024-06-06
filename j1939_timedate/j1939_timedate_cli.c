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

#define J1939_TIMEDATE_CLI_MAX_EPOLL_EVENTS	10

struct j1939_timedate_cli_priv {
	int sock_nack;
	int sock_main;

	struct sockaddr_can sockname;
	struct sockaddr_can peername;

	struct j1939_timedate_stats stats;

	struct libj1939_cmn cmn;
	struct timespec wait_until_time;

	bool utc;
	bool broadcast;
	bool done;
};

static void print_time_date_packet(struct j1939_timedate_cli_priv *priv,
				   const struct j1939_timedate_msg *msg)
{
	const struct j1939_time_date_packet *tdp =
		(const struct j1939_time_date_packet *)msg->buf;
	char timezone_offset[] = "+00:00 (Local Time)";
	char time_buffer[64];
	int actual_hour, actual_minute;
	int actual_year, actual_month;
	double actual_seconds;
	double actual_day;

	if (msg->len < (int)sizeof(*tdp)) {
		pr_warn("received too short time and date packet: %zi",
			msg->len);
		return;
	}

	actual_year = 1985 + tdp->year;
	actual_month = tdp->month;
	actual_day = tdp->day * 0.25;
	actual_hour = tdp->hours;
	actual_minute = tdp->minutes;
	actual_seconds = tdp->seconds * 0.25;

	if (tdp->local_hour_offset == 125) {
		snprintf(timezone_offset, sizeof(timezone_offset),
			 "+00:00 (Local Time)");
	} else 	if (!priv->utc) {
		actual_hour += tdp->local_hour_offset;
		actual_minute += tdp->local_minute_offset;

		/* Wraparound for hours and minutes if needed */
		if (actual_minute >= 60) {
			actual_minute -= 60;
			actual_hour++;
		} else if (actual_minute < 0) {
			actual_minute += 60;
			actual_hour--;
		}

		if (actual_hour >= 24) {
			actual_hour -= 24;
			actual_day++;
		} else if (actual_hour < 0) {
			actual_hour += 24;
			actual_day--;
		}

		snprintf(timezone_offset, sizeof(timezone_offset), "%+03d:%02d",
			 tdp->local_hour_offset, abs(tdp->local_minute_offset));
	} else {
		snprintf(timezone_offset, sizeof(timezone_offset),
			 "+00:00 (UTC)");
	}

	snprintf(time_buffer, sizeof(time_buffer),
		 "%d-%02d-%02.0f %02d:%02d:%05.2f%.20s",
		 actual_year, actual_month, actual_day, actual_hour,
		 actual_minute, actual_seconds, timezone_offset);

	printf("SA: 0x%02X, NAME:  0x%016llX, Time: %s\n",
	       msg->peername.can_addr.j1939.addr,
	       msg->peername.can_addr.j1939.name, time_buffer);

	if (!priv->broadcast)
		priv->done = true;
}

static int j1939_timedate_cli_rx_buf(struct j1939_timedate_cli_priv *priv,
				     struct j1939_timedate_msg *msg)
{
	pgn_t pgn = msg->peername.can_addr.j1939.pgn;
	int ret = 0;

	switch (pgn) {
	case J1939_PGN_TD:
		print_time_date_packet(priv, msg);
		break;
	default:
		pr_warn("%s: unsupported PGN: %x", __func__, pgn);
		/* Not a critical error */
		break;
	}

	return ret;
}

static int j1939_timedate_cli_rx_one(struct j1939_timedate_cli_priv *priv,
				     int sock)
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

	ret = j1939_timedate_cli_rx_buf(priv, msg);
	if (ret < 0) {
		pr_warn("failed to process rx buf: %i (%s)\n", ret,
			strerror(ret));
		return ret;
	}

	return 0;
}

static int j1939_timedate_cli_handle_events(struct j1939_timedate_cli_priv *priv,
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
			ret = j1939_timedate_cli_rx_one(priv, ev->data.fd);
			if (ret) {
				warn("recv one");
				return ret;
			}
		}
	}
	return 0;
}

static int j1939_timedate_cli_process_events_and_tasks(struct j1939_timedate_cli_priv *priv)
{
	int ret, nfds;

	ret = libj1939_prepare_for_events(&priv->cmn, &nfds, false);
	if (ret)
		return ret;

	if (nfds > 0) {
		ret = j1939_timedate_cli_handle_events(priv, nfds);
		if (ret)
			return ret;
	}

	return 0;
}

static int j1939_timedate_cli_send_req(struct j1939_timedate_cli_priv *priv)
{
	struct sockaddr_can addr = priv->peername;
	uint8_t data[3] = {0};
	int ret;

	addr.can_addr.j1939.pgn = J1939_PGN_REQUEST_PGN;

	data[0] = J1939_PGN_TD & 0xff;
	data[1] = (J1939_PGN_TD >> 8) & 0xff;
	data[2] = (J1939_PGN_TD >> 16) & 0xff;

	ret = sendto(priv->sock_main, data, sizeof(data), 0,
		     (struct sockaddr *)&addr, sizeof(addr));
	if (ret == -1) {
		ret = -errno;
		pr_warn("failed to send data: %i (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

static int j1939_timedate_cli_sock_main_prepare(struct j1939_timedate_cli_priv *priv)
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

static int j1939_timedate_cli_sock_nack_prepare(struct j1939_timedate_cli_priv *priv)
{
	struct sockaddr_can addr = priv->sockname;
	int ret;

	ret = libj1939_open_socket();
	if (ret < 0)
		return ret;

	priv->sock_nack = ret;

	addr.can_addr.j1939.pgn = ISOBUS_PGN_ACK;
	ret = libj1939_bind_socket(priv->sock_nack, &addr);
	if (ret < 0)
		return ret;

	return libj1939_add_socket_to_epoll(priv->cmn.epoll_fd,
						priv->sock_nack, EPOLLIN);
}

static int j1939_timedate_cli_sock_prepare(struct j1939_timedate_cli_priv *priv)
{
	int ret;

	ret = libj1939_create_epoll();
	if (ret < 0)
		return ret;

	priv->cmn.epoll_fd = ret;

	priv->cmn.epoll_events = calloc(J1939_TIMEDATE_CLI_MAX_EPOLL_EVENTS,
					sizeof(struct epoll_event));
	if (!priv->cmn.epoll_events)
		return -ENOMEM;

	priv->cmn.epoll_events_size = J1939_TIMEDATE_CLI_MAX_EPOLL_EVENTS;

	ret = j1939_timedate_cli_sock_main_prepare(priv);
	if (ret < 0)
		return ret;

	return j1939_timedate_cli_sock_nack_prepare(priv);
}

static void j1939_timedate_cli_print_help(void)
{
	printf("Usage: j1939_timedate-cli [options]\n");
	printf("Options:\n");
	printf("  --interface <interface_name> or -i <interface_name>\n");
	printf("	  Specifies the CAN interface to use (mandatory).\n");
	printf("  --local-address <local_address_hex> or -a <local_address_hex>\n");
	printf("	  Specifies the local address in hexadecimal (mandatory if local name is not provided).\n");
	printf("  --local-name <local_name_hex> or -n <local_name_hex>\n");
	printf("	  Specifies the local NAME in hexadecimal (mandatory if local address is not provided).\n");
	printf("  --remote-address <remote_address_hex> or -r <remote_address_hex>\n");
	printf("	  Specifies the remote address in hexadecimal (optional).\n");
	printf("  --remote-name <remote_name_hex> or -m <remote_name_hex>\n");
	printf("	  Specifies the remote NAME in hexadecimal (optional).\n");
	printf("  --utc or -u\n");
	printf("	  Outputs the time in UTC format.\n");
	printf("\n");
	printf("Note:\n");
	printf("  Local address and local name are mutually exclusive and one must be provided.\n");
	printf("  Remote address and remote name are mutually exclusive. \n");
	printf("  If no remote property is provided, the broadcast address will be used.\n");
	printf("\n");
	printf("Behavior:\n");
	printf("  In unicast mode (remote address or remote name provided),\n");
	printf("  the client will send a request and wait for the first response, then exit.\n");
	printf("  In broadcast mode (no remote address or remote name provided),\n");
	printf("  the program will wait 1000 milliseconds to collect responses, then exit.\n");
	printf("\n");
	printf("Time Output Formats:\n");
	printf("  YYYY-MM-DD HH:MM:SS.SS+00:00 (Local Time) - when no time zone information is received.\n");
	printf("  YYYY-MM-DD HH:MM:SS.SS+00:00 (UTC) - when the --utc option is used.\n");
	printf("  YYYY-MM-DD HH:MM:SS.SS+00:00 - default response with time zone offset automatically calculated.\n");
	printf("\n");
	printf("Complete Message Format:\n");
	printf("  The message will include the Source Address (SA) and J1939 NAME, formatted as follows:\n");
	printf("  SA: 0x60, NAME: 0x0000000000000000, Time: 2024-05-16 20:23:40.00+02:00\n");
	printf("  If the NAME is known, it will have a non-zero value.\n");
	printf("\n");
	printf("Usage Examples:\n");
	printf("  j1939acd -r 64-95 -c /tmp/1122334455667788.jacd 1122334455667788 vcan0 &\n");
	printf("\n");
	printf("  Broadcast mode:\n");
	printf("    j1939-timedate-cli -i vcan0 -a 0x80\n");
	printf("\n");
	printf("  Unicast mode:\n");
	printf("    j1939-timedate-cli -i vcan0 -a 0x80 -r 0x90\n");
	printf("\n");
	printf("  Using NAMEs instead of addresses:\n");
	printf("    j1939acd -r 64-95 -c /tmp/1122334455667788.jacd 1122334455667788 vcan0 &\n");
	printf("    j1939-timedate-cli -i vcan0 -n 0x1122334455667788\n");
}

static int j1939_timedate_cli_parse_args(struct j1939_timedate_cli_priv *priv, int argc, char *argv[])
{
	struct sockaddr_can *remote = &priv->peername;
	struct sockaddr_can *local = &priv->sockname;
	bool local_address_set = false;
	bool local_name_set = false;
	bool remote_address_set = false;
	bool remote_name_set = false;
	bool interface_set = false;
	int long_index = 0;
	int opt;

	static struct option long_options[] = {
		{"interface", required_argument, 0, 'i'},
		{"local-address", required_argument, 0, 'a'},
		{"local-name", required_argument, 0, 'n'},
		{"remote-address", required_argument, 0, 'r'},
		{"remote-name", required_argument, 0, 'm'},
		{"utc", no_argument, 0, 'u'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "a:n:r:m:i:u", long_options, &long_index)) != -1) {
		switch (opt) {
		case 'a':
			local->can_addr.j1939.addr = strtoul(optarg, NULL, 16);
			local_address_set = true;
			break;
		case 'n':
			local->can_addr.j1939.name = strtoull(optarg, NULL, 16);
			local_name_set = true;
			break;
		case 'r':
			remote->can_addr.j1939.addr = strtoul(optarg, NULL, 16);
			remote_address_set = true;
			break;
		case 'm':
			remote->can_addr.j1939.name = strtoull(optarg, NULL, 16);
			remote_name_set = true;
			break;
		case 'i':
			local->can_ifindex = if_nametoindex(optarg);
			if (!local->can_ifindex) {
				pr_err("Interface %s not found. Error: %d (%s)\n",
				       optarg, errno, strerror(errno));
				return -EINVAL;
			}
			remote->can_ifindex = local->can_ifindex;
			interface_set = true;
			break;
		case 'u':
			priv->utc = true;
			break;
		default:
			j1939_timedate_cli_print_help();
			return -EINVAL;
		}
	}

	if (!interface_set) {
		pr_err("interface not specified");
		j1939_timedate_cli_print_help();
		return -EINVAL;
	}

	if ((local_address_set && local_name_set) ||
	    (remote_address_set && remote_name_set)) {
		pr_err("Local address and local name or remote address and remote name are mutually exclusive\n");
		j1939_timedate_cli_print_help();
		return -EINVAL;
	}

	if (!local_address_set && !local_name_set) {
		pr_err("Local address and local name not specified. One of them is required\n");
		j1939_timedate_cli_print_help();
		return -EINVAL;
	}

	/* If no remote address is set, set it to broadcast */
	if (!remote_address_set && !remote_name_set) {
		remote->can_addr.j1939.addr = J1939_NO_ADDR;
		priv->broadcast = true;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct j1939_timedate_cli_priv *priv;
	struct timespec ts;
	int64_t time_diff;
	int ret;

	priv = malloc(sizeof(*priv));
	if (!priv)
		err(EXIT_FAILURE, "can't allocate priv");

	bzero(priv, sizeof(*priv));

	libj1939_init_sockaddr_can(&priv->sockname, J1939_PGN_TD);
	libj1939_init_sockaddr_can(&priv->peername, J1939_PGN_REQUEST_PGN);

	ret = j1939_timedate_cli_parse_args(priv, argc, argv);
	if (ret)
		return ret;

	ret = j1939_timedate_cli_sock_prepare(priv);
	if (ret)
		return ret;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	priv->cmn.next_send_time = ts;
	priv->wait_until_time = priv->cmn.next_send_time;
	/* Wait one second to collect all responses by default */
	timespec_add_ms(&priv->wait_until_time, 1000);

	ret = j1939_timedate_cli_send_req(priv);
	if (ret)
		return ret;

	while (1) {
		ret = j1939_timedate_cli_process_events_and_tasks(priv);
		if (ret)
			break;

		if (priv->done)
			break;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		time_diff = timespec_diff_ms(&priv->wait_until_time, &ts);
		if (time_diff < 0)
			break;
	}

	close(priv->cmn.epoll_fd);
	free(priv->cmn.epoll_events);

	close(priv->sock_main);
	close(priv->sock_nack);

	return ret;
}

