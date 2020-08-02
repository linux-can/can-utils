// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>
/*
 * This file implements Annex C.1.2 File Server Status according to
 * ISO 11783-13:2021.
 */

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

#include "isobusfs_srv.h"

/*
 * isobusfs_srv_fss_init - Initialize the file server status structure
 * @priv: Pointer to the private data structure of the ISOBUS file server
 *
 * This function initializes the file server status structure, which
 * represents the status of the file server according to Annex C.1.2
 * of ISO 11783-13:2021.
 */
void isobusfs_srv_fss_init(struct isobusfs_srv_priv *priv)
{
	struct isobusfs_cm_fss *st = &priv->st;

	st->fs_function =
		isobusfs_cg_function_to_buf(ISOBUSFS_CG_CONNECTION_MANAGMENT,
					    ISOBUSFS_CM_F_FS_STATUS);
	st->status = 0;
	st->num_open_files = 0;
	memset(st->reserved, 0xFF, sizeof(st->reserved));
}

/*
 * isobusfs_srv_fss_get_rate - Get the rate of File Server Status transmission
 * @priv: Pointer to the private data structure of the ISOBUS file server
 *
 * Returns: the transmission rate of the File Server Status messages depending
 * on the current state of the file server.
 */
static unsigned int isobusfs_srv_fss_get_rate(struct isobusfs_srv_priv *priv)
{
	switch (priv->st_state) {
	case ISOBUSFS_SRV_STATE_IDLE:
		return ISOBUSFS_CM_F_FS_STATUS_IDLE_RATE;
	/*
	 * On every change of Byte 2 "File Server Status" send max 5 status
	 * messages per second.
	 */
	case ISOBUSFS_SRV_STATE_STAT_CHANGE_1: /* fall through */
	case ISOBUSFS_SRV_STATE_STAT_CHANGE_2: /* fall through */
	case ISOBUSFS_SRV_STATE_STAT_CHANGE_3: /* fall through */
	case ISOBUSFS_SRV_STATE_STAT_CHANGE_4: /* fall through */
	case ISOBUSFS_SRV_STATE_STAT_CHANGE_5:
		priv->st_state--;
		return ISOBUSFS_CM_F_FS_STATUS_BUSY_RATE;
	case ISOBUSFS_SRV_STATE_BUSY:
		return ISOBUSFS_CM_F_FS_STATUS_BUSY_RATE;
	default:
		pr_warn("%s:%i: unknown state %d", __func__, __LINE__,
			priv->st_state);
	}

	/*
	 * In case something is wrong, fall back to idle rate to not spam the
	 * bus.
	 */
	return ISOBUSFS_CM_F_FS_STATUS_IDLE_RATE;
}

/**
 * isobusfs_srv_fss_send - Send periodic File Server Status messages
 * @priv: Pointer to the private data structure of the ISOBUS file server
 *
 * Returns: 0 if the message was sent successfully, a negative error code
 * otherwise.
 */
int isobusfs_srv_fss_send(struct isobusfs_srv_priv *priv)
{
	unsigned int next_msg_rate;
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
	if (priv->st_msg_stats.tskey_sch != priv->st_msg_stats.tskey_ack)
		pr_warn("previous message was not acked");

	/* send periodic file servers status messages. */
	ret = send(priv->sock_fss, &priv->st, sizeof(priv->st), MSG_DONTWAIT);
	if (ret < 0) {
		ret = -errno;
		pr_warn("Failed to send FS status message, error code: %d (%s)",
			ret, strerror(ret));
		return ret;
	}

	pr_debug("> tx FS status: 0x%02x, opened files: %d",
		 priv->st.status, priv->st.num_open_files);

	/* Calculate time for the next status message */
	next_msg_rate = isobusfs_srv_fss_get_rate(priv);
	priv->cmn.next_send_time = priv->cmn.last_time;
	timespec_add_ms(&priv->cmn.next_send_time, next_msg_rate);

	return 0;
}
