// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#include <net/if.h>

#include <linux/net_tstamp.h>

#include "isobusfs_srv.h"

int isobusfs_srv_sendto(struct isobusfs_srv_priv *priv,
			struct isobusfs_msg *msg, const void *buf,
			size_t buf_size)
{
	struct sockaddr_can addr = msg->peername;

	addr.can_addr.j1939.pgn = ISOBUSFS_PGN_FS_TO_CL;
	return isobusfs_sendto(msg->sock, buf, buf_size, &addr,
			       &priv->tx_buf_log);
}

int isobusfs_srv_send_error(struct isobusfs_srv_priv *priv,
			    struct isobusfs_msg *msg,
			    enum isobusfs_error err)
{
	uint8_t buf[ISOBUSFS_MIN_TRANSFER_LENGH];

	/* copy 2 bytes with command group, function and TAN from the source
	 * package
	 */
	memcpy(buf, &msg->buf[0], 2);
	buf[2] = err;

	/* not used space should be filled with 0xff */
	memset(&buf[3], 0xff, ARRAY_SIZE(buf) - 3);

	pr_debug("> tx error: 0x%02x (%s)", err, isobusfs_error_to_str(err));

	return isobusfs_srv_sendto(priv, msg, &buf[0], ARRAY_SIZE(buf));
}

/* server side rx */
static int isobusfs_srv_rx_fs(struct isobusfs_srv_priv *priv,
			      struct isobusfs_msg *msg)
{
	enum isobusfs_cg cg = isobusfs_buf_to_cmd(msg->buf);
	uint8_t addr = msg->peername.can_addr.j1939.addr;
	struct isobusfs_srv_client *client;
	int ret = 0;

	switch (cg) {
	case ISOBUSFS_CG_CONNECTION_MANAGMENT:
	case ISOBUSFS_CG_DIRECTORY_HANDLING:
	case ISOBUSFS_CG_FILE_ACCESS:
	case ISOBUSFS_CG_FILE_HANDLING:
	case ISOBUSFS_CG_VOLUME_HANDLING:
		break;
	default:
		pr_warn("%s: unsupported command group (%i)", __func__,
		      cg);

		/* ISO 11783-13:2021 - Annex C.1.1 Overview:
		 * If a client sends a command, which is not defined within this
		 * documentation, the file server shall respond with a
		 * NACK (ISO 11783-3:2018 Chapter 5.4.5)
		 */
		isobusfs_send_nack(priv->sock_nack, msg);

		/* Not a critical error */
		return 0;
	}

	client = isobusfs_srv_get_client(priv, addr);
	if (!client) {
		pr_warn("%s: client not found", __func__);
		return -EINVAL;
	}

	msg->sock = client->sock;

	switch (cg) {
	case ISOBUSFS_CG_CONNECTION_MANAGMENT:
		ret = isobusfs_srv_rx_cg_cm(priv, msg);
		break;
	case ISOBUSFS_CG_DIRECTORY_HANDLING:
		ret = isobusfs_srv_rx_cg_dh(priv, msg);
		break;
	case ISOBUSFS_CG_FILE_ACCESS:
		ret = isobusfs_srv_rx_cg_fa(priv, msg);
		break;
	case ISOBUSFS_CG_FILE_HANDLING:
		ret = isobusfs_srv_rx_cg_fh(priv, msg);
		break;
	case ISOBUSFS_CG_VOLUME_HANDLING:
		ret = isobusfs_srv_rx_cg_vh(priv, msg);
		break;
	}

	return ret;
}

static int isobusfs_srv_rx_ack(struct isobusfs_srv_priv *priv,
			       struct isobusfs_msg *msg)
{
	enum isobusfs_ack_ctrl ctrl = msg->buf[0];

	switch (ctrl) {
	case ISOBUS_ACK_CTRL_ACK:
		pr_debug("< rx: ACK?????");
		break;
	case ISOBUS_ACK_CTRL_NACK:
		/* we did something wrong */
		pr_debug("< rx: NACK!!!!!");
		isobusfs_dump_tx_data(&priv->tx_buf_log);
		break;
	default:
		pr_warn("%s: unsupported ACK control: %i", __func__, ctrl);
		return -EINVAL;
	}

	/* Not a critical error */
	return 0;
}

static int isobusfs_srv_rx_buf(struct isobusfs_srv_priv *priv, struct isobusfs_msg *msg)
{
	pgn_t pgn = msg->peername.can_addr.j1939.pgn;
	int ret;

	switch (pgn) {
	case ISOBUSFS_PGN_CL_TO_FS:
		ret = isobusfs_srv_rx_fs(priv, msg);
		break;
	case ISOBUS_PGN_ACK:
		ret = isobusfs_srv_rx_ack(priv, msg);
		break;
	default:
		pr_warn("%s: unsupported PGN: %i", __func__, pgn);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int isobusfs_srv_recv_one(struct isobusfs_srv_priv *priv, int sock)
{
	struct isobusfs_msg *msg;
	int flags = 0;
	int ret;

	msg = malloc(sizeof(*msg));
	if (!msg) {
		pr_err("can't allocate rx msg struct");
		goto done;
	}
	msg->buf_size = ISOBUSFS_MAX_TRANSFER_LENGH;
	msg->peer_addr_len = sizeof(msg->peername);
	msg->sock = sock;

	ret = recvfrom(sock, &msg->buf[0], msg->buf_size, flags,
		       (struct sockaddr *)&msg->peername, &msg->peer_addr_len);
	if (ret < 0) {
		pr_err("recvfrom(): %i (%s)", errno, strerror(errno));
		goto free_msg;
	}

	if (ret < ISOBUSFS_MIN_TRANSFER_LENGH) {
		pr_warn("buf is less then min transfer: %i < %i. Dropping.",
			ret, ISOBUSFS_MIN_TRANSFER_LENGH);

		/* TODO: The file server shall respond with Error Code 47
		 * Malformed Request, if the message is shorter than expected.
		 */
		isobusfs_send_nack(priv->sock_nack, msg);

		goto free_msg;
	}

	msg->len = ret;

	ret = isobusfs_srv_rx_buf(priv, msg);
	if (ret < 0) {
		pr_err("unhandled error by rx buf: %i", ret);
		goto free_msg;
	}

free_msg:
	free(msg);
done:
	return EXIT_SUCCESS;
}

static int isobusfs_srv_handle_events(struct isobusfs_srv_priv *priv, unsigned int nfds)
{
	int ret;
	unsigned int n;

	for (n = 0; n < nfds && n < priv->cmn.epoll_events_size; ++n) {
		struct epoll_event *ev = &priv->cmn.epoll_events[n];

		if (!ev->events) {
			warn("no events");
			continue;
		}

		if (ev->data.fd == priv->sock_fss) {
			if (ev->events & POLLERR) {
				struct isobusfs_err_msg emsg = {
					.stats = &priv->st_msg_stats,
				};

				ret = isobusfs_recv_err(priv->sock_fss, &emsg);
				if (ret)
					pr_warn("error queue reported error: %i", ret);
			}
		}

		if (ev->events & POLLIN) {
			ret = isobusfs_srv_recv_one(priv, ev->data.fd);
			if (ret) {
				warn("recv one");
				return ret;
			}
		}
	}
	return 0;
}

static int isobusfs_srv_handle_periodic_tasks(struct isobusfs_srv_priv *priv)
{
	/* remove timeouted clients */
	isobusfs_srv_remove_timeouted_clients(priv);

	/* this function will send status only if it is proper time to do so */
	return isobusfs_srv_fss_send(priv);
}

static int isobusfs_srv_process_events_and_tasks(struct isobusfs_srv_priv *priv)
{
	int ret, nfds;

	ret = libj1939_prepare_for_events(&priv->cmn, &nfds, false);
	if (ret)
		return ret;

	if (nfds > 0) {
		ret = isobusfs_srv_handle_events(priv, nfds);
		if (ret)
			return ret;
	}

	return isobusfs_srv_handle_periodic_tasks(priv);
}

static int isobusfs_srv_sock_fss_prepare(struct isobusfs_srv_priv *priv)
{
	struct sockaddr_can addr = priv->addr;
	int ret;

	ret = libj1939_open_socket();
	if (ret < 0)
		return ret;

	priv->sock_fss = ret;

	ret = isobusfs_cmn_configure_error_queue(priv->sock_fss);
	if (ret < 0)
		return ret;

	/* keep address and name and overwrite PGN */
	/* TODO: actually, this is PGN input filter. Should we use different
	 * PGN?
	 */
	addr.can_addr.j1939.pgn = ISOBUSFS_PGN_CL_TO_FS;
	ret = libj1939_bind_socket(priv->sock_fss, &addr);
	if (ret < 0)
		return ret;

	ret = libj1939_set_broadcast(priv->sock_fss);
	if (ret < 0)
		return ret;

	ret = isobusfs_cmn_set_linger(priv->sock_fss);
	if (ret < 0)
		return ret;

	ret = libj1939_socket_prio(priv->sock_fss, ISOBUSFS_PRIO_FSS);
	if (ret < 0)
		return ret;

	/* connect to broadcast address */
	addr.can_addr.j1939.name = J1939_NO_NAME;
	addr.can_addr.j1939.addr = J1939_NO_ADDR;
	addr.can_addr.j1939.pgn = ISOBUSFS_PGN_FS_TO_CL;
	ret = isobusfs_cmn_connect_socket(priv->sock_fss, &addr);
	if (ret < 0)
		return ret;

	/* poll for errors to get confirmation if our packets are send */
	return libj1939_add_socket_to_epoll(priv->cmn.epoll_fd, priv->sock_fss,
						EPOLLERR);
}

static int isobusfs_srv_sock_in_prepare(struct isobusfs_srv_priv *priv)
{
	struct sockaddr_can addr = priv->addr;
	int ret;

	ret = libj1939_open_socket();
	if (ret < 0)
		return ret;

	priv->sock_in = ret;

	/* keep address and name and overwrite PGN */
	addr.can_addr.j1939.pgn = ISOBUSFS_PGN_CL_TO_FS;
	ret = libj1939_bind_socket(priv->sock_in, &addr);
	if (ret < 0)
		return ret;

	return libj1939_add_socket_to_epoll(priv->cmn.epoll_fd, priv->sock_in,
					    EPOLLIN);
}

static int isobusfs_srv_sock_nack_prepare(struct isobusfs_srv_priv *priv)
{
	struct sockaddr_can addr = priv->addr;
	int ret;

	ret = libj1939_open_socket();
	if (ret < 0)
		return ret;

	priv->sock_nack = ret;

	addr.can_addr.j1939.pgn = ISOBUS_PGN_ACK;
	ret = libj1939_bind_socket(priv->sock_nack, &addr);
	if (ret < 0)
		return ret;

	ret = libj1939_socket_prio(priv->sock_nack, ISOBUSFS_PRIO_ACK);
	if (ret < 0)
		return ret;

	/* poll for errors to get confirmation if our packets are send */
	return libj1939_add_socket_to_epoll(priv->cmn.epoll_fd,
					    priv->sock_nack, EPOLLIN);
}

/**
 * isobusfs_srv_sock_prepare - Prepares the control socket and epoll instance
 * @priv: pointer to the isobusfs_srv_priv structure
 *
 * This function calls multiple helper functions to prepare the control socket
 * and epoll instance for the ISOBUS server.
 * Returns: 0 on success, negative error code on failure
 */
static int isobusfs_srv_sock_prepare(struct isobusfs_srv_priv *priv)
{
	int ret;

	ret = libj1939_create_epoll();
	if (ret < 0)
		return ret;

	priv->cmn.epoll_fd = ret;

	priv->cmn.epoll_events = calloc(ISOBUSFS_SRV_MAX_EPOLL_EVENTS,
					sizeof(struct epoll_event));
	if (!priv->cmn.epoll_events)
		return -ENOMEM;

	priv->cmn.epoll_events_size = ISOBUSFS_SRV_MAX_EPOLL_EVENTS;

	ret = isobusfs_srv_sock_fss_prepare(priv);
	if (ret < 0)
		return ret;

	ret = isobusfs_srv_sock_in_prepare(priv);
	if (ret < 0)
		return ret;

	return isobusfs_srv_sock_nack_prepare(priv);
}

static int isobusfs_srv_parse_volume_ext(struct isobusfs_srv_priv *priv,
					 const char *optarg,
					 char ***volumes, int *volumes_count)
{
	char *volume_info;
	char *token;

	if (*volumes_count >= ISOBUSFS_SRV_MAX_VOLUMES) {
		pr_err("Maximum number of volumes (%d) exceeded\n",
		       ISOBUSFS_SRV_MAX_VOLUMES);
		return -EINVAL;
	}

	volume_info = strdup(optarg);
	token = strtok(volume_info, ",");
	while (token) {
		*volumes_count += 1;
		*volumes = realloc(*volumes,
			      *volumes_count * sizeof(char *));
		*volumes[*volumes_count - 1] = strdup(token);
		token = strtok(NULL, ",");
	}

	return 0;
}

static int isobusfs_srv_parse_volumes(struct isobusfs_srv_priv *priv,
				      const char *optarg)
{
	struct isobusfs_srv_volume *volumes = priv->volumes;
	char *volume_info, *name, *path;
	size_t name_len, path_len;
	int ret;

	if (priv->volume_count >= ISOBUSFS_SRV_MAX_VOLUMES) {
		pr_err("Maximum number of volumes (%d) exceeded\n",
		       ISOBUSFS_SRV_MAX_VOLUMES);
		return -EINVAL;
	}

	volume_info = strdup(optarg);
	name = strtok(volume_info, ":");
	path = strtok(NULL, ":");

	if (!name || !path) {
		pr_err("Error: volume or path name is missing\n");
		ret = -EINVAL;
		goto free_volume_info;
	}

	name_len = strnlen(name, ISOBUSFS_SRV_MAX_VOLUME_NAME_LEN + 2);
	if (name_len > ISOBUSFS_SRV_MAX_VOLUME_NAME_LEN) {
		pr_err("Error: Volume name exceeds maximum length (%d)\n",
		       ISOBUSFS_SRV_MAX_VOLUME_NAME_LEN);
		ret = -EINVAL;
		goto free_volume_info;
	}

	path_len = strnlen(path, ISOBUSFS_SRV_MAX_PATH_LEN + 2);
	if (path_len > ISOBUSFS_SRV_MAX_PATH_LEN) {
		pr_err("Error: Path name exceeds maximum length (%d)\n",
		       ISOBUSFS_SRV_MAX_PATH_LEN);
		ret = -EINVAL;
		goto free_volume_info;
	}

	volumes[priv->volume_count].name = strdup(name);
	volumes[priv->volume_count].path = strdup(path);
	priv->volume_count++;

	ret = 0;
free_volume_info:
	free(volume_info);

	return ret;
}

static void isobusfs_srv_generate_mfs_dir_name(struct isobusfs_srv_priv *priv)
{
	uint16_t manufacturer_code = (priv->local_name >> 21) & 0x07FF;

	snprintf(&priv->mfs_dir[0], sizeof(priv->mfs_dir), "MCMC%04u",
		 manufacturer_code);
}

static void isobusfs_srv_print_help(void)
{

	printf("Usage: isobusfs-srv [options]\n");
	printf("Options:\n");
	printf("  --address <local_address_hex> or -a <local_address_hex>\n");
	printf("  --default-volume <volume_name> or -d <volume_name>\n");
	printf("  --interface <interface_name> or -i <interface_name>\n");
	printf("  --log-level <logging_level> or -l <loging_level>\n");
	printf("  --name <local_name_hex> or -n <local_name_hex>\n");
	printf("  --removable-volume <volume_name_1,volume_name_2,...> or -r <volume_name_1,volume_name_2,...>\n");
	printf("  --server-version <version_number> or -s <version_number>\n");
	printf("  --volume <volume_name>:<path> or -v <volume_name>:<path>\n");
	printf("  --writeable-volume <volume_name_1,volume_name_2,...> or -w <volume_name_1,volume_name_2,...>\n");
	printf("Note: Local address and local name are mutually exclusive\n");
}

static int isobusfs_srv_parse_args(struct isobusfs_srv_priv *priv, int argc,
				   char *argv[])
{
	struct sockaddr_can *addr = &priv->addr;
	char **removable_volumes = NULL;
	char **writeable_volumes = NULL;
	uint32_t local_address = 0;
	uint64_t local_name = 0;
	bool local_address_set = false;
	bool local_name_set = false;
	bool voluem_set = false;
	bool interface_set = false;
	int ret, level, version;
	int opt, i, j;
	int writeable_volumes_count = 0;
	int long_index = 0;

	struct option long_options[] = {
		{"address", required_argument, NULL, 'a'},
		{"default-volume", required_argument, NULL, 'd'},
		{"interface", required_argument, NULL, 'i'},
		{"log-level", required_argument, NULL, 'l'},
		{"name", required_argument, NULL, 'n'},
		{"removable-volume", required_argument, 0, 'r'},
		{"server-version", required_argument, 0, 's'},
		{"volume", required_argument, NULL, 'v'},
		{"writeable-volume", required_argument, 0, 'w'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	while ((opt = getopt_long(argc, argv, "a:d:i:l:n:r:s:v:w:h",
				  long_options, &long_index)) != -1) {
		switch (opt) {
		case 'a': {
			if (local_name_set) {
				pr_err("Both local address and local name provided, they are mutually exclusive\n");
				return -EINVAL;
			}
			sscanf(optarg, "%x", &local_address);
			addr->can_addr.j1939.addr = local_address;
			local_address_set = true;
			break;
		}
		case 'd': {
			priv->default_volume = strdup(optarg);
			break;
		}
		case 'i': {
			addr->can_ifindex = if_nametoindex(optarg);
			if (!addr->can_ifindex) {
				pr_err("Interface %s not found. Error: %d (%s)\n",
				       optarg, errno, strerror(errno));
				return -EINVAL;
			}
			interface_set = true;
			break;
		}
		case 'l': {
			level = strtoul(optarg, NULL, 0);
			if (level < LOG_LEVEL_ERROR || level > LOG_LEVEL_DEBUG)
				pr_err("invalid debug level %d", level);
			isobusfs_log_level_set(level);
			break;
		}
		case 'n': {
			if (local_address_set) {
				pr_err("Both local address and local name provided, they are mutually exclusive\n");
				return -EINVAL;
			}
			sscanf(optarg, "%" SCNx64, &local_name);
			priv->local_name = local_name;
			addr->can_addr.j1939.name = local_name;
			local_name_set = true;
			break;
		}
		case 'r': {
			ret = isobusfs_srv_parse_volume_ext(priv, optarg,
							   &removable_volumes,
							   &priv->removable_volumes_count);
			if (ret < 0)
				return ret;

			break;
		}
		case 's': {
			version = atoi(optarg);
			if (version < 0 || version > 255)
				pr_err("Invalid server version %d. Using default version: %d",
				       version, ISOBUSFS_SRV_VERSION);
			break;
		}
		case 'v': {
			ret = isobusfs_srv_parse_volumes(priv, optarg);
			if (ret < 0)
				return ret;
			voluem_set = true;
			break;
		}
		case 'w': {

			ret = isobusfs_srv_parse_volume_ext(priv, optarg,
							   &writeable_volumes,
							   &writeable_volumes_count);
			if (ret < 0)
				return ret;

			break;
		}
		case 'h':
		default:
			isobusfs_srv_print_help();
			return -EINVAL;
		}
	}

	if (!local_address_set && !local_name_set) {
		pr_err("Error: local address or local name is missing");
		isobusfs_srv_print_help();
		return -EINVAL;
	}

	if (!voluem_set) {
		pr_err("Error: volume is missing");
		isobusfs_srv_print_help();
		return -EINVAL;
	}

	if (!interface_set) {
		pr_err("Error: interface is missing");
		isobusfs_srv_print_help();
		return -EINVAL;
	}

	if (!priv->volume_count) {
		pr_err("Error: volume is missing");
		isobusfs_srv_print_help();
		return -EINVAL;
	}

	if (priv->volume_count == 1) {
		if (priv->default_volume) {
			pr_err("Error: default volume is not needed for single volume");
			isobusfs_srv_print_help();
			return -EINVAL;
		}

		priv->default_volume = strdup(priv->volumes[0].name);
	} else if (priv->volume_count > 1) {
		if (!priv->default_volume) {
			pr_err("Error: default volume is missing");
			isobusfs_srv_print_help();
			return -EINVAL;
		}
		/* Check if default volume is valid */
		for (i = 0; i < priv->volume_count; i++) {
			if (!strcmp(priv->default_volume, priv->volumes[i].name))
				break;
			if (i == priv->volume_count - 1) {
				pr_err("Error: default volume should be one of defined volumes");
				isobusfs_srv_print_help();
				return -EINVAL;
			}
		}
	}

	for (i = 0; i < priv->removable_volumes_count; i++) {
		bool found = false;

		for (j = 0; j < priv->volume_count; j++) {
			if (!strcmp(removable_volumes[i],
				    priv->volumes[j].name)) {
				priv->volumes[j].removable = true;
				found = true;
				break;
			}
		}
		if (!found) {
			pr_err("Error: removable volume %s is not defined",
			       removable_volumes[i]);
			isobusfs_srv_print_help();
			return -EINVAL;
		}
	}

	for (i = 0; i < writeable_volumes_count; i++) {
		bool found = false;

		for (j = 0; j < priv->volume_count; j++) {
			if (!strcmp(writeable_volumes[i],
				    priv->volumes[j].name)) {
				priv->volumes[j].writeable = true;
				found = true;
				break;
			}
		}
		if (!found) {
			pr_err("Error: writeable volume %s is not defined",
			       writeable_volumes[i]);
			isobusfs_srv_print_help();
			return -EINVAL;
		}
	}

	for (i = 0; i < priv->volume_count; i++) {
		ret = isobusfs_cmn_dh_validate_dir_path(priv->volumes[i].path,
							priv->volumes[i].writeable);
		if (ret < 0) {
			if (ret == -ENOTDIR)
				pr_err("Error: path %s is not a directory",
				       priv->volumes[i].path);
			else if (ret == -EACCES)
				pr_err("Error: can't access path %s, error %i (%s)",
				       priv->volumes[i].path, ret, strerror(ret));

			/* If volume is not removable, return error. Probably
			 * we will be able to detect it later.
			 */
			if (!priv->volumes[i].removable)
				return ret;
		}
	}

	if (!local_name_set)
		pr_warn("local name is not set. Won't be able to generate proper manufacturer-specific directory name. Falling mack to MCMC0000");
	isobusfs_srv_generate_mfs_dir_name(priv);

	pr_debug("Server configuration:");
	pr_debug("  local NAME: 0x%" SCNx64, priv->local_name);
	pr_debug("  manufacturer-specific directory: %s", priv->mfs_dir);
	pr_debug("Configured volumes:");
	for (i = 0; i < priv->volume_count; i++) {
		pr_debug("  %s: %s", priv->volumes[i].name,
			 priv->volumes[i].path);
		pr_debug("    %s", priv->volumes[i].writeable ? "writeable" : "read-only");
		pr_debug("    %s", priv->volumes[i].removable ? "removable" : "non-removable");
	}

	for (i = 0; i < priv->removable_volumes_count; i++)
		free(removable_volumes[i]);

	free(removable_volumes);

	for (i = 0; i < writeable_volumes_count; i++)
		free(writeable_volumes[i]);

	free(writeable_volumes);

	return 0;
}

int main(int argc, char *argv[])
{
	struct isobusfs_srv_priv *priv;
	struct timespec ts;
	int ret;

	/* Allocate memory for the private structure */
	priv = malloc(sizeof(*priv));
	if (!priv)
		err(EXIT_FAILURE, "can't allocate priv");

	/* Clear memory for the private structure */
	memset(priv, 0, sizeof(*priv));

	/* Initialize sockaddr_can with a non-configurable PGN */
	libj1939_init_sockaddr_can(&priv->addr, J1939_NO_PGN);

	priv->server_version = ISOBUSFS_SRV_VERSION;
	/* Parse command line arguments */
	ret = isobusfs_srv_parse_args(priv, argc, argv);
	if (ret)
		return ret;

	/* Prepare sockets for the server */
	ret = isobusfs_srv_sock_prepare(priv);
	if (ret)
		return ret;

	/* Initialize File Server Status structure */
	isobusfs_srv_fss_init(priv);
	/* Initialize client structures */
	isobusfs_srv_init_clients(priv);

	/* Init next st_next_send_time value to avoid warnings */
	clock_gettime(CLOCK_MONOTONIC, &ts);
	priv->cmn.next_send_time = ts;

	/* Start the isobusfsd server */
	pr_info("Starting isobusfs-srv");
	while (1) {
		ret = isobusfs_srv_process_events_and_tasks(priv);
		if (ret)
			break;
	}

	/* Close epoll and control sockets */
	close(priv->cmn.epoll_fd);
	free(priv->cmn.epoll_events);
	close(priv->sock_fss);
	close(priv->sock_in);
	close(priv->sock_nack);

	return ret;
}
