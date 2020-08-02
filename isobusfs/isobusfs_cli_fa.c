// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "isobusfs_cli.h"
#include "isobusfs_cmn_fa.h"

int isobusfs_cli_fa_sf_req(struct isobusfs_priv *priv, uint8_t handle,
			   uint8_t position_mode, int32_t offset)
{
	struct isobusfs_fa_seekf_req req;
	int ret;

	req.fs_function = isobusfs_cg_function_to_buf(ISOBUSFS_CG_FILE_ACCESS,
						      ISOBUSFS_FA_F_SEEK_FILE_REQ);
	req.tan = isobusfs_cli_get_next_tan(priv);
	req.handle = handle;
	req.position_mode = position_mode;
	req.offset = htole32(offset);

	priv->state = ISOBUSFS_CLI_STATE_WAIT_SF_RESP;

	ret = isobusfs_send(priv->sock_main, &req, sizeof(req),
			    &priv->tx_buf_log);
	if (ret < 0) {
		pr_warn("failed to send Seek File Request: %d (%s)", ret,
			strerror(ret));
		return ret;
	}

	pr_debug("> tx: Seek File Request for handle: %x, position mode: %d, offset: %d",
		 req.handle, req.position_mode, req.offset);

	return ret;
}

static int isobusfs_cli_fa_sf_res_log(struct isobusfs_priv *priv,
				      struct isobusfs_msg *msg,
					     void *ctx, int error)
{
	struct isobusfs_fa_seekf_res *res =
		(struct isobusfs_fa_seekf_res *)msg;

	if (!isobusfs_cli_tan_is_valid(res->tan, priv)) {
		priv->state = ISOBUSFS_CLI_STATE_SF_FAIL;
		return -EINVAL;
	}

	if (res->error_code) {
		priv->state = ISOBUSFS_CLI_STATE_SF_FAIL;
		pr_warn("< rx: Seek File Error - Error code: %d",
			res->error_code);
		return -EIO;
	}

	priv->read_offset = le32toh(res->position);
	priv->state = ISOBUSFS_CLI_STATE_SF_DONE;
	pr_debug("< rx: Seek File Success, position: %d", res->position);

	return 0;
}

int isobusfs_cli_send_and_register_fa_sf_event(struct isobusfs_priv *priv,
					       uint8_t handle,
					       uint8_t position_mode,
					       int32_t offset,
					       isobusfs_event_callback cb,
					       void *ctx)
{
	struct isobusfs_event event;
	uint8_t fs_function;
	int ret;

	ret = isobusfs_cli_fa_sf_req(priv, handle, position_mode, offset);
	if (ret < 0)
		return ret;

	fs_function =
		isobusfs_cg_function_to_buf(ISOBUSFS_CG_FILE_ACCESS,
					    ISOBUSFS_FA_F_SEEK_FILE_RES);

	if (cb)
		event.cb = cb;
	else
		event.cb = isobusfs_cli_fa_sf_res_log;

	event.ctx = ctx;

	isobusfs_cli_prepare_response_event(&event, priv->sock_main,
					    fs_function);

	return isobusfs_cli_register_event(priv, &event);
}

int isobusfs_cli_fa_rf_req(struct isobusfs_priv *priv, uint8_t handle,
			   uint16_t count)
{
	struct isobusfs_fa_readf_req req;
	int ret;

	req.fs_function = isobusfs_cg_function_to_buf(ISOBUSFS_CG_FILE_ACCESS,
						      ISOBUSFS_FA_F_READ_FILE_REQ);
	req.tan = isobusfs_cli_get_next_tan(priv);
	req.handle = handle;
	req.count = htole16(count);
	memset(req.reserved, 0xff, sizeof(req.reserved));

	priv->state = ISOBUSFS_CLI_STATE_WAIT_RF_RESP;

	ret = isobusfs_send(priv->sock_main, &req, sizeof(req), &priv->tx_buf_log);
	if (ret < 0) {
		ret = -errno;
		pr_warn("failed to send Read File Request: %d (%s)", ret, strerror(ret));
		return ret;
	}

	pr_debug("> tx: Read File Request for handle: %x, size: %d", req.handle, count);

	return ret;
}

static int isobusfs_cli_fa_rf_res_log(struct isobusfs_priv *priv,
				      struct isobusfs_msg *msg,
				      void *ctx, int error)
{
	struct isobusfs_read_file_response *res =
		(struct isobusfs_read_file_response *)msg->buf;

	if (priv->state != ISOBUSFS_CLI_STATE_WAIT_RF_RESP) {
		pr_warn("invalid state: %i (expected %i)", priv->state,
			ISOBUSFS_CLI_STATE_WAIT_RF_RESP);
		return -EINVAL;
	}

	if (!isobusfs_cli_tan_is_valid(res->tan, priv)) {
		priv->state = ISOBUSFS_CLI_STATE_RF_FAIL;
	} else if (res->error_code && res->error_code != ISOBUSFS_ERR_END_OF_FILE) {
		pr_warn("read file failed with error code: %i", res->error_code);
		priv->state = ISOBUSFS_CLI_STATE_RF_FAIL;
	} else {
		if (priv->read_data) {
			pr_err("read data buffer not empty");
			free(priv->read_data);
		}

		priv->read_data_len = le16toh(res->count);
		priv->read_data = malloc(priv->read_data_len);
		if (!priv->read_data) {
			pr_err("failed to allocate memory for data");
			priv->state = ISOBUSFS_CLI_STATE_RF_FAIL;
		} else {
			memcpy(priv->read_data, res->data, priv->read_data_len);
			priv->state = ISOBUSFS_CLI_STATE_RF_DONE;
		}
	}

	pr_debug("< rx: Read File Response. Error code: %i", res->error_code);
	return 0;
}

int isobusfs_cli_send_and_register_fa_rf_event(struct isobusfs_priv *priv,
					       uint8_t handle,
					       uint16_t count,
					       isobusfs_event_callback cb,
					       void *ctx)
{
	struct isobusfs_event event;
	uint8_t fs_function;
	int ret;

	ret = isobusfs_cli_fa_rf_req(priv, handle, count);
	if (ret < 0)
		return ret;

	fs_function =
		isobusfs_cg_function_to_buf(ISOBUSFS_CG_FILE_ACCESS,
					    ISOBUSFS_FA_F_READ_FILE_RES);

	if (cb)
		event.cb = cb;
	else
		event.cb = isobusfs_cli_fa_rf_res_log;

	event.ctx = ctx;

	isobusfs_cli_prepare_response_event(&event, priv->sock_main,
					    fs_function);

	return isobusfs_cli_register_event(priv, &event);
}

int isobusfs_cli_fa_cf_req(struct isobusfs_priv *priv, uint8_t handle)
{
	struct isobusfs_close_file_request req;
	int ret;

	req.fs_function = isobusfs_cg_function_to_buf(ISOBUSFS_CG_FILE_ACCESS,
						      ISOBUSFS_FA_F_CLOSE_FILE_REQ);
	req.tan = isobusfs_cli_get_next_tan(priv);
	req.handle = handle;
	memset(req.reserved, 0xff, sizeof(req.reserved));

	priv->state = ISOBUSFS_CLI_STATE_WAIT_CF_RESP;

	ret = isobusfs_send(priv->sock_main, &req, sizeof(req), &priv->tx_buf_log);
	if (ret < 0) {
		ret = -errno;
		pr_warn("failed to send Close File request: %d (%s)", ret, strerror(ret));
		return ret;
	}

	pr_debug("> tx: Close File Request for handle: %x", req.handle);

	return ret;
}

static int isobusfs_cli_fa_cf_res_log(struct isobusfs_priv *priv,
				      struct isobusfs_msg *msg,
				      void *ctx, int error)
{
	struct isobusfs_close_file_res *res =
		(struct isobusfs_close_file_res *)msg->buf;

	if (priv->state != ISOBUSFS_CLI_STATE_WAIT_CF_RESP) {
		pr_warn("invalid state: %i (expected %i)", priv->state,
			ISOBUSFS_CLI_STATE_WAIT_CF_RESP);
		return -EINVAL;
	}

	if (!isobusfs_cli_tan_is_valid(res->tan, priv)) {
		priv->state = ISOBUSFS_CLI_STATE_CF_FAIL;
	} else if (res->error_code != 0) {
		pr_warn("ccd failed with error code: %i", res->error_code);
		priv->state = ISOBUSFS_CLI_STATE_CF_FAIL;
	} else {
		priv->state = ISOBUSFS_CLI_STATE_CF_DONE;
	}

	pr_debug("< rx: Close File Response. Error code: %i",
		 res->error_code);

	return 0;
}

int isobusfs_cli_send_and_register_fa_cf_event(struct isobusfs_priv *priv,
					       uint8_t handle,
					       isobusfs_event_callback cb,
					       void *ctx)
{
	struct isobusfs_event event;
	uint8_t fs_function;
	int ret;

	ret = isobusfs_cli_fa_cf_req(priv, handle);
	if (ret < 0)
		return ret;

	fs_function =
		isobusfs_cg_function_to_buf(ISOBUSFS_CG_FILE_ACCESS,
					    ISOBUSFS_FA_F_CLOSE_FILE_RES);

	if (cb)
		event.cb = cb;
	else
		event.cb = isobusfs_cli_fa_cf_res_log;

	event.ctx = ctx;

	isobusfs_cli_prepare_response_event(&event, priv->sock_main,
					    fs_function);

	return isobusfs_cli_register_event(priv, &event);
}

int isobusfs_cli_fa_of_req(struct isobusfs_priv *priv, const char *name,
			   size_t name_len, uint8_t flags)
{
	struct isobusfs_fa_openf_req *req;
	size_t req_len = sizeof(*req) + name_len;
	size_t padding_size = 0;
	int ret;

	if (name_len > ISOBUSFS_MAX_PATH_NAME_LENGTH) {
		pr_warn("path name too long: %i, max is %i", name_len,
			ISOBUSFS_MAX_PATH_NAME_LENGTH);
		return -EINVAL;
	}

	if (req_len < ISOBUSFS_MIN_TRANSFER_LENGH) {
		/* Update the buffer size accordingly */
		padding_size = ISOBUSFS_MIN_TRANSFER_LENGH - req_len;
		req_len = ISOBUSFS_MIN_TRANSFER_LENGH;
	}

	req = malloc(req_len);
	if (!req) {
		pr_err("failed to allocate memory for ccd request");
		return -ENOMEM;
	}

	req->fs_function = isobusfs_cg_function_to_buf(ISOBUSFS_CG_FILE_ACCESS,
						       ISOBUSFS_FA_F_OPEN_FILE_REQ);
	req->tan = isobusfs_cli_get_next_tan(priv);
	req->flags = flags;
	memcpy(&req->name[0], name, name_len);
	req->name_len = name_len;

	if (padding_size) {
		/* Fill the rest of the res structure with 0xff */
		memset(((uint8_t *)req) + req_len - padding_size, 0xff,
		       padding_size);
	}

	priv->handle = ISOBUSFS_FILE_HANDLE_ERROR;
	priv->state = ISOBUSFS_CLI_STATE_WAIT_OF_RESP;
	ret = isobusfs_send(priv->sock_main, req, req_len, &priv->tx_buf_log);
	if (ret < 0) {
		ret = -errno;
		pr_warn("failed to send ccd request: %d (%s)",
			ret, strerror(ret));
		goto free_req;
	}

	pr_debug("> tx: Open File Request for %s, with flags: %x", name,
		 req->flags);

free_req:
	free(req);

	return ret;
}

static int isobusfs_cli_fa_open_file_res_log(struct isobusfs_priv *priv,
					     struct isobusfs_msg *msg,
					     void *ctx, int error)

{
	struct isobusfs_fa_openf_res *res =
		(struct isobusfs_fa_openf_res *)msg->buf;

	if (priv->state != ISOBUSFS_CLI_STATE_WAIT_OF_RESP) {
		pr_warn("invalid state: %i (expected %i)", priv->state,
			ISOBUSFS_CLI_STATE_WAIT_OF_RESP);
		return -EINVAL;
	}

	if (!isobusfs_cli_tan_is_valid(res->tan, priv)) {
		priv->state = ISOBUSFS_CLI_STATE_OF_FAIL;
	} else if (res->error_code != 0) {
		pr_warn("open file request failed with error code: %i", res->error_code);
		priv->state = ISOBUSFS_CLI_STATE_OF_FAIL;
	} else if (res->handle == ISOBUSFS_FILE_HANDLE_ERROR) {
		pr_warn("open file request didn't failed with error code, but with handle");
		priv->state = ISOBUSFS_CLI_STATE_OF_FAIL;
	} else {
		priv->state = ISOBUSFS_CLI_STATE_OF_DONE;
		priv->handle = res->handle;
	}

	pr_debug("< rx: Open File Response. Error code: %i",
		 res->error_code);

	return 0;
}

int isobusfs_cli_send_and_register_fa_of_event(struct isobusfs_priv *priv,
					     const char *name,
					     size_t name_len,
					     uint8_t flags,
					     isobusfs_event_callback cb,
					     void *ctx)
{
	struct isobusfs_event event;
	uint8_t fs_function;
	int ret;

	ret = isobusfs_cli_fa_of_req(priv, name, name_len, flags);
	if (ret < 0)
		return ret;

	fs_function =
		isobusfs_cg_function_to_buf(ISOBUSFS_CG_FILE_ACCESS,
					    ISOBUSFS_FA_F_OPEN_FILE_RES);

	if (cb)
		event.cb = cb;
	else
		event.cb = isobusfs_cli_fa_open_file_res_log;

	event.ctx = ctx;

	isobusfs_cli_prepare_response_event(&event, priv->sock_main,
					    fs_function);

	return isobusfs_cli_register_event(priv, &event);
}

/* Command group: directory handling */
int isobusfs_cli_rx_cg_fa(struct isobusfs_priv *priv,
			  struct isobusfs_msg *msg)
{
	int func = isobusfs_buf_to_function(msg->buf);
	int ret = 0;

	switch (func) {
	case ISOBUSFS_FA_F_OPEN_FILE_RES:
		ret = isobusfs_cli_fa_open_file_res_log(priv, msg, NULL, 0);
		break;
	case ISOBUSFS_FA_F_CLOSE_FILE_RES:
		ret = isobusfs_cli_fa_cf_res_log(priv, msg, NULL, 0);
		break;
	case ISOBUSFS_FA_F_READ_FILE_RES:
		ret = isobusfs_cli_fa_rf_res_log(priv, msg, NULL, 0);
		break;
	case ISOBUSFS_FA_F_SEEK_FILE_RES:
		ret = isobusfs_cli_fa_sf_res_log(priv, msg, NULL, 0);
		break;
	case ISOBUSFS_FA_F_WRITE_FILE_RES:
	default:
		pr_warn("%s: unsupported function: %i", __func__, func);
		/* Not a critical error */
	}

	return ret;
}
