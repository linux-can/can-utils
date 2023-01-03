// SPDX-License-Identifier: GPL-2.0
//
// Microchip MCP251xFD Family CAN controller debug tool
//
// Copyright (c) 2020, 2022, 2023 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include <linux/kernel.h>

#include "mcp251xfd.h"
#include "mcp251xfd-dump-userspace.h"

static int
do_mcp251xfd_regmap_read(struct mcp251xfd_priv *priv,
			 struct mcp251xfd_mem *mem,
			 const char *file_path)
{
	FILE *reg_file;
	uint16_t reg;
	uint32_t val;
	unsigned int n = 0;
	int ret, err = 0;

	reg_file = fopen(file_path, "r");
	if (!reg_file)
		return -errno;

	while ((ret = fscanf(reg_file, "%hx: %x\n", &reg, &val)) != EOF) {
		if (ret != 2) {
			fscanf(reg_file, "%*[^\n]\n");
			continue;
		}

		if (reg >= ARRAY_SIZE(mem->buf)) {
			err = -EINVAL;
			goto out_close;
		}

		*(uint32_t *)(mem->buf + reg) = val;

		n++;
	}

	printf("regmap: Found %u registers in %s\n", n, file_path);
	if (!n)
		err = -EINVAL;

 out_close:
	fclose(reg_file);

	return err;
}

int mcp251xfd_regmap_read(struct mcp251xfd_priv *priv,
			  struct mcp251xfd_mem *mem,
			  const char *file_path)
{
	char *tmp;
	int err;

	err = do_mcp251xfd_regmap_read(priv, mem, file_path);
	if (!err)
		return 0;

	/* maybe it's something like "spi0.0" */
	tmp = strchr(file_path, '/');
	if (tmp)
		return -ENOENT;

	/* first try literally */
	err = asprintf(&tmp, "/sys/kernel/debug/regmap/%s/registers", file_path);
	if (err == -1)
		return -errno;

	err = do_mcp251xfd_regmap_read(priv, mem, tmp);
	free (tmp);
	if (!err)
		return 0;

	/* then add "-crc" */
	err = asprintf(&tmp, "/sys/kernel/debug/regmap/%s-crc/registers", file_path);
	if (err == -1)
		return -errno;

	err = do_mcp251xfd_regmap_read(priv, mem, tmp);
	free (tmp);

	return err;
}
