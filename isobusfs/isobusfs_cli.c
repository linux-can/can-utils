// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "isobusfs_cli.h"

/* Maximal number of events that can be registered. The number is
 * based on feeling not on any real data.
 */
#define ISOBUSFS_CLI_MAX_EVENTS 10

int isobusfs_cli_register_event(struct isobusfs_priv *priv,
				const struct isobusfs_event *new_event)
{
	if (!priv->events) {
		priv->events = malloc(sizeof(*new_event) *
				      ISOBUSFS_CLI_MAX_EVENTS);
		if (!priv->events)
			return -ENOMEM;

		priv->max_events = ISOBUSFS_CLI_MAX_EVENTS;
	} else if (priv->num_events >= priv->max_events) {
		return -1;
	}

	memcpy(&priv->events[priv->num_events], new_event,
	       sizeof(*new_event));
	priv->num_events++;

	return 0;
}

int isobusfs_cli_remove_event(struct isobusfs_priv *priv,
			      struct isobusfs_event *event_to_remove)
{
	size_t i;

	for (i = 0; i < priv->num_events; ++i) {
		if (&priv->events[i] == event_to_remove) {
			memmove(&priv->events[i], &priv->events[i + 1],
				(priv->num_events - i - 1) *
				sizeof(*priv->events));
			priv->num_events--;

			return 0;
		}
	}

	return -ENOENT;
}

struct timespec ms_to_timespec(unsigned int timeout_ms)
{
	struct timespec ts;

	ts.tv_sec = timeout_ms / 1000;
	ts.tv_nsec = (timeout_ms % 1000) * 1000000;

	return ts;
}

void isobusfs_cli_prepare_response_event(struct isobusfs_event *event, int sock,
					 uint8_t fs_function)
{
	struct timespec current_time, timeout_timespec;

	event->fd = sock;
	event->fs_function = fs_function;

	/* Calculate the timeout */
	clock_gettime(CLOCK_REALTIME, &current_time);
	timeout_timespec = ms_to_timespec(ISOBUSFS_CLI_DEFAULT_WAIT_TIMEOUT_MS);
	event->timeout.tv_sec = current_time.tv_sec + timeout_timespec.tv_sec;
	event->timeout.tv_nsec = current_time.tv_nsec + timeout_timespec.tv_nsec;

	/* Adjust for nanosecond overflow */
	if (event->timeout.tv_nsec >= 1000000000) {
		event->timeout.tv_nsec -= 1000000000;
		event->timeout.tv_sec++;
	}

	event->one_shot = true;
}

static bool isobusfs_cli_has_event_expired(const struct timespec *timeout)
{
	struct timespec now;

	clock_gettime(CLOCK_REALTIME, &now);
	return now.tv_sec > timeout->tv_sec ||
		   (now.tv_sec == timeout->tv_sec &&
		    now.tv_nsec > timeout->tv_nsec);
}

static void isobusfs_cli_process_expired_events(struct isobusfs_priv *priv)
{
	for (size_t i = 0; i < priv->num_events; ++i) {
		struct isobusfs_event *event = &priv->events[i];

		if (isobusfs_cli_has_event_expired(&event->timeout)) {
			event->cb(priv, NULL, event->ctx, -ETIME);

			isobusfs_cli_remove_event(priv, event);

			i--;
		}
	}
}

static int isobusfs_cli_rx_event(struct isobusfs_priv *priv, int sock,
				 struct isobusfs_msg *msg, bool *found)
{
	struct isobusfs_event *event = NULL;
	int ret = 0;

	*found = false;

	for (size_t i = 0; i < priv->num_events; ++i) {
		event = &priv->events[i];

		if (event->fd == sock && event->fs_function == msg->buf[0]) {
			if (event->cb)
				ret = event->cb(priv, msg, event->ctx, 0);

			*found = true;
			break;
		}
	}

	if (*found && event->one_shot)
		isobusfs_cli_remove_event(priv, event);

	return ret;
}

static int isobusfs_cli_rx(struct isobusfs_priv *priv, struct isobusfs_msg *msg)
{
	int cmd = isobusfs_buf_to_cmd(msg->buf);
	int ret = 0;

	switch (cmd) {
	case ISOBUSFS_CG_CONNECTION_MANAGMENT:
		ret = isobusfs_cli_rx_cg_cm(priv, msg);
		break;
	case ISOBUSFS_CG_DIRECTORY_HANDLING:
		ret = isobusfs_cli_rx_cg_dh(priv, msg);
		break;
	case ISOBUSFS_CG_FILE_ACCESS: /* fall through */
		ret = isobusfs_cli_rx_cg_fa(priv, msg);
		break;
	case ISOBUSFS_CG_FILE_HANDLING: /* fall through */
	case ISOBUSFS_CG_VOLUME_HANDLING: /* fall through */
	default:
		isobusfs_send_nack(priv->sock_nack, msg);
		pr_warn("unsupported command group: %i", cmd);
		/* Not a critical error */
		return 0;
	}

	return ret;
}

static int isobusfs_cli_rx_ack(struct isobusfs_priv *priv, struct isobusfs_msg *msg)
{
	enum isobusfs_ack_ctrl ctrl = msg->buf[0];

	switch (ctrl) {
	case ISOBUS_ACK_CTRL_ACK:
		/* received ACK unexpectedly an ACK, no idea what to do */
		pr_debug("< rx: ACK?????");
		break;
	case ISOBUS_ACK_CTRL_NACK:
		/* we did something wrong */
		pr_debug("< rx: NACK!!!!!!");
		/* try to provide some usable information with a trace of
		 * the TX history
		 */
		isobusfs_dump_tx_data(&priv->tx_buf_log);
		priv->state = ISOBUSFS_CLI_STATE_IDLE;
		break;
	default:
		pr_warn("%s: unsupported ACK control: %i", __func__, ctrl);
	}

	/* Not a critical error */
	return 0;
}

static int isobusfs_cli_rx_buf(struct isobusfs_priv *priv, struct isobusfs_msg *msg)
{
	pgn_t pgn = msg->peername.can_addr.j1939.pgn;
	int ret;

	switch (pgn) {
	case ISOBUSFS_PGN_FS_TO_CL:
		ret = isobusfs_cli_rx(priv, msg);
		break;
	case ISOBUS_PGN_ACK:
		ret = isobusfs_cli_rx_ack(priv, msg);
		break;
	default:
		pr_warn("%s: unsupported PGN: %x", __func__, pgn);
		/* Not a critical error */
		ret = 0;
		break;
	}

	return ret;
}

static int isobusfs_cli_rx_one(struct isobusfs_priv *priv, int sock)
{
	struct isobusfs_msg *msg;
	int flags = 0;
	bool found;
	int ret;

	msg = malloc(sizeof(*msg));
	if (!msg) {
		pr_err("can't allocate rx msg struct\n");
		return -ENOMEM;
	}
	msg->buf_size = ISOBUSFS_MAX_TRANSFER_LENGH;
	msg->peer_addr_len = sizeof(msg->peername);
	msg->sock = sock;

	ret = recvfrom(sock, &msg->buf[0], msg->buf_size, flags,
		       (struct sockaddr *)&msg->peername, &msg->peer_addr_len);

	if (ret < 0) {
		ret = -errno;
		pr_warn("recvfrom() failed: %i %s", ret, strerror(-ret));
		return ret;
	}

	if (ret < ISOBUSFS_MIN_TRANSFER_LENGH) {
		pr_warn("buf is less then min transfer: %i\n", ret);
		isobusfs_send_nack(priv->sock_nack, msg);
		return -EINVAL;
	}

	msg->len = ret;

	ret = isobusfs_cli_rx_event(priv, sock, msg, &found);
	if (ret < 0) {
		pr_warn("failed to process rx event: %i (%s)\n", ret, strerror(ret));
		return ret;
	} else if (found)
		return 0;

	ret = isobusfs_cli_rx_buf(priv, msg);
	if (ret < 0) {
		pr_warn("failed to process rx buf: %i (%s)\n", ret, strerror(ret));
		return ret;
	}

	return 0;
}

static int isobusfs_cli_handle_events(struct isobusfs_priv *priv, int nfds)
{
	int ret;
	int n;

	for (n = 0; n < nfds && n < priv->cmn.epoll_events_size; ++n) {
		struct epoll_event *ev = &priv->cmn.epoll_events[n];

		if (!ev->events) {
			warn("no events");
			continue;
		}

		if (ev->data.fd == priv->sock_ccm) {
			if (ev->events & POLLERR) {
				struct isobusfs_err_msg emsg = {
					.stats = &priv->stats,
				};

				ret = isobusfs_recv_err(priv->sock_ccm, &emsg);
				if (ret && ret != -EINTR)
					return ret;
			}
		} else if (ev->data.fd == STDIN_FILENO) {
			if (!priv->interactive) {
				warn("got POLLIN on stdin, but interactive mode is disabled");
				continue;
			}
			if (ev->events & POLLIN) {
				ret = isobusfs_cli_interactive(priv);
				if (ret)
					return ret;
			} else
				warn("got not POLLIN on stdin");
		} else if (ev->events & POLLIN) {
			ret = isobusfs_cli_rx_one(priv, ev->data.fd);
			if (ret) {
				warn("recv one");
				return ret;
			}
		}
	}

	return 0;
}

static int isobusfs_cli_handle_periodic_tasks(struct isobusfs_priv *priv)
{
	/* detect FS timeout */
	isobusfs_cli_fs_detect_timeout(priv);

	isobusfs_cli_run_self_tests(priv);

	isobusfs_cli_process_expired_events(priv);

	/* this function will send status only if it is proper time to do so */
	return isobusfs_cli_ccm_send(priv);
}

int isobusfs_cli_process_events_and_tasks(struct isobusfs_priv *priv)
{
	bool dont_wait = false;
	int nfds = 0;
	int ret;

	if (priv->state == ISOBUSFS_CLI_STATE_SELFTEST)
		dont_wait = true;

	ret = isobusfs_cmn_prepare_for_events(&priv->cmn, &nfds, dont_wait);
	if (ret)
		return ret;

	if (nfds > 0) {
		ret = isobusfs_cli_handle_events(priv, nfds);
		if (ret)
			return ret;
	}

	return isobusfs_cli_handle_periodic_tasks(priv);
}

static int isobusfs_cli_sock_main_prepare(struct isobusfs_priv *priv)
{
	struct sockaddr_can addr = priv->sockname;
	int ret;

	ret = isobusfs_cmn_open_socket();
	if (ret < 0)
		return ret;

	priv->sock_main = ret;

	/* TODO: this is TX only socket */
	addr.can_addr.j1939.pgn = ISOBUSFS_PGN_FS_TO_CL;
	ret = isobusfs_cmn_bind_socket(priv->sock_main, &addr);
	if (ret < 0)
		return ret;

	ret = isobusfs_cmn_set_linger(priv->sock_main);
	if (ret < 0)
		return ret;

	ret = isobusfs_cmn_socket_prio(priv->sock_main, ISOBUSFS_PRIO_DEFAULT);
	if (ret < 0)
		return ret;

	ret = isobusfs_cmn_connect_socket(priv->sock_main, &priv->peername);
	if (ret < 0)
		return ret;

	return isobusfs_cmn_add_socket_to_epoll(priv->cmn.epoll_fd,
						priv->sock_main, EPOLLIN);
}

/* isobusfs_cli_sock_int_prepare() is used to prepare stdin for interactive
 * mode.
 */
static int isobusfs_cli_sock_int_prepare(struct isobusfs_priv *priv)
{
	int ret;

	if (!priv->interactive)
		return 0;

	isobusfs_set_interactive(true);

	ret = fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
	if (ret < 0)
		return ret;

	return isobusfs_cmn_add_socket_to_epoll(priv->cmn.epoll_fd,
						STDIN_FILENO, EPOLLIN);
}

static int isobusfs_cli_sock_ccm_prepare(struct isobusfs_priv *priv)
{
	struct sockaddr_can addr = priv->sockname;
	int ret;

	ret = isobusfs_cmn_open_socket();
	if (ret < 0)
		return ret;

	priv->sock_ccm = ret;

	ret = isobusfs_cmn_configure_error_queue(priv->sock_ccm);
	if (ret < 0)
		return ret;

	/* TODO: this is TX only socket */
	addr.can_addr.j1939.pgn = J1939_NO_PGN;
	ret = isobusfs_cmn_bind_socket(priv->sock_ccm, &addr);
	if (ret < 0)
		return ret;

	ret = isobusfs_cmn_set_linger(priv->sock_ccm);
	if (ret < 0)
		return ret;

	ret = isobusfs_cmn_socket_prio(priv->sock_ccm, ISOBUSFS_PRIO_DEFAULT);
	if (ret < 0)
		return ret;

	ret = isobusfs_cmn_connect_socket(priv->sock_ccm, &priv->peername);
	if (ret < 0)
		return ret;

	/* poll for errors to get confirmation if our packets are send */
	return isobusfs_cmn_add_socket_to_epoll(priv->cmn.epoll_fd, priv->sock_ccm,
						EPOLLERR);
}

static int isobusfs_cli_sock_nack_prepare(struct isobusfs_priv *priv)
{
	struct sockaddr_can addr = priv->sockname;
	int ret;

	ret = isobusfs_cmn_open_socket();
	if (ret < 0)
		return ret;

	priv->sock_nack = ret;

	addr.can_addr.j1939.pgn = ISOBUS_PGN_ACK;
	ret = isobusfs_cmn_bind_socket(priv->sock_nack, &addr);
	if (ret < 0)
		return ret;

	ret = isobusfs_cmn_socket_prio(priv->sock_nack, ISOBUSFS_PRIO_ACK);
	if (ret < 0)
		return ret;

	/* poll for errors to get confirmation if our packets are send */
	return isobusfs_cmn_add_socket_to_epoll(priv->cmn.epoll_fd,
						priv->sock_nack, EPOLLIN);
}

/* rx socket for fss and volume status announcements */
static int isobusfs_cli_sock_bcast_prepare(struct isobusfs_priv *priv)
{
	struct sockaddr_can addr = priv->sockname;
	int ret;

	ret = isobusfs_cmn_open_socket();
	if (ret < 0)
		return ret;

	priv->sock_bcast_rx = ret;

	/* keep address and name and overwrite PGN */
	addr.can_addr.j1939.name = J1939_NO_NAME;
	addr.can_addr.j1939.addr = J1939_NO_ADDR;
	addr.can_addr.j1939.pgn = ISOBUSFS_PGN_FS_TO_CL;
	ret = isobusfs_cmn_bind_socket(priv->sock_bcast_rx, &addr);
	if (ret < 0)
		return ret;

	ret = isobusfs_cmn_set_broadcast(priv->sock_bcast_rx);
	if (ret < 0)
		return ret;

	ret = isobusfs_cmn_connect_socket(priv->sock_bcast_rx, &priv->peername);
	if (ret < 0)
		return ret;

	return isobusfs_cmn_add_socket_to_epoll(priv->cmn.epoll_fd, priv->sock_bcast_rx,
						EPOLLIN);
}

static int isobusfs_cli_sock_prepare(struct isobusfs_priv *priv)
{
	int ret;

	ret = isobusfs_cmn_create_epoll();
	if (ret < 0)
		return ret;

	priv->cmn.epoll_fd = ret;

	priv->cmn.epoll_events = calloc(ISOBUSFS_CLI_MAX_EPOLL_EVENTS,
					sizeof(struct epoll_event));
	if (!priv->cmn.epoll_events)
		return -ENOMEM;

	priv->cmn.epoll_events_size = ISOBUSFS_CLI_MAX_EPOLL_EVENTS;

	ret = isobusfs_cli_sock_int_prepare(priv);
	if (ret < 0)
		return ret;

	ret = isobusfs_cli_sock_ccm_prepare(priv);
	if (ret < 0)
		return ret;

	ret = isobusfs_cli_sock_bcast_prepare(priv);
	if (ret < 0)
		return ret;

	ret = isobusfs_cli_sock_main_prepare(priv);
	if (ret < 0)
		return ret;

	return isobusfs_cli_sock_nack_prepare(priv);
}

static void isobusfs_cli_print_help(void)
{
	printf("Usage: isobusfs-cli [options]\n");
	printf("Options:\n");
	printf("  --interactive or -I (Default)\n");
	printf("  --interface <interface_name> or -i <interface_name>\n");
	printf("  --local-address <local_address_hex> or -a <local_address_hex>\n");
	printf("  --local-name <local_name_hex> or -n <local_name_hex>\n");
	printf("  --log-level <logging_level> or -l <logging_level> (Default %d)\n",
	       LOG_LEVEL_INFO);
	printf("  --remote-address <remote_address_hex> or -r <remote_address_hex>\n");
	printf("  --remote-name <remote_name_hex> or -m <remote_name_hex>\n");
	printf("Note: Local address and local name are mutually exclusive\n");
	printf("Note: Remote address and remote name are mutually exclusive\n");
}

static int isobusfs_cli_parse_args(struct isobusfs_priv *priv, int argc, char *argv[])
{
	struct sockaddr_can *remote = &priv->peername;
	struct sockaddr_can *local = &priv->sockname;
	bool local_address_set = false;
	bool local_name_set = false;
	bool remote_address_set = false;
	bool remote_name_set = false;
	bool interface_set = false;
	int long_index = 0;
	int level;
	int opt;

	static struct option long_options[] = {
		{"interface", required_argument, 0, 'i'},
		{"interactive", no_argument, 0, 'I'},
		{"local-address", required_argument, 0, 'a'},
		{"local-name", required_argument, 0, 'n'},
		{"log-level", required_argument, 0, 'l'},
		{"remote-address", required_argument, 0, 'r'},
		{"remote-name", required_argument, 0, 'm'},
		{0, 0, 0, 0}
	};

	/* active by default */
	priv->interactive = true;

	while ((opt = getopt_long(argc, argv, "a:n:r:m:Ii:l:", long_options, &long_index)) != -1) {
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
		case 'I':
			priv->interactive = true;
			break;
		case 'l':
			level = strtoul(optarg, NULL, 0);
			if (level < LOG_LEVEL_ERROR || level > LOG_LEVEL_DEBUG)
				pr_err("invalid debug level %d", level);
			isobusfs_log_level_set(level);
			break;
		default:
			isobusfs_cli_print_help();
			return -EINVAL;
		}
	}

	if (!interface_set) {
		pr_err("interface not specified");
		isobusfs_cli_print_help();
		return -EINVAL;
	}

	if ((local_address_set && local_name_set) ||
	    (remote_address_set && remote_name_set)) {
		pr_err("local address and local name or remote address and remote name are mutually exclusive");
		isobusfs_cli_print_help();
		return -EINVAL;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct isobusfs_priv *priv;
	struct timespec ts;
	int ret;

	priv = malloc(sizeof(*priv));
	if (!priv)
		err(EXIT_FAILURE, "can't allocate priv");

	bzero(priv, sizeof(*priv));

	isobusfs_init_sockaddr_can(&priv->sockname, J1939_NO_PGN);
	isobusfs_init_sockaddr_can(&priv->peername, ISOBUSFS_PGN_CL_TO_FS);

	ret = isobusfs_cli_parse_args(priv, argc, argv);
	if (ret)
		return ret;

	ret = isobusfs_cli_sock_prepare(priv);
	if (ret)
		return ret;

	isobusfs_cli_ccm_init(priv);

	/* Init next st_next_send_time value to avoid warnings */
	clock_gettime(CLOCK_MONOTONIC, &ts);
	priv->cmn.next_send_time = ts;

	if (priv->interactive)
		isobusfs_cli_int_start(priv);
	else
		pr_debug("starting client\n");

	while (1) {
		ret = isobusfs_cli_process_events_and_tasks(priv);
		if (ret)
			break;
	}

	close(priv->cmn.epoll_fd);
	free(priv->cmn.epoll_events);

	close(priv->sock_main);
	close(priv->sock_nack);
	close(priv->sock_ccm);
	close(priv->sock_bcast_rx);

	return ret;
}

