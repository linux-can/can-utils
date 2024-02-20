// SPDX-License-Identifier: LGPL-2.0-only
// SPDX-FileCopyrightText: 2023 Oleksij Rempel <linux@rempel-privat.de>

#include <errno.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "isobusfs_cmn.h"

#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int isobusfs_cmn_dh_validate_dir_path(const char *path, bool writable)
{
	struct stat path_stat;
	int mode = R_OK;
	int ret;

	mode |= writable ? W_OK : 0;
	ret = access(path, mode);
	if (ret == -1) {
		ret = -errno;
		pr_err("failed to acces path %s, for read %s. %s", path,
		       writable ? "and write" : "", strerror(ret));
		return ret;
	}

	ret = stat(path, &path_stat);
	if (ret == -1) {
		ret = -errno;
		pr_err("failed to get stat information on path %s. %s", path,
			strerror(ret));
		return ret;
	}

	if (!S_ISDIR(path_stat.st_mode)) {
		pr_err("path %s is not a directory", path);
		return -ENOTDIR;
	}

	return 0;
}

