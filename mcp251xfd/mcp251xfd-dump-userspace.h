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

#define MCP251XFD_TX_FIFO 1
#define MCP251XFD_RX_FIFO(x) (MCP251XFD_TX_FIFO + 1 + (x))

struct mcp251xfd_mem {
	char buf[0x1000];
};

struct mcp251xfd_ring {
	unsigned int head;
	unsigned int tail;

	u16 base;
	u8 nr;
	u8 fifo_nr;
	u8 obj_num;
	u8 obj_size;
};

struct mcp251xfd_priv {
	struct regmap *map;

	struct mcp251xfd_ring tef[1];
	struct mcp251xfd_ring tx[1];
	struct mcp251xfd_ring rx[1];

	u8 rx_ring_num;
};

static inline u8 mcp251xfd_get_ring_head(const struct mcp251xfd_ring *ring)
{
	return ring->head & (ring->obj_num - 1);
}

static inline u8 mcp251xfd_get_ring_tail(const struct mcp251xfd_ring *ring)
{
	return ring->tail & (ring->obj_num - 1);
}

void mcp251xfd_dump(struct mcp251xfd_priv *priv);
int mcp251xfd_dev_coredump_read(struct mcp251xfd_priv *priv,
				struct mcp251xfd_mem *mem,
				const char *file_path);
int mcp251xfd_regmap_read(struct mcp251xfd_priv *priv,
			  struct mcp251xfd_mem *mem,
			  const char *file_path);

#endif
