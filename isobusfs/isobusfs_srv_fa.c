// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "isobusfs_srv.h"
#include "isobusfs_cmn_fa.h"

static struct isobusfs_srv_handles *
isobusfs_srv_walk_handles(struct isobusfs_srv_priv *priv, const char *path)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(priv->handles); i++) {
		if (priv->handles[i].path == NULL)
			continue;

		if (!strcmp(priv->handles[i].path, path))
			return &priv->handles[i];
	}

	return NULL;
}

static int isobusfs_srv_add_file(struct isobusfs_srv_priv *priv,
				 const char *path, int fd, DIR *dir)
{
	int j;

	if (priv->handles_count >= ARRAY_SIZE(priv->handles)) {
		pr_err("too many handles");
		return -ENOSPC;
	}

	for (j = 0; j < ARRAY_SIZE(priv->handles); j++) {
		if (priv->handles[j].path == NULL)
			break;
	}

	priv->handles[j].path = strdup(path);
	priv->handles[j].fd = fd;
	priv->handles[j].dir = dir;

	priv->handles_count++;
	return j;
}

static int isobusfs_srv_add_client_to_file(struct isobusfs_srv_handles *file,
					   struct isobusfs_srv_client *client)
{
	int j;

	for (j = 0; j < ARRAY_SIZE(file->clients); j++) {
		if (file->clients[j] == client)
			return 0;
	}

	for (j = 0; j < ARRAY_SIZE(file->clients); j++) {
		if (file->clients[j] == NULL) {
			file->clients[j] = client;
			file->refcount++;
			return 0;
		}
	}

	pr_err("%s: can't add client to file", __func__);
	return -ENOENT;
}

static int isobusfs_srv_request_file(struct isobusfs_srv_priv *priv,
				     struct isobusfs_srv_client *client,
				     const char *path, int fd, DIR *dir)
{
	struct isobusfs_srv_handles *file;
	int handle, ret;

	file = isobusfs_srv_walk_handles(priv, path);
	if (!file) {
		handle = isobusfs_srv_add_file(priv, path, fd, dir);
		if (handle < 0)
			return handle;

		file = &priv->handles[handle];
	} else {
		handle = file - priv->handles;
	}

	ret = isobusfs_srv_add_client_to_file(file, client);
	if (ret < 0)
		return ret;

	return handle;
}

static struct isobusfs_srv_handles *
isobusfs_srv_get_handle(struct isobusfs_srv_priv *priv, int handle)
{
	if (handle < 0 || handle >= ARRAY_SIZE(priv->handles))
		return NULL;

	return &priv->handles[handle];
}

static int isobusfs_srv_release_handle(struct isobusfs_srv_priv *priv,
				       struct isobusfs_srv_client *client,
				       int handle)
{
	struct isobusfs_srv_handles *hdl = isobusfs_srv_get_handle(priv, handle);
	int client_index;

	if (!hdl) {
		pr_warn("%s: invalid handle %d", __func__, handle);
		return -ENOENT;
	}

	/* Find the client in the hdl's client list and remove it */
	for (client_index = 0; client_index < ARRAY_SIZE(hdl->clients); client_index++) {
		if (hdl->clients[client_index] == client) {
			hdl->clients[client_index] = NULL;
			hdl->refcount--;

			pr_debug("%s: client %p removed from handle %d", __func__, client, handle);
			/* If refcount is 0, close the hdl and remove it from the list */
			if (hdl->refcount == 0) {
				pr_debug("%s: closing handle %d", __func__, handle);
				/* fd will be automatically closed when
				 * closedir(3) is called.
				 */
				if (hdl->dir)
					closedir(hdl->dir);
				else
					close(hdl->fd);
				memset(hdl, 0, sizeof(*hdl));
				priv->handles_count--;
			}

			return 0;
		}
	}

	pr_err("%s: client %p not found in handle %d", __func__, client, handle);
	return -ENOENT;
}

void isobusfs_srv_remove_client_from_handles(struct isobusfs_srv_priv *priv,
					   struct isobusfs_srv_client *client)
{
	int handle;
	int client_index;

	for (handle = 0; handle < ARRAY_SIZE(priv->handles); handle++) {
		struct isobusfs_srv_handles *hdl = &priv->handles[handle];

		if (hdl->path == NULL)
			continue;

		for (client_index = 0; client_index < ARRAY_SIZE(hdl->clients); client_index++) {
			if (hdl->clients[client_index] == client) {
				hdl->clients[client_index] = NULL;
				hdl->refcount--;

				if (hdl->refcount == 0) {
					close(hdl->fd);
					memset(hdl, 0, sizeof(*hdl));
					priv->handles_count--;
				}

				break;
			}
		}
	}
}

static int isobusfs_srv_fa_open_directory(struct isobusfs_srv_priv *priv,
					  struct isobusfs_srv_client *client,
					  const char *path, size_t path_len,
					  uint8_t *handle)
{
	char linux_path[ISOBUSFS_SRV_MAX_PATH_LEN];
	struct isobusfs_srv_handles *hdl;
	struct stat file_stat;
	int file_index, fd;
	DIR *dir;
	int ret;

	ret = isobusfs_path_to_linux_path(priv, path, path_len, linux_path, sizeof(linux_path));
	if (ret < 0)
		return ret;

	hdl = isobusfs_srv_walk_handles(priv, linux_path);
	if (hdl) {
		pr_err("%s: Path %s is already opened\n", __func__, linux_path);
		return ISOBUSFS_ERR_OTHER;
	}

	dir = opendir(linux_path);
	if (!dir) {
		pr_err("%s: Error opening directory %s. Error %d (%s)\n",
		       __func__, linux_path, errno, strerror(errno));
		switch (errno) {
		case EACCES:
			return ISOBUSFS_ERR_ACCESS_DENIED;
		case ENOENT:
			return ISOBUSFS_ERR_FILE_ORPATH_NOT_FOUND;
		case ENOMEM:
			return ISOBUSFS_ERR_OUT_OF_MEM;
		default:
			return ISOBUSFS_ERR_OTHER;
		}
	}

	fd = dirfd(dir);
	if (fd < 0) {
		pr_err("%s: Error getting file descriptor for directory %s. Error %d (%s)\n",
		       __func__, linux_path, errno, strerror(errno));
		closedir(dir);
		return ISOBUSFS_ERR_OTHER;
	}

	if (fstat(fd, &file_stat) < 0 || !S_ISDIR(file_stat.st_mode)) {
		pr_err("%s: Path %s is not a directory\n", __func__,
		       linux_path);
		closedir(dir);
		return ISOBUSFS_ERR_INVALID_ACCESS;
	}

	file_index = isobusfs_srv_request_file(priv, client, linux_path, fd,
					       dir);
	if (file_index < 0) {
		closedir(dir);
		return file_index;
	}

	*handle = (uint8_t)file_index;

	return 0;
}

static int isobusfs_srv_fa_open_file(struct isobusfs_srv_priv *priv,
				     struct isobusfs_srv_client *client,
				     const char *path, size_t path_len,
				     uint8_t flags, uint8_t *handle)
{
	char linux_path[ISOBUSFS_SRV_MAX_PATH_LEN];
	struct isobusfs_srv_handles *hdl;
	struct stat file_stat;
	int open_flags = 0;
	int file_index;
	int ret, fd;

	ret = isobusfs_path_to_linux_path(priv, path, path_len,
					  linux_path, sizeof(linux_path));
	if (ret < 0)
		return ret;

	pr_debug("convert ISOBUS FS path to linux path: %.*s -> %s",
		 path_len, path, linux_path);

	/* Determine open flags based on the requested access type */
	switch (flags & ISOBUSFS_FA_OPEN_MASK) {
	case ISOBUSFS_FA_OPEN_FILE_RO:
		open_flags |= O_RDONLY;
		break;
	case ISOBUSFS_FA_OPEN_FILE_WO:
		open_flags |= O_WRONLY;
		break;
	case ISOBUSFS_FA_OPEN_FILE_WR:
		open_flags |= O_RDWR;
		if (!(flags & ISOBUSFS_FA_OPEN_APPEND))
			open_flags |= O_TRUNC;
		break;
	default:
		return ISOBUSFS_ERR_INVALID_ACCESS;
	}

	if (flags & ISOBUSFS_FA_OPEN_APPEND)
		open_flags |= O_APPEND;

	/* Check if the file is already opened */
	hdl = isobusfs_srv_walk_handles(priv, linux_path);
	if (hdl) {
		pr_warn("Handle: %s is already opened by client: %x\n",
			linux_path, client->addr);
		fd = hdl->fd;
	} else {
		/* Open the file if not already opened */
		fd = open(linux_path, open_flags);
		if (fd < 0) {
			switch (errno) {
			case EACCES:
				return ISOBUSFS_ERR_ACCESS_DENIED;
			case EINVAL:
				return ISOBUSFS_ERR_INVALID_ACCESS;
			case EMFILE:
			case ENFILE:
				return ISOBUSFS_ERR_TOO_MANY_FILES_OPEN;
			case ENOENT:
				return ISOBUSFS_ERR_FILE_ORPATH_NOT_FOUND;
			case ENOMEM:
				return ISOBUSFS_ERR_OUT_OF_MEM;
			default:
				return ISOBUSFS_ERR_OTHER;
			}
		}
		/* Check if the opened path is a regular file */
		if (fstat(fd, &file_stat) < 0) {
			close(fd);
			return ISOBUSFS_ERR_OTHER;
		}

		if (!S_ISREG(file_stat.st_mode)) {
			close(fd);
			/* Invalid access (not a regular file) */
			return ISOBUSFS_ERR_INVALID_ACCESS;
		}
	}

	/* Request the file, which also handles refcount and client list 
	 * updates
	 */
	file_index = isobusfs_srv_request_file(priv, client, linux_path, fd,
					       NULL);
	if (file_index < 0) {
		close(fd);
		return file_index;
	}

	*handle = (uint8_t)file_index;

	return 0;
}

static int isobusfs_srv_fa_open_file_req(struct isobusfs_srv_priv *priv,
					 struct isobusfs_msg *msg)
{
	struct isobusfs_fa_openf_req *req =
		(struct isobusfs_fa_openf_req *)msg->buf;
	uint16_t name_len = le16toh(req->name_len);
	struct isobusfs_srv_client *client;
	struct isobusfs_fa_openf_res res;
	uint8_t error_code = 0;
	uint8_t access_type;
	size_t abs_path_len;
	char *abs_path;
	uint8_t handle;
	int ret = 0;

	client = isobusfs_srv_get_client_by_msg(priv, msg);
	if (!client) {
		pr_warn("client not found");
		error_code = ISOBUSFS_ERR_OTHER;
		goto send_response;
	}

	if (name_len > msg->len - sizeof(*req)) {
		error_code = ISOBUSFS_ERR_INVALID_ACCESS;
		goto send_response;
	}

	/* Perform checks on the received request, e.g., validate path length */
	if (name_len > ISOBUSFS_MAX_PATH_NAME_LENGTH) {
		error_code = ISOBUSFS_ERR_INVALID_ACCESS;
		goto send_response;
	}

	abs_path_len = ISOBUSFS_SRV_MAX_PATH_LEN;
	abs_path = malloc(abs_path_len);
	if (!abs_path) {
		pr_warn("failed to allocate memory");
		return -ENOMEM;
	}

	if (client->current_dir[0] == '\0')
		isobusfs_srv_set_default_current_dir(priv, client);

	/* Normalize provided string and convert it to absolute ISOBUS FS path */
	ret = isobusfs_convert_relative_to_absolute(priv, client->current_dir,
						    (char *)req->name, req->name_len,
						    abs_path, abs_path_len);
	if (ret < 0) {
		error_code = ISOBUSFS_ERR_FILE_ORPATH_NOT_FOUND;
		goto send_response;
	}

	pr_debug("< rx: Open File Request. from client 0x%2x: %.*s. Current directory: %s",
		 client->addr, req->name_len, req->name, client->current_dir);

	access_type = FIELD_GET(ISOBUSFS_FA_OPEN_MASK, req->flags);
	if (access_type == ISOBUSFS_FA_OPEN_DIR) {
		error_code = isobusfs_srv_fa_open_directory(priv, client, abs_path,
							    abs_path_len, &handle);
	} else {
		error_code = isobusfs_srv_fa_open_file(priv, client, abs_path,
						       abs_path_len, req->flags,
						       &handle);
	}

send_response:
	res.fs_function =
		isobusfs_cg_function_to_buf(ISOBUSFS_CG_FILE_ACCESS,
					    ISOBUSFS_FA_F_OPEN_FILE_RES);
	res.tan = req->tan;
	res.error_code = error_code;
	res.handle = handle;
	memset(&res.reserved[0], 0xff, sizeof(res.reserved));

	/* send to socket */
	ret = isobusfs_srv_sendto(priv, msg, &res, sizeof(res));
	if (ret < 0) {
		pr_warn("can't send current directory response");
		goto err;
	}

	pr_debug("> tx: Open File Response. Error code: %d (%s).", error_code,
		 isobusfs_error_to_str(error_code));
err:

	return ret;
}

static int isobusfs_srv_read_file(struct isobusfs_srv_handles *handle,
				  uint8_t *buffer, size_t count,
				  ssize_t *readed_size)
{
	*readed_size = read(handle->fd, buffer, count);
	if (*readed_size < 0) {
		switch (errno) {
		case EBADF:
			return ISOBUSFS_ERR_INVALID_HANDLE;
		case EFAULT:
			return ISOBUSFS_ERR_OUT_OF_MEM;
		case EIO:
			return ISOBUSFS_ERR_ON_READ;
		default:
			return ISOBUSFS_ERR_OTHER;
		}
	}

	return 0;
}

static uint16_t convert_to_file_date(time_t time_val)
{
	struct tm *timeinfo = localtime(&time_val);
	int year, month, day;

	if (!timeinfo)
		return 0;

	year = timeinfo->tm_year + 1900 - 1980;
	month = timeinfo->tm_mon + 1;
	day = timeinfo->tm_mday;

	if (year < 0 || year > 127)
		return 0;

	return (year << 9) | (month << 5) | day;
}

static uint16_t convert_to_file_time(time_t time_val)
{
	struct tm *timeinfo = localtime(&time_val);
	int hours, minutes, seconds;
	uint16_t time;

	if (!timeinfo)
		return 0;

	hours = timeinfo->tm_hour;
	minutes = timeinfo->tm_min;
	seconds = timeinfo->tm_sec / 2;

	time = (hours << 11) | (minutes << 5) | seconds;

	return time;
}

static int check_access_with_base(const char *base_dir,
				  const char *relative_path, int mode)
{
	char full_path[ISOBUSFS_SRV_MAX_PATH_LEN];

	if (snprintf(full_path, sizeof(full_path), "%s/%s", base_dir,
		 relative_path) >= sizeof(full_path)) {
		return -ENAMETOOLONG;
	}

	return access(full_path, mode);
}

static int isobusfs_srv_read_directory(struct isobusfs_srv_handles *handle,
				       uint8_t *buffer, size_t count,
				       ssize_t *readed_size)
{
	DIR *dir = handle->dir;
	struct dirent *entry;
	size_t pos = 0;
	size_t entry_count = 0;


	/*
	 * Position the directory stream to the previously stored offset (handle->dir_pos).
	 *
	 * Handling Changes in Directory Contents:
	 * - If the directory contents change between reads (e.g., files/directories added or deleted),
	 *   handle->dir_pos may not point to the expected entry.
	 * - To ensure consistency, implement checks (e.g., compare inode numbers) to verify the correct
	 *   entry position.
	 *
	 * Detecting End of Directory:
	 * - If readdir() returns NULL before reaching handle->dir_pos, it indicates the end of the
	 *   directory with no more entries to read.
	 * - This situation should be handled appropriately, such as by resetting handle->dir_pos and
	 *   either returning an error or restarting from the beginning of the directory, depending
	 *   on the application's requirements.
	 */
	for (size_t i = 0; i < handle->dir_pos &&
	     (entry = readdir(dir)) != NULL; i++) {
		/* Iterating to the desired position */
	}

	/*
	 * Directory Entry Layout:
	 * This loop reads directory entries and encodes them into a buffer.
	 * Each entry in the buffer follows the format specified in ISO 11783-13:2021.
	 *
	 * The layout of each directory entry in the buffer is as follows:
	 * - Byte 1: Filename Length (as per ISO 11783-13:2021 B.22).
	 *   Represents the length of the filename that follows.
	 *
	 * - Byte 2–n: Filename (as per ISO 11783-13:2021 B.23).
	 *   The actual name of the file or directory.
	 *
	 * - Byte n + 1: Attributes (as per ISO 11783-13:2021 B.15).
	 *
	 * - Bytes n + 2, n + 3: File Date (as per ISO 11783-13:2021 B.24).
	 *   Encoded file date, using a 16-bit format derived from file_stat.st_mtime.
	 *
	 * - Bytes n + 4, n + 5: File Time (as per ISO 11783-13:2021 B.25).
	 *   Encoded file time, using a 16-bit format derived from file_stat.st_mtime.
	 *
	 * - Bytes n + 6 … n + 9: Size (as per ISO 11783-13:2021 B.26).
	 *   The size of the file in bytes, encoded in a 32-bit little-endian format.
	 *
	 * The handle->dir_pos is incremented after processing each entry, marking
	 * the current position in the directory stream for subsequent reads.
	 */
	while ((entry = readdir(dir)) != NULL) {
		size_t entry_name_len, entry_total_len;
		__le16 file_date, file_time;
		uint8_t attributes = 0;
		struct stat file_stat;
		__le32 size;

		if (check_access_with_base(handle->path, entry->d_name, R_OK) != 0)
			continue; /* Skip this entry if it's not readable */

		if (fstatat(handle->fd, entry->d_name, &file_stat, 0) < 0)
			continue; /* Skip this entry on error */

		entry_name_len = strlen(entry->d_name);
		if (entry_name_len > ISOBUSFS_MAX_DIR_ENTRY_NAME_LENGTH)
			continue;

		entry_total_len = 1 + entry_name_len + 1 + 2 + 2 + 4;

		if (pos + entry_total_len > count)
			break;

		buffer[pos++] = (uint8_t)entry_name_len;

		memcpy(buffer + pos, entry->d_name, entry_name_len);
		pos += entry_name_len;

		if (S_ISDIR(file_stat.st_mode))
			attributes |= ISOBUSFS_ATTR_DIRECTORY;
		if (check_access_with_base(handle->path, entry->d_name, W_OK) != 0)
			attributes |= ISOBUSFS_ATTR_READ_ONLY;
		buffer[pos++] = attributes;

		file_date = htole16(convert_to_file_date(file_stat.st_mtime));
		memcpy(buffer + pos, &file_date, sizeof(file_date));
		pos += sizeof(file_date);

		file_time = htole16(convert_to_file_time(file_stat.st_mtime));
		memcpy(buffer + pos, &file_time, sizeof(file_time));
		pos += sizeof(file_time);

		size = htole32(file_stat.st_size);
		memcpy(buffer + pos, &size, sizeof(size));
		pos += sizeof(size);

		entry_count++;
	}

	*readed_size = pos;
	return 0;
}

static int isobusfs_srv_fa_rf_req(struct isobusfs_srv_priv *priv,
				  struct isobusfs_msg *msg)
{
	uint8_t res_fail[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct isobusfs_read_file_response *res;
	struct isobusfs_srv_handles *handle;
	struct isobusfs_srv_client *client;
	struct isobusfs_fa_readf_req *req;
	ssize_t readed_size = 0;
	uint8_t error_code = 0;
	ssize_t send_size;
	int ret = 0;
	int count;

	req = (struct isobusfs_fa_readf_req *)msg->buf;
	count = le16toh(req->count);

	pr_debug("< rx: Read File Request. tan: %d, handle: %d, count: %d",
		 req->tan, req->handle, count);
	/* C.3.5.1 Read File, General:
	 * The requested data (excluding the other parameters) is sent in
	 * the response (up to 1 780 bytes when TP is used, up to 65 530 bytes
	 * when ETP is used). The number of data bytes read can be less than
	 * requested if the end of the file is reached.
	 * TODO: currently we are not able to detect support transport mode,
	 * so ETP is assumed.
	 */
	if (count > ISOBUSFS_MAX_DATA_LENGH)
		count = ISOBUSFS_MAX_DATA_LENGH;

	res = malloc(sizeof(*res) + count);
	if (!res) {
		pr_warn("failed to allocate memory");
		res = (struct isobusfs_read_file_response *)&res_fail[0];
		error_code = ISOBUSFS_ERR_OUT_OF_MEM;
		goto send_response;
	}

	client = isobusfs_srv_get_client_by_msg(priv, msg);
	if (!client) {
		pr_warn("client not found");
		error_code = ISOBUSFS_ERR_OTHER;
		goto send_response;
	}

	handle = isobusfs_srv_get_handle(priv, req->handle);
	if (!handle) {
		pr_warn("failed to find file with handle: %x", req->handle);
		error_code = ISOBUSFS_ERR_FILE_ORPATH_NOT_FOUND;
	}

	/* Determine whether to read a file or a directory */
	if (handle->dir) {
		ret = isobusfs_srv_read_directory(handle, res->data, count,
						  &readed_size);
	} else {
		ret = isobusfs_srv_read_file(handle, res->data, count,
					     &readed_size);
	}

	if (ret < 0) {
		error_code = ret;
		readed_size = 0;
	} else if (count != 0 && readed_size == 0) {
		error_code = ISOBUSFS_ERR_END_OF_FILE;
	}

send_response:
	res->fs_function =
		isobusfs_cg_function_to_buf(ISOBUSFS_CG_FILE_ACCESS,
					    ISOBUSFS_FA_F_READ_FILE_RES);
	res->tan = req->tan;
	res->error_code = error_code;
	res->count = htole16(readed_size);

	send_size = sizeof(*res) + readed_size;
	if (send_size < ISOBUSFS_MIN_TRANSFER_LENGH)
		send_size = ISOBUSFS_MIN_TRANSFER_LENGH;

	/* send to socket */
	ret = isobusfs_srv_sendto(priv, msg, res, send_size);
	if (ret < 0) {
		pr_warn("can't send Read File Response");
		goto free_res;
	}

	pr_debug("> tx: Read File Response. Error code: %d (%s), readed size: %d",
		 error_code, isobusfs_error_to_str(error_code), readed_size);

free_res:
	free(res);
	return ret;
}

static int isobusfs_srv_seek(struct isobusfs_srv_priv *priv,
			     struct isobusfs_srv_handles *handle, int32_t offset,
			     uint8_t position_mode)
{
	int whence;
	off_t offs;

	switch (position_mode) {
	case ISOBUSFS_FA_SEEK_SET:
		whence = SEEK_SET;
		if (offset < 0) {
			pr_warn("Invalid offset. Offset must be positive.");
			return ISOBUSFS_ERR_INVALID_REQUESTED_LENGHT;
		}
		break;
	case ISOBUSFS_FA_SEEK_CUR:
		whence = SEEK_CUR;
		if (offset < 0 && handle->offset < -offset) {
			pr_warn("Invalid offset. Negative offset is too big.");
			return ISOBUSFS_ERR_INVALID_REQUESTED_LENGHT;
		}
		break;
	case ISOBUSFS_FA_SEEK_END:
		whence = SEEK_END;
		if (offset > 0) {
			pr_warn("Invalid offset. Offset must be negative");
			return ISOBUSFS_ERR_INVALID_REQUESTED_LENGHT;
		}
		break;
	default:
		pr_warn("invalid position mode");
		return ISOBUSFS_ERR_OTHER;
	}

	/* seek file */
	offs = lseek(handle->fd, offset, whence);
	if (offs < 0) {
		pr_warn("Failed to seek file");

		switch (offs) {
		case EBADF:
			return ISOBUSFS_ERR_INVALID_HANDLE;
		case EINVAL:
			return ISOBUSFS_ERR_INVALID_REQUESTED_LENGHT;
		case ENXIO:
			return ISOBUSFS_ERR_END_OF_FILE;
		case EOVERFLOW:
			return ISOBUSFS_ERR_OUT_OF_MEM;
		case ESPIPE:
			return ISOBUSFS_ERR_ACCESS_DENIED;
		default:
			return ISOBUSFS_ERR_OTHER;
		}
	}

	handle->offset = offs;

	return ISOBUSFS_ERR_SUCCESS;
}

static int isobusfs_srv_seek_directory(struct isobusfs_srv_handles *handle,
				       int32_t offset)
{
	DIR *dir = fdopendir(handle->fd);

	if (!dir)
		return ISOBUSFS_ERR_OTHER;

	rewinddir(dir);

	for (int32_t i = 0; i < offset; i++) {
		if (readdir(dir) == NULL)
			return ISOBUSFS_ERR_END_OF_FILE;
	}

	handle->dir_pos = offset;

	return ISOBUSFS_ERR_SUCCESS;
}

static int isobusfs_srv_fa_sf_req(struct isobusfs_srv_priv *priv,
				  struct isobusfs_msg *msg)
{
	struct isobusfs_fa_seekf_res res = {0};
	struct isobusfs_srv_client *client;
	struct isobusfs_fa_seekf_req *req;
	struct isobusfs_srv_handles *handle;
	int32_t offset_out = 0;
	uint8_t error_code = 0;
	int ret;

	req = (struct isobusfs_fa_seekf_req *)msg->buf;
	pr_debug("< rx: Seek File Request. Handle: %x, offset: %d, position mode: %d",
		 req->handle, le32toh(req->offset), req->position_mode);

	client = isobusfs_srv_get_client_by_msg(priv, msg);
	if (!client) {
		pr_warn("client not found");
		error_code = ISOBUSFS_ERR_OTHER;
		goto send_response;
	}

	handle = isobusfs_srv_get_handle(priv, req->handle);
	if (!handle) {
		pr_warn("failed to find handle: %x", req->handle);
		error_code = ISOBUSFS_ERR_INVALID_HANDLE;
		goto send_response;
	}

	if (handle->dir) {
		error_code = isobusfs_srv_seek_directory(handle,
							 le32toh(req->offset));
		res.position = htole32(handle->dir_pos);
	} else {
		error_code = isobusfs_srv_seek(priv, handle, le32toh(req->offset),
					       req->position_mode);
		res.position = htole32(handle->offset);
	}

send_response:
	res.fs_function =
		isobusfs_cg_function_to_buf(ISOBUSFS_CG_FILE_ACCESS,
					    ISOBUSFS_FA_F_SEEK_FILE_RES);
	res.tan = req->tan;
	res.error_code = error_code;

	/* send to socket */
	ret = isobusfs_srv_sendto(priv, msg, &res, sizeof(res));
	if (ret < 0) {
		pr_warn("can't send seek file response");
		return ret;
	}

	pr_debug("> tx: Seek File Response. Error code: %d, offset: %d",
		 error_code, offset_out);

	return 0;
}

static int isobusfs_srv_fa_cf_req(struct isobusfs_srv_priv *priv,
				  struct isobusfs_msg *msg)
{
	struct isobusfs_close_file_request *req;
	struct isobusfs_close_file_res res;
	struct isobusfs_srv_client *client;
	uint8_t error_code = 0;
	int ret;

	req = (struct isobusfs_close_file_request *)msg->buf;

	client = isobusfs_srv_get_client_by_msg(priv, msg);
	if (!client) {
		pr_warn("client not found");
		error_code = ISOBUSFS_ERR_OTHER;
		goto send_response;
	}

	ret = isobusfs_srv_release_handle(priv, client, req->handle);
	if (ret < 0) {
		pr_warn("failed to release handle: %x", req->handle);
		switch (ret) {
		case -ENOENT:
			error_code = ISOBUSFS_ERR_FILE_ORPATH_NOT_FOUND;
			break;
		default:
			error_code = ISOBUSFS_ERR_OTHER;
		}
	}

send_response:
	res.fs_function =
		isobusfs_cg_function_to_buf(ISOBUSFS_CG_FILE_ACCESS,
					    ISOBUSFS_FA_F_CLOSE_FILE_RES);
	res.tan = req->tan;
	res.error_code = error_code;
	memset(&res.reserved[0], 0xff, sizeof(res.reserved));

	/* send to socket */
	ret = isobusfs_srv_sendto(priv, msg, &res, sizeof(res));
	if (ret < 0) {
		pr_warn("can't send current directory response");
		goto err;
	}

	pr_debug("> tx: Close File Response. Error code: %d", error_code);

err:
	return ret;
}

/* Command group: file access */
int isobusfs_srv_rx_cg_fa(struct isobusfs_srv_priv *priv,
			  struct isobusfs_msg *msg)
{
	int func = isobusfs_buf_to_function(msg->buf);
	int ret = 0;

	switch (func) {
	case ISOBUSFS_FA_F_OPEN_FILE_REQ:
		ret = isobusfs_srv_fa_open_file_req(priv, msg);
		break;
	case ISOBUSFS_FA_F_CLOSE_FILE_REQ:
		ret = isobusfs_srv_fa_cf_req(priv, msg);
		break;
	case ISOBUSFS_FA_F_READ_FILE_REQ:
		ret = isobusfs_srv_fa_rf_req(priv, msg);
		break;
	case ISOBUSFS_FA_F_SEEK_FILE_REQ:
		ret = isobusfs_srv_fa_sf_req(priv, msg);
		break;
	case ISOBUSFS_FA_F_WRITE_FILE_REQ: /* fall through */
	default:
		pr_warn("%s: unsupported function: %i", __func__, func);
		isobusfs_srv_send_error(priv, msg,
					ISOBUSFS_ERR_FUNC_NOT_SUPPORTED);
	}

	return ret;
}
