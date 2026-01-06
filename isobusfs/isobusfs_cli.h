// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#ifndef ISOBUSFS_CLI_H
#define ISOBUSFS_CLI_H

#include <sys/epoll.h>
#include <stdbool.h>

#include "isobusfs_cmn.h"
#include "isobusfs_cmn_cm.h"

#define ISOBUSFS_CLI_MAX_EPOLL_EVENTS		10
#define ISOBUSFS_CLI_DEFAULT_WAIT_TIMEOUT_MS	1000 /* ms */

/* internel return codes, not errno values */
#define ISOBUSFS_CLI_RET_EXIT			1

enum isobusfs_cli_state {
	ISOBUSFS_CLI_STATE_CONNECTING,
	ISOBUSFS_CLI_STATE_IDLE,
	ISOBUSFS_CLI_STATE_NACKED, /* here is NACKed and not what you think */
	ISOBUSFS_CLI_STATE_SELFTEST,
	ISOBUSFS_CLI_STATE_WAIT_FS_PROPERTIES,
	ISOBUSFS_CLI_STATE_WAIT_CURRENT_DIR,
	ISOBUSFS_CLI_STATE_WAIT_CCD_RESP,
	ISOBUSFS_CLI_STATE_WAIT_OF_RESP,
	ISOBUSFS_CLI_STATE_WAIT_FILE_SIZE,
	ISOBUSFS_CLI_STATE_WAIT_FILE,
	ISOBUSFS_CLI_STATE_WAIT_VOLUME_STATUS,
	ISOBUSFS_CLI_STATE_WAIT_CF_RESP,
	ISOBUSFS_CLI_STATE_WAIT_SF_RESP, /* wait for seek file response */
	ISOBUSFS_CLI_STATE_WAIT_RF_RESP, /* wait for read file response */
	ISOBUSFS_CLI_STATE_MAX_WAITING,

	ISOBUSFS_CLI_STATE_CONNECTING_DONE,
	ISOBUSFS_CLI_STATE_GET_FS_PROPERTIES_DONE,
	ISOBUSFS_CLI_STATE_GET_CURRENT_DIR_DONE,
	ISOBUSFS_CLI_STATE_GET_CURRENT_DIR_FAIL,
	ISOBUSFS_CLI_STATE_GET_FILE_SIZE_DONE,
	ISOBUSFS_CLI_STATE_GET_FILE_DONE,
	ISOBUSFS_CLI_STATE_VOLUME_STATUS_DONE,
	ISOBUSFS_CLI_STATE_CCD_DONE,
	ISOBUSFS_CLI_STATE_CCD_FAIL,
	ISOBUSFS_CLI_STATE_OF_DONE,
	ISOBUSFS_CLI_STATE_OF_FAIL,
	ISOBUSFS_CLI_STATE_CF_DONE,
	ISOBUSFS_CLI_STATE_CF_FAIL,
	ISOBUSFS_CLI_STATE_SF_DONE,
	ISOBUSFS_CLI_STATE_SF_FAIL,
	ISOBUSFS_CLI_STATE_RF_CONT,
	ISOBUSFS_CLI_STATE_RF_DONE,
	ISOBUSFS_CLI_STATE_RF_FAIL,
	ISOBUSFS_CLI_STATE_MAX_DONE,

	ISOBUSFS_CLI_STATE_GET_FS_PROPERTIES,
	ISOBUSFS_CLI_STATE_GET_CURRENT_DIR,
	ISOBUSFS_CLI_STATE_GET_FILE_SIZE,
	ISOBUSFS_CLI_STATE_GET_FILE,
	ISOBUSFS_CLI_STATE_VOLUME_STATUS,
	ISOBUSFS_CLI_STATE_TEST_CLEANUP,
	ISOBUSFS_CLI_STATE_TEST_DONE,
	ISOBUSFS_CLI_STATE_MAX_ACTIVE,
};

struct isobusfs_priv;

typedef int (*isobusfs_event_callback)(struct isobusfs_priv *priv,
				       struct isobusfs_msg *msg, void *ctx,
				       int error);

struct isobusfs_event {
	isobusfs_event_callback cb;
	struct timespec timeout;
	/* fs_function is needed to identify package type for event
	 * subscription
	 */
	uint8_t fs_function;
	int fd;
	bool one_shot;
	void *ctx;
};

struct isobusfs_priv {
	int sock_ccm;
	int sock_nack;
	int sock_main;
	int sock_bcast_rx;
	struct isobusfs_cm_ccm ccm; /* file server status message */

	bool run_selftest;

	struct sockaddr_can sockname;
	struct sockaddr_can peername;

	struct isobusfs_stats stats;

	uint8_t next_tan;
	uint8_t cl_buf[1];

	bool fs_is_active;
	struct timespec fs_last_seen;
	uint8_t fs_version;
	uint8_t fs_max_open_files;
	uint8_t fs_caps;
	struct isobusfs_buf_log tx_buf_log;
	enum isobusfs_cli_state state;

	struct libj1939_cmn cmn;
	uint8_t handle;

	uint32_t read_offset;
	uint8_t *read_data;
	size_t read_data_len;

	bool interactive;
	bool int_busy;

	struct isobusfs_event *events;
	uint num_events;
	uint max_events;

	enum isobusfs_error error_code;
};

/* isobusfs_cli_cm.c */
void isobusfs_cli_ccm_init(struct isobusfs_priv *priv);
int isobusfs_cli_ccm_send(struct isobusfs_priv *priv);
void isobusfs_cli_fs_detect_timeout(struct isobusfs_priv *priv);
int isobusfs_cli_rx_cg_cm(struct isobusfs_priv *priv, struct isobusfs_msg *msg);
int isobusfs_cli_property_req(struct isobusfs_priv *priv);
int isobusfs_cli_volume_status_req(struct isobusfs_priv *priv,
				   uint8_t volume_mode,
				   uint16_t path_name_length,
				   const char *volume_name);

/* isobusfs_cli_dh.c */
int isobusfs_cli_ccd_req(struct isobusfs_priv *priv, const char *name,
			 size_t name_len);
int isobusfs_cli_get_current_dir_req(struct isobusfs_priv *priv);
int isobusfs_cli_rx_cg_dh(struct isobusfs_priv *priv,
			  struct isobusfs_msg *msg);
int isobusfs_cli_send_and_register_ccd_event(struct isobusfs_priv *priv,
					     const char *name,
					     size_t name_len,
					     isobusfs_event_callback cb,
					     void *ctx);
int isobusfs_cli_send_and_register_gcd_event(struct isobusfs_priv *priv,
					     isobusfs_event_callback cb,
					     void *ctx);

/* isobusfs_cli_fa.c */
int isobusfs_cli_rx_cg_fa(struct isobusfs_priv *priv,
			  struct isobusfs_msg *msg);
int isobusfs_cli_fa_of_req(struct isobusfs_priv *priv, const char *name,
			   size_t name_len, uint8_t flags);
int isobusfs_cli_fa_cf_req(struct isobusfs_priv *priv, uint8_t handle);
int isobusfs_cli_fa_rf_req(struct isobusfs_priv *priv, uint8_t handle,
			   uint16_t count);
int isobusfs_cli_fa_sf_req(struct isobusfs_priv *priv, uint8_t handle,
			   uint8_t position_mode, int32_t offset);

int isobusfs_cli_send_and_register_fa_of_event(struct isobusfs_priv *priv,
					     const char *name,
					     size_t name_len,
					     uint8_t flags,
					     isobusfs_event_callback cb,
					     void *ctx);
int isobusfs_cli_send_and_register_fa_sf_event(struct isobusfs_priv *priv,
					       uint8_t handle,
					       uint8_t position_mode,
					       int32_t offset,
					       isobusfs_event_callback cb,
					       void *ctx);
int isobusfs_cli_send_and_register_fa_rf_event(struct isobusfs_priv *priv,
					       uint8_t handle,
					       uint16_t count,
					       isobusfs_event_callback cb,
					       void *ctx);
int isobusfs_cli_send_and_register_fa_cf_event(struct isobusfs_priv *priv,
					       uint8_t handle,
					       isobusfs_event_callback cb,
					       void *ctx);

/* isobusfs_cli_selftests.c */
void isobusfs_cli_run_self_tests(struct isobusfs_priv *priv);

/* isobusfs_cli_int.c */
void isobusfs_cli_int_start(struct isobusfs_priv *priv);
int isobusfs_cli_interactive(struct isobusfs_priv *priv);

/* isobusfs_cli.c */
int isobusfs_cli_process_events_and_tasks(struct isobusfs_priv *priv);
void isobusfs_cli_prepare_response_event(struct isobusfs_event *event, int sock,
					 uint8_t fs_function);
int isobusfs_cli_register_event(struct isobusfs_priv *priv,
				const struct isobusfs_event *new_event);

static inline uint8_t isobusfs_cli_get_next_tan(struct isobusfs_priv *priv)
{
	return priv->next_tan++;
}

static inline bool isobusfs_cli_tan_is_valid(uint8_t tan,
					     struct isobusfs_priv *priv)
{
	uint8_t expected_tan = priv->next_tan == 0 ? 255 : priv->next_tan - 1;

	if (tan != expected_tan) {
		pr_err("%s: tan %d is not valid, expected tan %d\n", __func__,
		       tan, expected_tan);
		return false;
	}

	return true;
}

#endif /* ISOBUSFS_CLI_H */
