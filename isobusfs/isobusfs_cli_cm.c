// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "isobusfs_cli.h"
#include "isobusfs_cmn_cm.h"


int isobusfs_cli_volume_status_req(struct isobusfs_priv *priv,
				   uint8_t volume_mode,
				   uint16_t path_name_length,
				   const char *volume_name)
{
	struct isobusfs_cm_vol_stat_req req;
	size_t req_size;
	int ret;

	req.fs_function =
		isobusfs_cg_function_to_buf(ISOBUSFS_CG_CONNECTION_MANAGMENT,
					    ISOBUSFS_CM_VOLUME_STATUS_REQ);

	req.volume_mode = volume_mode;
	req.name_len = htole16(path_name_length);
	req_size = sizeof(req) - sizeof(req.name) + path_name_length;

	memcpy(req.name, volume_name, path_name_length);

	ret = isobusfs_send(priv->sock_main, &req, req_size, &priv->tx_buf_log);
	if (ret < 0) {
		ret = -errno;
		pr_warn("failed to send volume status request: %d (%s)", ret, strerror(ret));
		return ret;
	}

	priv->state = ISOBUSFS_CLI_STATE_WAIT_VOLUME_STATUS;

	pr_debug("> tx: volume status request");
	return 0;
}


int isobusfs_cli_property_req(struct isobusfs_priv *priv)
{
	uint8_t buf[ISOBUSFS_MIN_TRANSFER_LENGH];
	int ret;

	/* not used space should be filled with 0xff */
	memset(buf, 0xff, ARRAY_SIZE(buf));
	buf[0] = isobusfs_cg_function_to_buf(ISOBUSFS_CG_CONNECTION_MANAGMENT,
					     ISOBUSFS_CM_GET_FS_PROPERTIES);

	/* send property request */
	ret = isobusfs_send(priv->sock_main, buf, sizeof(buf), &priv->tx_buf_log);
	if (ret < 0)
		return ret;

	priv->state = ISOBUSFS_CLI_STATE_WAIT_FS_PROPERTIES;

	pr_debug("> tx: FS property request");
	return 0;
}

/* ccm section */
void isobusfs_cli_ccm_init(struct isobusfs_priv *priv)
{
	struct isobusfs_cm_ccm *ccm = &priv->ccm;

	ccm->fs_function =
		isobusfs_cg_function_to_buf(ISOBUSFS_CG_CONNECTION_MANAGMENT,
					    ISOBUSFS_CM_F_FS_STATUS);
	ccm->version = 2;
	memset(ccm->reserved, 0xFF, sizeof(ccm->reserved));
}

/**
 * isobusfs_cli_ccm_send - send periodic file server status messages
 * @priv: pointer to the isobusfs_priv structure
 *
 * Returns 0 on success, -1 on errors.
 */
int isobusfs_cli_ccm_send(struct isobusfs_priv *priv)
{
	int64_t time_diff;
	int ret;

	/* Test if it is proper time to send next status message. */
	time_diff = timespec_diff_ms(&priv->cmn.next_send_time,
				     &priv->cmn.last_time);
	if (time_diff > ISOBUSFS_CM_F_FS_STATUS_RATE_JITTER) {
		/* too early to send next message */
		return 0;
	}

	if (time_diff < -ISOBUSFS_CM_F_FS_STATUS_RATE_JITTER) {
		pr_warn("too late to send next fs status message: %ld ms",
			time_diff);
	}

	/* Make sure we send the message with the latest stats */
	if (priv->stats.tskey_sch != priv->stats.tskey_ack)
		pr_warn("previous message was not acked");

	/* send periodic file servers status messages. */
	ret = isobusfs_send(priv->sock_ccm, &priv->ccm, sizeof(priv->ccm),
			    &priv->tx_buf_log);
	if (ret < 0) {
		pr_err("sendto() failed: %d (%s)", ret, strerror(ret));
		return ret;
	}

	pr_debug("> tx: ccm version: %d", priv->ccm.version);

	priv->cmn.next_send_time = priv->cmn.last_time;
	timespec_add_ms(&priv->cmn.next_send_time, 2000);

	return 0;
}

/* detect if FS is timeout */
void isobusfs_cli_fs_detect_timeout(struct isobusfs_priv *priv)
{
	int64_t time_diff;

	if (!priv->fs_is_active)
		return;

	time_diff = timespec_diff_ms(&priv->cmn.last_time,
				     &priv->fs_last_seen);
	if (time_diff > ISOBUSFS_FS_TIMEOUT) {
		pr_debug("file server timeout");
		priv->fs_is_active = false;
	}
}

/* activate FS status if was not active till now */
static void isobusfs_cli_fs_activate(struct isobusfs_priv *priv)
{
	if (priv->fs_is_active)
		return;

	pr_debug("file server detectet");
	priv->fs_is_active = true;
}

static int isobusfs_cli_rx_fs_status(struct isobusfs_priv *priv,
				     struct isobusfs_msg *msg)
{
	struct isobusfs_cm_fss *fs_status = (void *)msg->buf;
	int ret = 0;

	if (msg->len != sizeof(*fs_status)) {
		pr_warn("wrong message length: %d", msg->len);
		return -EINVAL;
	}

	isobusfs_cli_fs_activate(priv);

	priv->fs_last_seen = priv->cmn.last_time;
	pr_debug("< rx: fs status: %x, opened files: %d",
	      fs_status->status, fs_status->num_open_files);

	return ret;
}

/* process FS properties response */
static int isobusfs_cli_rx_fs_property_res(struct isobusfs_priv *priv,
					   struct isobusfs_msg *msg)
{
	struct isobusfs_cm_get_fs_props_resp *fs_prop = (void *)msg->buf;
	int ret = 0;

	if (priv->state != ISOBUSFS_CLI_STATE_WAIT_FS_PROPERTIES) {
		pr_warn("unexpected fs properties response");
		return -EINVAL;
	}

	if (msg->len != sizeof(*fs_prop)) {
		pr_warn("wrong message length: %d", msg->len);
		return -EINVAL;
	}

	priv->fs_version = fs_prop->version_number;
	priv->fs_max_open_files = fs_prop->max_open_files;
	priv->fs_caps = fs_prop->fs_capabilities;


	pr_debug("< rx: fs properties: version: %d, max open files: %d, caps: %x",
		 priv->fs_version, priv->fs_max_open_files, priv->fs_caps);

	priv->state = ISOBUSFS_CLI_STATE_GET_FS_PROPERTIES_DONE;

	return ret;
}

/* function to handle ISOBUSFS_CM_VOLUME_STATUS_RES */
static int isobusfs_cli_rx_volume_status_res(struct isobusfs_priv *priv,
					     struct isobusfs_msg *msg)
{
	struct isobusfs_cm_vol_stat_res *vol_status = (void *)msg->buf;
	int ret = 0;

	if (priv->state != ISOBUSFS_CLI_STATE_WAIT_VOLUME_STATUS) {
		pr_warn("unexpected volume status response");
		return -EINVAL;
	}

	pr_debug("< rx: volume status: %x, max time before remove %d, error code %d, path name length %d, name %s",
		 vol_status->volume_status,
		 vol_status->max_time_before_removal,
		 vol_status->error_code,
		 vol_status->name_len,
		 vol_status->name);

	priv->state = ISOBUSFS_CLI_STATE_VOLUME_STATUS_DONE;

	return ret;
}

/* Command group: connection management */
int isobusfs_cli_rx_cg_cm(struct isobusfs_priv *priv, struct isobusfs_msg *msg)
{
	int func = isobusfs_buf_to_function(msg->buf);
	int ret = 0;

	switch (func) {
	case ISOBUSFS_CM_F_FS_STATUS:
		return isobusfs_cli_rx_fs_status(priv, msg);
	case ISOBUSFS_CM_GET_FS_PROPERTIES_RES:
		return isobusfs_cli_rx_fs_property_res(priv, msg);
	case ISOBUSFS_CM_VOLUME_STATUS_RES:
		return isobusfs_cli_rx_volume_status_res(priv, msg);
	default:
		pr_warn("unsupported function: %i", func);
		return -EINVAL;
	}

	return ret;
}
