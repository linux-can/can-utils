// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../libj1939.h"
#include "isobusfs_cli.h"
#include "isobusfs_cmn_dh.h"
#include "isobusfs_cmn_fa.h"

#define MAX_COMMAND_LENGTH 256

#define MAX_DISPLAY_FILENAME_LENGTH 100

struct command_mapping {
	const char *command;
	int (*function)(struct isobusfs_priv *priv, const char *options);
	const char *help;
};

struct command_mapping commands[];

static bool isobusfs_cli_int_is_error(struct isobusfs_priv *priv, int error,
				      uint8_t error_code, uint8_t tan)
{
	bool is_error = false;

	if (error) {
		pr_int("failed with error: %i (%s)\n", error,
		       strerror(error));
		is_error = true;
	} else if (!isobusfs_cli_tan_is_valid(tan, priv)) {
		pr_int("Invalid TAN\n");
		is_error = true;
	} else if (error_code && error_code != ISOBUSFS_ERR_END_OF_FILE) {
		pr_int("Failed with error code: %i (%s)\n", error_code,
		       isobusfs_error_to_str(error_code));
		is_error = true;
	}

	return is_error;
}

static void isobusfs_cli_promt(struct isobusfs_priv *priv)
{
	/* we are currently waiting for a response */
	if (priv->int_busy)
		return;

	pr_int("isobusfs> ");
}

static int cmd_help(struct isobusfs_priv *priv, const char *options)
{
	for (int i = 0; commands[i].command != NULL; i++)
		pr_int("%s - %s\n", commands[i].command, commands[i].help);

	return 0;
}

static int cmd_exit(struct isobusfs_priv *priv, const char *options)
{
	pr_int("exit interactive mode\n");
	/* Return -EINTR to indicate the program should exit */
	return -EINTR;
}

static int cmd_dmesg(struct isobusfs_priv *priv, const char *options)
{
	isobusfs_print_log_buffer();
	return 0;
}

static int cmd_selftest(struct isobusfs_priv *priv, const char *options)
{
	pr_int("run selftest\n");
	priv->run_selftest = true;

	return 0;
}

/* ------ get command -------*/
enum isobusfs_cli_get_state {
	ISOBUSFS_CLI_GET_STATE_START,
	ISOBUSFS_CLI_GET_STATE_OPEN_FILE_SENT,
	ISOBUSFS_CLI_GET_STATE_SEEK_FILE_SENT,
	ISOBUSFS_CLI_GET_STATE_READ_FILE_SENT,
	ISOBUSFS_CLI_GET_STATE_CLOSE_FILE_SENT,
	ISOBUSFS_CLI_GET_STATE_COMPLETED,
	ISOBUSFS_CLI_GET_STATE_ERROR
};

struct isobusfs_cli_get_context {
	enum isobusfs_cli_get_state state;
	int handle;
	size_t offset;
	char *remote_path;
	char *local_path;
	FILE *local_file;
	size_t bytes_received;
	size_t total_size;
};

static int isobusfs_cli_get_event_callback(struct isobusfs_priv *priv,
					   struct isobusfs_msg *msg,
					   void *context, int error);

static void isobusfs_cli_get_handle_send_open_file(struct isobusfs_priv *priv,
					   struct isobusfs_cli_get_context *ctx)
{
	isobusfs_event_callback cb = isobusfs_cli_get_event_callback;
	const char *remote_path = ctx->remote_path;
	uint8_t flags = ISOBUSFS_FA_OPEN_FILE_RO;
	int ret;

	/* Send the open file request to the server */
	ret = isobusfs_cli_send_and_register_fa_of_event(priv, remote_path,
							 strlen(remote_path),
							 flags, cb, ctx);
	if (ret) {
		pr_int("Error: Failed to send open file request, error code: %d\n",
		       ret);
		ctx->state = ISOBUSFS_CLI_GET_STATE_ERROR;
		return;
	}

	ctx->state = ISOBUSFS_CLI_GET_STATE_OPEN_FILE_SENT;
}

static void
isobusfs_cli_get_handle_open_file_sent(struct isobusfs_priv *priv,
				       struct isobusfs_cli_get_context *ctx,
				       struct isobusfs_msg *msg)
{
	isobusfs_event_callback cb = isobusfs_cli_get_event_callback;
	struct isobusfs_fa_openf_res *res =
		(struct isobusfs_fa_openf_res *)msg->buf;
	int ret;

	if (isobusfs_cli_int_is_error(priv, 0, res->error_code, res->tan) ||
	    res->handle == ISOBUSFS_FILE_HANDLE_ERROR) {
		pr_int("Error: Failed to open file on server, error code: %d, handle: %d\n",
		       res->error_code, res->handle);
		ctx->state = ISOBUSFS_CLI_GET_STATE_ERROR;
		return;
	}

	ctx->handle = res->handle;

	/* Seek to the beginning of the file */
	ret = isobusfs_cli_send_and_register_fa_sf_event(priv, ctx->handle,
							 ISOBUSFS_FA_SEEK_SET,
							 0, cb, ctx);
	if (ret) {
		pr_int("Error: Failed to send seek request, error code: %d\n",
		       ret);
		ctx->state = ISOBUSFS_CLI_GET_STATE_ERROR;
		return;
	}

	ctx->state = ISOBUSFS_CLI_GET_STATE_SEEK_FILE_SENT;
}

static void
isobusfs_cli_get_handle_seek_file_sent(struct isobusfs_priv *priv,
				       struct isobusfs_cli_get_context *ctx,
				       struct isobusfs_msg *msg)
{
	isobusfs_event_callback cb = isobusfs_cli_get_event_callback;
	struct isobusfs_fa_seekf_res *res =
		(struct isobusfs_fa_seekf_res *)msg->buf;
	uint16_t read_size;
	int ret;

	if (isobusfs_cli_int_is_error(priv, 0, res->error_code, res->tan) ||
	    res->position != ctx->offset) {
		pr_int("Error: Failed to seek file on server, error code: %d, position: %d\n",
		       res->error_code, res->position);
		ctx->state = ISOBUSFS_CLI_GET_STATE_ERROR;
		return;
	}

	/* set max possible number fitting in to 16bits */
	read_size = UINT16_MAX;

	ret = isobusfs_cli_send_and_register_fa_rf_event(priv, ctx->handle,
							 read_size, cb, ctx);
	if (ret) {
		pr_int("Error: Failed to send read file request, error code: %d\n",
		       ret);
		ctx->state = ISOBUSFS_CLI_GET_STATE_ERROR;
		return;
	}

	ctx->state = ISOBUSFS_CLI_GET_STATE_READ_FILE_SENT;
}

static void
isobusfs_cli_get_handle_read_file_sent(struct isobusfs_priv *priv,
				       struct isobusfs_cli_get_context *ctx,
				       struct isobusfs_msg *msg)
{
	isobusfs_event_callback cb = isobusfs_cli_get_event_callback;
	struct isobusfs_read_file_response *res =
		(struct isobusfs_read_file_response *)msg->buf;
	size_t bytes_read, bytes_written;
	uint8_t position_mode;
	int ret;

	if (isobusfs_cli_int_is_error(priv, 0, res->error_code, res->tan)) {
		pr_int("Error: Failed to read file from server, error code: %d\n",
		       res->error_code);
		ctx->state = ISOBUSFS_CLI_GET_STATE_ERROR;
		return;
	}

	/* Write the received data to the local file */
	bytes_read = le16toh(res->count);
	bytes_written = fwrite(res->data, 1, bytes_read, ctx->local_file);
	if (bytes_written != bytes_read) {
		pr_int("Error: Failed to write data to local file.\n");
		ctx->state = ISOBUSFS_CLI_GET_STATE_ERROR;
		return;
	}

	ctx->bytes_received += bytes_written;
	ctx->offset += bytes_written;

	/* Check if the end of the file has been reached */
	if (res->error_code == ISOBUSFS_ERR_END_OF_FILE) {
		/* Send a close file request */
		ret = isobusfs_cli_send_and_register_fa_cf_event(priv,
								 ctx->handle,
								 cb, ctx);
		if (ret) {
			pr_int("Error: Failed to send close file request, error code: %d\n",
			       ret);
			ctx->state = ISOBUSFS_CLI_GET_STATE_ERROR;
			return;
		}
		ctx->state = ISOBUSFS_CLI_GET_STATE_CLOSE_FILE_SENT;
		return;
	}

	position_mode = ISOBUSFS_FA_SEEK_SET;
	/* If more data is available, send a new seek request */

	ret = isobusfs_cli_send_and_register_fa_sf_event(priv, ctx->handle,
							 position_mode,
							 ctx->offset, cb, ctx);
	if (ret) {
		pr_int("Error: Failed to send next seek request, error code: %d\n",
		       ret);
		ctx->state = ISOBUSFS_CLI_GET_STATE_ERROR;
		return;
	}

	ctx->state = ISOBUSFS_CLI_GET_STATE_SEEK_FILE_SENT;
}

static void
isobusfs_cli_get_handle_close_file_sent(struct isobusfs_priv *priv,
					struct isobusfs_cli_get_context *ctx,
					struct isobusfs_msg *msg)
{
	struct isobusfs_close_file_res *res =
		(struct isobusfs_close_file_res *)msg->buf;

	if (isobusfs_cli_int_is_error(priv, 0, res->error_code, res->tan)) {
		pr_int("Error: Failed to close file on server, error code: %d\n",
		       res->error_code);
		ctx->state = ISOBUSFS_CLI_GET_STATE_ERROR;
	} else {
		pr_int("File closed successfully.\n");
		ctx->state = ISOBUSFS_CLI_GET_STATE_COMPLETED;
	}
}

static void isobusfs_cli_get_free_ctx(struct isobusfs_cli_get_context *ctx)
{
	if (ctx->local_file)
		fclose(ctx->local_file);

	free(ctx);
}

static void
isobusfs_cli_process_get_command(struct isobusfs_priv *priv,
				 struct isobusfs_cli_get_context *ctx,
				 struct isobusfs_msg *msg)
{
	switch (ctx->state) {
	case ISOBUSFS_CLI_GET_STATE_START:
		isobusfs_cli_get_handle_send_open_file(priv, ctx);
		break;
	case ISOBUSFS_CLI_GET_STATE_OPEN_FILE_SENT:
		isobusfs_cli_get_handle_open_file_sent(priv, ctx, msg);
		break;
	case ISOBUSFS_CLI_GET_STATE_SEEK_FILE_SENT:
		isobusfs_cli_get_handle_seek_file_sent(priv, ctx, msg);
		break;
	case ISOBUSFS_CLI_GET_STATE_READ_FILE_SENT:
		isobusfs_cli_get_handle_read_file_sent(priv, ctx, msg);
		break;
	case ISOBUSFS_CLI_GET_STATE_CLOSE_FILE_SENT:
		isobusfs_cli_get_handle_close_file_sent(priv, ctx, msg);
		break;
	default:
		pr_int("Error: Unexpected state in get command processing: %d\n",
		       ctx->state);
		break;
	}

	if (ctx->state == ISOBUSFS_CLI_GET_STATE_COMPLETED ||
	    ctx->state == ISOBUSFS_CLI_GET_STATE_ERROR) {
		if (ctx->state != ISOBUSFS_CLI_GET_STATE_COMPLETED) {
			/* Try to close handle and do not wait for response. */
			isobusfs_cli_send_and_register_fa_cf_event(priv,
								   ctx->handle,
								   NULL, NULL);
		}

		pr_int("File transfer %s.\n",
		       ctx->state == ISOBUSFS_CLI_GET_STATE_COMPLETED ?
		       "completed" : "failed");
		priv->int_busy = false;
		isobusfs_cli_get_free_ctx(ctx);
		isobusfs_cli_promt(priv);
	}
}

static int isobusfs_cli_get_event_callback(struct isobusfs_priv *priv,
					   struct isobusfs_msg *msg,
					   void *context, int error)
{
	struct isobusfs_cli_get_context *ctx =
		(struct isobusfs_cli_get_context *)context;

	if (error) {
		pr_int("Error in get event callback: %d\n", error);
		ctx->state = ISOBUSFS_CLI_GET_STATE_ERROR;
		isobusfs_cli_process_get_command(priv, ctx, NULL);
		return error;
	}

	isobusfs_cli_process_get_command(priv, ctx, msg);

	return 0;
}

static int cmd_get(struct isobusfs_priv *priv, const char *options)
{
	struct isobusfs_cli_get_context *ctx;
	const char *remote_path = NULL;
	char *local_path = NULL;
	char *options_copy, *opt;

	if (!options) {
		pr_int("Usage: get <remote_path> [local_path]\n");
		return -EINVAL;
	}

	options_copy = strdup(options);
	if (!options_copy) {
		pr_int("Error: Unable to allocate memory for options processing.\n");
		return -ENOMEM;
	}

	opt = strtok(options_copy, " ");
	if (opt) {
		remote_path = opt;
		opt = strtok(NULL, " ");
		if (opt)
			local_path = strdup(opt);
	}

	if (!remote_path) {
		pr_int("Error: Invalid arguments. Usage: get <remote_path> [local_path]\n");
		free(options_copy);
		return -EINVAL;
	}

	if (!local_path) {
		const char *filename = strrchr(remote_path, '/');

		filename = filename ? filename + 1 : remote_path;
		local_path = strdup(filename);
		if (!local_path) {
			pr_int("Error: Unable to allocate memory for local path.\n");
			free(options_copy);
			return -ENOMEM;
		}
	}

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		pr_int("Error: Unable to allocate memory for get context.\n");
		free(local_path);
		free(options_copy);
		return -ENOMEM;
	}

	ctx->remote_path = strdup(remote_path);
	if (!ctx->remote_path) {
		pr_int("Error: Unable to allocate memory for remote path.\n");
		free(ctx);
		free(local_path);
		free(options_copy);
		return -ENOMEM;
	}

	ctx->local_path = local_path;

	ctx->local_file = fopen(ctx->local_path, "wb");
	if (!ctx->local_file) {
		int ret = -errno;

		pr_int("Error: Unable to open local file for writing. %s\n",
		       strerror(errno));
		free(ctx->remote_path);
		free(ctx->local_path);
		free(ctx);
		free(options_copy);
		return ret;
	}

	priv->int_busy = true;
	ctx->state = ISOBUSFS_CLI_GET_STATE_START;
	isobusfs_cli_process_get_command(priv, ctx, NULL);

	free(options_copy);
	return 0;
}

/* ------ ls command -------*/
enum isobusfs_cli_ls_state {
	ISOBUSFS_CLI_LS_STATE_START,
	ISOBUSFS_CLI_LS_STATE_OPEN_DIR_SENT,
	ISOBUSFS_CLI_LS_STATE_SEEK_DIR_SENT,
	ISOBUSFS_CLI_LS_STATE_READ_DIR_SENT,
	ISOBUSFS_CLI_LS_STATE_CLOSE_DIR_SENT,
	ISOBUSFS_CLI_LS_STATE_COMPLETED,
	ISOBUSFS_CLI_LS_STATE_ERROR
};

struct isobusfs_cli_ls_context {
	enum isobusfs_cli_ls_state state;
	int handle;
	size_t offset;
	char *path;
	bool long_format;
	size_t entry_count;
	size_t request_count;
};

static int isobusfs_cli_ls_event_callback(struct isobusfs_priv *priv,
					   struct isobusfs_msg *msg,
					   void *context, int error);

static void isobusfs_cli_ls_free_ctx(struct isobusfs_cli_ls_context *ctx)
{
	free(ctx->path);
	free(ctx);
}

static void
isobusfs_cli_ls_handle_send_open_dir(struct isobusfs_priv *priv,
				     struct isobusfs_cli_ls_context *ctx)

{
	isobusfs_event_callback cb = isobusfs_cli_ls_event_callback;
	uint8_t flags = ISOBUSFS_FA_OPEN_DIR;
	int ret;

	ret = isobusfs_cli_send_and_register_fa_of_event(priv,
							 ctx->path,
							 strlen(ctx->path),
							 flags, cb, ctx);
	if (ret) {
		pr_int("Error: Unable to send open dir command.\n");
		ctx->state = ISOBUSFS_CLI_LS_STATE_ERROR;
		return;
	}

	ctx->state = ISOBUSFS_CLI_LS_STATE_OPEN_DIR_SENT;
}

static void
isobusfs_cli_ls_handle_open_dir_sent(struct isobusfs_priv *priv,
				     struct isobusfs_cli_ls_context *ctx,
				     struct isobusfs_msg *msg)
{
	isobusfs_event_callback cb = isobusfs_cli_ls_event_callback;
	struct isobusfs_fa_openf_res *res =
		(struct isobusfs_fa_openf_res *)msg->buf;
	int ret;

	if (isobusfs_cli_int_is_error(priv, 0, res->error_code, res->tan))
		goto error;
	else if (res->handle == ISOBUSFS_FILE_HANDLE_ERROR)
		goto error;

	pr_debug("< rx: Open File Response. Error code: %i",
		 res->error_code);

	ctx->handle = res->handle;

	ret = isobusfs_cli_send_and_register_fa_sf_event(priv, ctx->handle,
							 0, ctx->entry_count,
							 cb, ctx);
	if (ret)
		pr_int("Failed to send seek file request: %i\n", ret);

	ctx->state = ISOBUSFS_CLI_LS_STATE_SEEK_DIR_SENT;

	return;
error:
	ctx->state = ISOBUSFS_CLI_LS_STATE_ERROR;
}

static void
isobusfs_cli_ls_handle_seek_dir_sent(struct isobusfs_priv *priv,
				     struct isobusfs_cli_ls_context *ctx,
				     struct isobusfs_msg *msg)
{
	isobusfs_event_callback cb = isobusfs_cli_ls_event_callback;
	struct isobusfs_fa_seekf_res *res =
		(struct isobusfs_fa_seekf_res *)msg;
	uint16_t count;
	int ret;

	if (isobusfs_cli_int_is_error(priv, 0, res->error_code, res->tan))
		goto error;

	if (res->position != ctx->offset) {
		pr_int("Failed to seek to position %zu, got %zu\n",
		       ctx->offset, res->position);
		goto error;
	}

	/* set max possible number fitting in to 16bits */
	count = UINT16_MAX;
	ctx->request_count = count;

	ret = isobusfs_cli_send_and_register_fa_rf_event(priv, ctx->handle,
							 count, cb, ctx);
	if (ret) {
		pr_int("Failed to send read file request: %i\n", ret);
		goto error;
	}

	ctx->state = ISOBUSFS_CLI_LS_STATE_READ_DIR_SENT;

	return;
error:
	ctx->state = ISOBUSFS_CLI_LS_STATE_ERROR;
}

/**
 * Convert a 16-bit encoded date to a formatted date string ("YYYY-MM-DD").
 *
 * @param encoded_date The 16-bit encoded date.
 * @param formatted_date Buffer to store the formatted date string.
 */
static void convert_to_formatted_date(uint16_t encoded_date,
				      char *formatted_date)
{
	int year, month, day;

	if (!formatted_date)
		return;

	/* Extract year, month, and day from the encoded date */
	year = ((encoded_date >> 9) & 0x7f) + 1980;	/* Bits 15 … 9 */
	month = (encoded_date >> 5) & 0x0f;		/* Bits 8 … 5 */
	day = encoded_date & 0x1f;			/* Bits 4 … 0 */

	snprintf(formatted_date, 11, "%04d-%02d-%02d", year, month, day);
}

/**
 * Convert a 16-bit encoded time to a formatted time string ("HH:MM:SS").
 *
 * @param encoded_time The 16-bit encoded time.
 * @param formatted_time Buffer to store the formatted time string.
 */
static void convert_to_formatted_time(uint16_t encoded_time,
				      char *formatted_time)
{
	int hours, minutes, seconds;

	if (!formatted_time)
		return;

	/* Extract hours, minutes, and seconds from the encoded time */
	/* Bits 15 … 11 */
	hours = (encoded_time >> 11) & 0x1f;
	/* Bits 10 … 5 */
	minutes = (encoded_time >> 5) & 0x3f;
	/* Bits 4 … 0, in steps of 2 seconds */
	seconds = (encoded_time & 0x1f) * 2;

	snprintf(formatted_time, 9, "%02d:%02d:%02d", hours, minutes, seconds);
}

static bool isobusfs_cli_extract_directory_entry(const uint8_t *buffer,
						 size_t buffer_length,
						 size_t *pos, char *filename,
						 uint8_t *attributes,
						 uint16_t *file_date,
						 uint16_t *file_time,
						 uint32_t *file_size)
{
	uint8_t filename_length;
	size_t entry_total_len;

	if (*pos + 2 > buffer_length) {
		pr_int("Error: Incomplete data in buffer\n");
		return false;
	}

	filename_length = buffer[*pos];
	entry_total_len = 1 + filename_length + 1 + 2 + 2 + 4;

	if (*pos + entry_total_len > buffer_length) {
		pr_int("Error: Incomplete data in buffer\n");
		return false;
	}

	(*pos)++;
	strncpy(filename, (const char *)buffer + *pos, filename_length);
	filename[filename_length] = '\0';
	*pos += filename_length;
	if (filename_length > MAX_DISPLAY_FILENAME_LENGTH) {
		/* Truncate the filename and replace the last character
		 * with a dots
		 */
		filename[MAX_DISPLAY_FILENAME_LENGTH] = '\0';
		filename[MAX_DISPLAY_FILENAME_LENGTH - 1] = '.';
		filename[MAX_DISPLAY_FILENAME_LENGTH - 2] = '.';
	}

	*attributes = buffer[*pos];
	(*pos)++;

	*file_date = le16toh(*(__le16 *)(buffer + *pos));
	*pos += 2;
	*file_time = le16toh(*(__le16 *)(buffer + *pos));
	*pos += 2;
	*file_size = le32toh(*(__le32 *)(buffer + *pos));
	*pos += 4;

	return true;
}

static void
isobusfs_cli_print_directory_entry(struct isobusfs_cli_ls_context *ctx,
				   const char *filename, uint8_t attributes,
				   uint16_t file_date, uint16_t file_time,
				   uint32_t file_size)
{
	char formatted_date[] = "YYYY-MM-DD\0";
	char formatted_time[] = "HH:MM:SS\0";
	char file_type, writeable;


	if (!ctx->long_format) {
		pr_int("%s\n", filename);
		return;
	}

	file_type = (attributes & ISOBUSFS_ATTR_DIRECTORY) ? 'd' : '-';
	writeable = (attributes & ISOBUSFS_ATTR_READ_ONLY) ? '-' : 'w';

	convert_to_formatted_date(file_date, formatted_date);
	convert_to_formatted_time(file_time, formatted_time);

	pr_int("%c%c%c  %u  %s  %s  %s\n",
		   file_type, 'r', writeable,
		   file_size, formatted_date, formatted_time, filename);
}

static void
isobusfs_cli_print_directory_entries(struct isobusfs_cli_ls_context *ctx,
				     const uint8_t *buffer,
				     size_t buffer_length)
{
	char filename[ISOBUSFS_MAX_DIR_ENTRY_NAME_LENGTH + 1];
	uint16_t file_date, file_time;
	uint32_t file_size;
	uint8_t attributes;
	size_t pos = 0;

	while (pos < buffer_length) {
		if (!isobusfs_cli_extract_directory_entry(buffer, buffer_length,
							  &pos, filename,
							  &attributes,
							  &file_date,
							  &file_time,
							  &file_size))
			return;

		isobusfs_cli_print_directory_entry(ctx, filename, attributes,
						   file_date, file_time,
						   file_size);
		ctx->entry_count++;
	}
}

static void
isobusfs_cli_ls_handle_read_dir_sent(struct isobusfs_priv *priv,
				     struct isobusfs_cli_ls_context *ctx,
				     struct isobusfs_msg *msg)
{
	struct isobusfs_read_file_response *res =
		(struct isobusfs_read_file_response *)msg->buf;
	size_t buffer_length = msg->len - sizeof(*res);
	isobusfs_event_callback cb;
	uint16_t count;
	int ret;

	pr_debug("< rx: Read File Response. Error code: %i", res->error_code);
	if (isobusfs_cli_int_is_error(priv, 0, res->error_code, res->tan))
		goto error;

	count = le16toh(res->count);
	if (count && count != buffer_length) {
		pr_int("Buffer length mismatch: %u != %zu\n", count,
		       buffer_length);
		goto error;
	}

	if (count)
		isobusfs_cli_print_directory_entries(ctx, res->data,
						     buffer_length);

	cb = isobusfs_cli_ls_event_callback;
	if (count) {
		ret = isobusfs_cli_send_and_register_fa_cf_event(priv,
								 ctx->handle,
								 cb, ctx);
		if (ret) {
			pr_int("Failed to send close file request: %i\n", ret);
			goto error;
		}

		ctx->state = ISOBUSFS_CLI_LS_STATE_CLOSE_DIR_SENT;
	} else {
		ctx->offset = ctx->entry_count;
		ret = isobusfs_cli_send_and_register_fa_sf_event(priv,
								 ctx->handle, 0,
								 ctx->offset,
								 cb, ctx);
		if (ret)
			pr_int("Failed to send seek file request: %i\n", ret);

		ctx->state = ISOBUSFS_CLI_LS_STATE_SEEK_DIR_SENT;
	}

	return;
error:

	ctx->state = ISOBUSFS_CLI_LS_STATE_ERROR;
}

static void
isobusfs_cli_ls_handle_close_dir_sent(struct isobusfs_priv *priv,
				      struct isobusfs_cli_ls_context *ctx,
				      struct isobusfs_msg *msg)
{
	struct isobusfs_close_file_res *res =
		(struct isobusfs_close_file_res *)msg->buf;

	if (isobusfs_cli_int_is_error(priv, 0, res->error_code, res->tan))
		goto error;

	pr_debug("< rx: Close File Response. Error code: %i",
		 res->error_code);

	return;
error:
	ctx->state = ISOBUSFS_CLI_LS_STATE_ERROR;
}

static void isobusfs_cli_process_ls_command(struct isobusfs_priv *priv,
					    struct isobusfs_cli_ls_context *ctx,
					    struct isobusfs_msg *msg)
{
	switch (ctx->state) {
	case ISOBUSFS_CLI_LS_STATE_START:
		isobusfs_cli_ls_handle_send_open_dir(priv, ctx);
		break;
	case ISOBUSFS_CLI_LS_STATE_OPEN_DIR_SENT:
		isobusfs_cli_ls_handle_open_dir_sent(priv, ctx, msg);
		break;
	case ISOBUSFS_CLI_LS_STATE_SEEK_DIR_SENT:
		isobusfs_cli_ls_handle_seek_dir_sent(priv, ctx, msg);
		break;
	case ISOBUSFS_CLI_LS_STATE_READ_DIR_SENT:
		isobusfs_cli_ls_handle_read_dir_sent(priv, ctx, msg);
		break;
	case ISOBUSFS_CLI_LS_STATE_CLOSE_DIR_SENT:
		isobusfs_cli_ls_handle_close_dir_sent(priv, ctx, msg);
		ctx->state = ISOBUSFS_CLI_LS_STATE_COMPLETED;
		break;
	default:
		pr_int("Unexpected state: %i\n", ctx->state);
		break;
	}

	if (ctx->state == ISOBUSFS_CLI_LS_STATE_COMPLETED ||
	    ctx->state == ISOBUSFS_CLI_LS_STATE_ERROR) {
		if (ctx->state != ISOBUSFS_CLI_LS_STATE_COMPLETED) {
			/* Try to close handle and do not wait for response. */
			isobusfs_cli_send_and_register_fa_cf_event(priv,
					      ctx->handle, NULL, NULL);
		}

		pr_int("Entries found: %i\n", ctx->entry_count);
		priv->int_busy = false;
		isobusfs_cli_ls_free_ctx(ctx);
		isobusfs_cli_promt(priv);
	}
}

static int isobusfs_cli_ls_event_callback(struct isobusfs_priv *priv,
					  struct isobusfs_msg *msg,
					  void *context, int error)
{
	struct isobusfs_cli_ls_context *ctx =
		(struct isobusfs_cli_ls_context *)context;

	if (!error) {
		isobusfs_cli_process_ls_command(priv, ctx, msg);
	} else {
		ctx->state = ISOBUSFS_CLI_LS_STATE_ERROR;
		isobusfs_cli_process_ls_command(priv, ctx, NULL);
	}

	return 0;
}

static int cmd_ls(struct isobusfs_priv *priv, const char *options)
{
	struct isobusfs_cli_ls_context *ctx;
	bool long_format = false;
	char *options_copy, *opt;
	const char *path = ".";

	if (options) {
		options_copy = strdup(options);
		if (!options_copy) {
			pr_int("Error: Unable to allocate memory for options processing.\n");
			return -ENOMEM;
		}

		opt = strtok(options_copy, " ");
		while (opt != NULL) {
			if (strcmp(opt, "-h") == 0) {
				pr_int("Usage: ls [-t] [path]\n");
				pr_int("Options:\n");
				pr_int("  -l\tuse a long listing format\n");
				pr_int("  path\tDirectory to list\n");
				free(options_copy);
				return 0;
			} else if (strcmp(opt, "-l") == 0) {
				long_format = true;
			} else {
				/* Assume any non-option argument is the path */
				path = opt;
			}
			opt = strtok(NULL, " ");
		}
		free(options_copy);
	}

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		pr_int("Error: Unable to allocate memory for ls context.\n");
		return -ENOMEM;
	}

	ctx->path = strdup(path);
	if (!ctx->path) {
		free(ctx);
		pr_int("Error: Unable to allocate memory for path.\n");
		return -ENOMEM;
	}

	priv->int_busy = true;
	ctx->long_format = long_format;
	ctx->state = ISOBUSFS_CLI_LS_STATE_START;
	isobusfs_cli_process_ls_command(priv, ctx, NULL);

	return 0;
}

static int cmd_ll(struct isobusfs_priv *priv, const char *options)
{
	char *options_copy = NULL;

	if (!options)
		return cmd_ls(priv, "-l");

	int ret;

	options_copy = strdup(options);
	if (!options_copy) {
		pr_int("Error: Unable to allocate memory for options processing.\n");
		return -1;
	}

	ret = cmd_ls(priv, strcat(options_copy, " -l"));
	free(options_copy);
	return ret;
}

static int isobusfs_cli_int_cd_state(struct isobusfs_priv *priv,
				     struct isobusfs_msg *msg,
				     void *ctx, int error)
{
	struct isobusfs_dh_ccd_res *res =
		(struct isobusfs_dh_ccd_res *)msg->buf;

	if (!isobusfs_cli_int_is_error(priv, error, res->error_code, res->tan))
		pr_debug("< rx: change current directory response. Error code: %i",
			 res->error_code);

	priv->int_busy = false;
	isobusfs_cli_promt(priv);

	return 0;
}

static int cmd_cd(struct isobusfs_priv *priv, const char *options)
{
	char *options_copy = NULL;
	const char *path = "."; /* Default path is the current directory */
	char *opt;
	int ret;

	if (!options)
		goto send_ccd_req;

	options_copy = strdup(options);
	if (!options_copy) {
		pr_int("Error: Unable to allocate memory for options processing.\n");
		return -ENOMEM;
	}

	opt = strtok(options_copy, " ");
	while (opt) {
		if (strcmp(options, "-h") == 0) {
			pr_int("Usage: cd [path]\n");
			pr_int("Options:\n");
			pr_int("  path\tPath of new directory\n");
			return 0;
		}

		/* Assume any non-option argument is the path */
		path = opt;
		opt = strtok(NULL, " ");
	}

send_ccd_req:
	ret = isobusfs_cli_send_and_register_ccd_event(priv, path, strlen(path),
						       isobusfs_cli_int_cd_state,
						       NULL);
	if (ret) {
		pr_int("Error: Unable to send CCD request.\n");
		goto free_options;
	}
	priv->int_busy = true;

free_options:
	free(options_copy);
	return 0;
}

static int isobusfs_cli_int_pwd_state(struct isobusfs_priv *priv,
				      struct isobusfs_msg *msg,
				      void *ctx, int error)
{
	struct isobusfs_dh_get_cd_res *res =
		(struct isobusfs_dh_get_cd_res *)msg->buf;
	char str[ISOBUSFS_MAX_PATH_NAME_LENGTH];
	uint16_t str_len;

	if (isobusfs_cli_int_is_error(priv, error, res->error_code, res->tan))
		goto error;

	str_len = le16toh(res->name_len);
	if (str_len > ISOBUSFS_MAX_PATH_NAME_LENGTH) {
		pr_int("path name too long: %i, max is %i", str_len,
			ISOBUSFS_MAX_PATH_NAME_LENGTH);
		str_len = ISOBUSFS_MAX_PATH_NAME_LENGTH;

	}
	strncpy(str, (const char *)&res->name[0], str_len);

	priv->int_busy = false;
	pr_int("%s\n", str);
error:
	isobusfs_cli_promt(priv);

	return 0;
}

static int cmd_pwd(struct isobusfs_priv *priv, const char *options)
{
	int ret;

	ret = isobusfs_cli_send_and_register_gcd_event(priv,
				       isobusfs_cli_int_pwd_state, NULL);
	if (ret) {
		pr_int("Error: Unable to send Get Current Dir request.\n");
		return ret;
	}
	priv->int_busy = true;

	return 0;
}

struct command_mapping commands[] = {
	{"exit", cmd_exit, "exit interactive mode"},
	{"quit", cmd_exit, "exit interactive mode"},
	{"help", cmd_help, "show this help"},
	{"dmesg", cmd_dmesg, "show log buffer"},
	{"selftest", cmd_selftest, "run selftest"},
	{"ls", cmd_ls, "list directory"},
	{"ll", cmd_ll, "list directory with long listing format"},
	{"cd", cmd_cd, "change directory"},
	{"pwd", cmd_pwd, "print name of current/working directory"},
	{"get", cmd_get, "get file"},
	{NULL, NULL, NULL}
};

void isobusfs_cli_int_start(struct isobusfs_priv *priv)
{
	pr_int("Interactive mode\n");
	isobusfs_cli_promt(priv);
}

int isobusfs_cli_interactive(struct isobusfs_priv *priv)
{
	char command[MAX_COMMAND_LENGTH];
	ssize_t len;
	int ret;

	len = read(STDIN_FILENO, command, MAX_COMMAND_LENGTH);
	if (len == 1) {
		isobusfs_cli_promt(priv);
		return 0;
	} else if (len > 0) {
		char *cmd, *options;

		if (command[len - 1] == '\n')
			command[len - 1] = '\0';
		else
			command[len] = '\0';

		cmd = strtok(command, " ");
		options = strtok(NULL, "\0");

		for (int i = 0; commands[i].command != NULL; i++) {
			if (strcmp(cmd, commands[i].command) == 0) {
				ret = commands[i].function(priv, options);
				if (ret)
					return ret;
				isobusfs_cli_promt(priv);
				return 0;
			}
		}

		pr_int("unknown comand\n");
		isobusfs_cli_promt(priv);
	} else {
		if (errno != EAGAIN && errno != EWOULDBLOCK)
			pr_int("read error\n");
	}

	return 0;
}
