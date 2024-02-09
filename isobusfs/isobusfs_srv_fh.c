// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "isobusfs_srv.h"

/* Command group: file handling */
int isobusfs_srv_rx_cg_fh(struct isobusfs_srv_priv *priv,
			  struct isobusfs_msg *msg)
{
	int func = isobusfs_buf_to_function(msg->buf);
	int ret = 0;

	switch (func) {
	case ISOBUSFS_FH_F_MOVE_FILE_REQ:
	case ISOBUSFS_FH_F_DELETE_FILE_REQ:
	case ISOBUSFS_FH_F_GET_FILE_ATTR_REQ:
	case ISOBUSFS_FH_F_SET_FILE_ATTR_REQ:
	case ISOBUSFS_FH_F_GET_FILE_DATETIME_REQ:
	default:
		isobusfs_srv_send_error(priv, msg,
					ISOBUSFS_ERR_FUNC_NOT_SUPPORTED);
		pr_warn("%s: unsupported function: %i", __func__, func);
	}

	return ret;
}
