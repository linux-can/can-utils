/* SPDX-License-Identifier: GPL-2.0
 *
 * Microchip MCP251xFD Family CAN controller debug tool
 *
 * Copyright (c) 2019, 2020 Pengutronix,
 *               Marc Kleine-Budde <kernel@pengutronix.de>
 */

#ifndef _MCP251XFD_DUMP_USERSPACE_H
#define _MCP251XFD_DUMP_USERSPACE_H

#include "mcp251xfd.h"
#include "mcp251xfd-dump.h"

#define MCP251XFD_DUMP_UNKNOWN (-1U)

struct mcp251xfd_mem {
	char buf[0x1000];
};

struct mcp251xfd_ring {
	enum mcp251xfd_dump_object_type	type;
	const struct mcp251xfd_dump_regs_fifo *fifo;
	void *ram;

	unsigned int head;
	unsigned int tail;

	u16 base;
	u8 nr;
	u8 fifo_nr;
	u8 obj_num;
	u8 obj_size;
};

#define MCP251XFD_RING_TEF 0

struct mcp251xfd_priv {
	struct regmap *map;
	struct mcp251xfd_ring ring[32];
};

void mcp251xfd_dump_ring_init(struct mcp251xfd_ring *ring);

void mcp251xfd_dump(struct mcp251xfd_priv *priv);
int mcp251xfd_dev_coredump_read(struct mcp251xfd_priv *priv,
				struct mcp251xfd_mem *mem,
				const char *file_path);
int mcp251xfd_regmap_read(struct mcp251xfd_priv *priv,
			  struct mcp251xfd_mem *mem,
			  const char *file_path);
const char *
get_object_type_str(enum mcp251xfd_dump_object_type object_type);

#endif
