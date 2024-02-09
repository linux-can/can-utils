// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#ifndef ISOBUSFS_SRV_H
#define ISOBUSFS_SRV_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/epoll.h>
#include <dirent.h>

#include "isobusfs_cmn.h"
#include "isobusfs_cmn_cm.h"

#define ISOBUSFS_SRV_VERSION			4
#define ISOBUSFS_SRV_MAX_CTRL_SOCKETS		1
#define ISOBUSFS_SRV_MAX_CLIENT_SOCKETS		255
#define ISOBUSFS_SRV_MAX_EPOLL_EVENTS		(ISOBUSFS_SRV_MAX_CTRL_SOCKETS + \
						 ISOBUSFS_SRV_MAX_CLIENT_SOCKETS)
#define ISOBUSFS_SRV_MAX_OPENED_HANDLES		255
/*
 * ISO 11783-13:2021 standard does not explicitly specify a maximum number of
 * clients that can be supported on the network. However, the ISO 11783 standard
 * is built on top of the SAE J1939 protocol, which has a maximum of 238
 * available addresses for nodes. This number is calculated from the available
 * address range for assignment to nodes on the network, which includes 127
 * addresses in the range 1-127 and 111 addresses in the range 248-254,
 * inclusive. Some addresses in the total range (0-255) are reserved for
 * specific purposes, such as broadcast messages and null addresses.
 *
 * The maximum number of 238 nodes includes both clients and servers, so the
 * actual number of clients that can be supported will be less than 238.
 *
 * It is important to note that the practical limit of clients in an ISO
 * 11783-13 network could be lower due to factors such as network bandwidth,
 * performance constraints of the individual devices, and the complexity of the
 * network.
 */
#define ISOBUSFS_SRV_MAX_CLIENTS			237

enum isobusfs_srv_fss_state {
	ISOBUSFS_SRV_STATE_IDLE = 0, /* send status with 2000ms interval */
	ISOBUSFS_SRV_STATE_STAT_CHANGE_1, /* send status with 200ms interval */
	ISOBUSFS_SRV_STATE_STAT_CHANGE_2, /* send status with 200ms interval */
	ISOBUSFS_SRV_STATE_STAT_CHANGE_3, /* send status with 200ms interval */
	ISOBUSFS_SRV_STATE_STAT_CHANGE_4, /* send status with 200ms interval */
	ISOBUSFS_SRV_STATE_STAT_CHANGE_5, /* send status with 200ms interval */
	ISOBUSFS_SRV_STATE_BUSY, /* send status with 200ms interval */
};

struct isobusfs_srv_client {
	int sock;
	struct timespec last_received;
	uint8_t addr;
	uint8_t tan;
	uint8_t version;
	char current_dir[ISOBUSFS_SRV_MAX_PATH_LEN];
};

struct isobusfs_srv_volume {
	char *name;
	char *path;
	bool removable;
	bool writeable;
	int refcount;
	struct isobusfs_srv_client *clients[ISOBUSFS_SRV_MAX_CLIENTS];
};

struct isobusfs_srv_handles {
	char *path;
	int refcount;
	int fd;
	off_t offset;
	int32_t dir_pos;
	DIR *dir;
	struct isobusfs_srv_client *clients[ISOBUSFS_SRV_MAX_CLIENTS];
};

struct isobusfs_srv_priv {
	/* incoming traffic from peers */
	int sock_in;
	/*
	 * egress only File Server Status broadcast packets with different
	 * prio
	 */
	int sock_fss;
	/*
	 * bidirectional socket for NACK packets.
	 * ISO 11783-3:2018 5.4.5 Acknowledgement
	 */
	int sock_nack;
	struct sockaddr_can addr;

	int server_version;

	/* fs status related variables */
	struct isobusfs_cm_fss st; /* file server status message */
	enum isobusfs_srv_fss_state st_state;
	struct isobusfs_stats st_msg_stats;

	/* client related variables */
	struct isobusfs_srv_client clients[ISOBUSFS_SRV_MAX_CLIENTS];
	int clients_count;
	struct isobusfs_buf_log tx_buf_log;

	struct isobusfs_cmn cmn;

	struct isobusfs_srv_volume volumes[ISOBUSFS_SRV_MAX_VOLUMES];
	int volume_count;
	int removable_volumes_count;
	const char *default_volume;
	/* manufacturer-specific directory */
	char mfs_dir[9];
	uint64_t local_name;

	struct isobusfs_srv_handles handles[ISOBUSFS_SRV_MAX_OPENED_HANDLES];
	int handles_count;
};

/* isobusfs_srv.c */
int isobusfs_srv_send_error(struct isobusfs_srv_priv *priv,
			    struct isobusfs_msg *msg, enum isobusfs_error err);
int isobusfs_srv_sendto(struct isobusfs_srv_priv *priv,
			struct isobusfs_msg *msg, const void *buf,
			size_t buf_size);

/* isobusfs_srv_cm_fss.c */
void isobusfs_srv_fss_init(struct isobusfs_srv_priv *priv);
int isobusfs_srv_fss_send(struct isobusfs_srv_priv *priv);

/* isobusfs_srv_cm.c */
int isobusfs_srv_rx_cg_cm(struct isobusfs_srv_priv *priv,
			  struct isobusfs_msg *msg);
void isobusfs_srv_remove_timeouted_clients(struct isobusfs_srv_priv *priv);
void isobusfs_srv_init_clients(struct isobusfs_srv_priv *priv);
struct isobusfs_srv_client *isobusfs_srv_get_client(
		struct isobusfs_srv_priv *priv, uint8_t addr);
struct isobusfs_srv_client *isobusfs_srv_get_client_by_msg(
		struct isobusfs_srv_priv *priv, struct isobusfs_msg *msg);

/* isobusfs_srv_dh.c */
int isobusfs_srv_rx_cg_dh(struct isobusfs_srv_priv *priv,
			  struct isobusfs_msg *msg);
int isobusfs_path_to_linux_path(struct isobusfs_srv_priv *priv,
		const char *isobusfs_path, size_t isobusfs_path_size,
		char *linux_path, size_t linux_path_size);
int isobusfs_check_current_dir_access(struct isobusfs_srv_priv *priv,
				      const char *path, size_t path_size);
int isobusfs_convert_relative_to_absolute(struct isobusfs_srv_priv *priv,
					  const char *current_dir,
					  const char *rel_path,
					  size_t rel_path_size, char *abs_path,
					  size_t abs_path_size);
void isobusfs_srv_set_default_current_dir(struct isobusfs_srv_priv *priv,
					  struct isobusfs_srv_client *client);


/* isobusfs_srv_vh.c */
int isobusfs_srv_rx_cg_vh(struct isobusfs_srv_priv *priv,
			  struct isobusfs_msg *msg);

/* isobusfs_srv_fh.c */
int isobusfs_srv_rx_cg_fh(struct isobusfs_srv_priv *priv,
			  struct isobusfs_msg *msg);

/* isobusfs_srv_fa.c */
int isobusfs_srv_rx_cg_fa(struct isobusfs_srv_priv *priv,
			  struct isobusfs_msg *msg);
void isobusfs_srv_remove_client_from_handles(struct isobusfs_srv_priv *priv,
					     struct isobusfs_srv_client *client);


#endif /* ISOBUSFS_SRV_H */
