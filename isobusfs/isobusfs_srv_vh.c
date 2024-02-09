// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "isobusfs_srv.h"

/* Command group: volume hnadling */
int isobusfs_srv_rx_cg_vh(struct isobusfs_srv_priv *priv,
			  struct isobusfs_msg *msg)
{
	int func = isobusfs_buf_to_function(msg->buf);
	int ret = 0;

	switch (func) {
	/* TODO: currently not implemented */
	case ISOBUSFS_VA_F_INITIALIZE_VOLUME_REQ: /* fall through */
	default:
		isobusfs_srv_send_error(priv, msg,
					ISOBUSFS_ERR_FUNC_NOT_SUPPORTED);
		pr_warn("%s: unsupported function: %i", __func__, func);
	}

	return ret;
}
