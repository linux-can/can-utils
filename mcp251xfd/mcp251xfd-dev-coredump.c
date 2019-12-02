// SPDX-License-Identifier: GPL-2.0
//
// Microchip MCP251xFD Family CAN controller debug tool
//
// Copyright (c) 2020, 2021 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <linux/kernel.h>

#include "mcp251xfd.h"
#include "mcp251xfd-dump-userspace.h"

#define pr_err(fmt, args...)    fprintf(stderr, fmt, ##args)
#define pr_no(fmt, args...)     while (0) { fprintf(stdout, fmt, ##args); }

#ifdef DEBUG
#define pr_debug(fmt, args...) pr_err(fmt, ##args)
#else
#define pr_debug(fmt, args...) pr_no(fmt, ##args)
#endif


struct mcp251xfd_dump_iter {
	const void *start;
	const struct mcp251xfd_dump_object_header *hdr;
	const void *object_start;
	const void *object_end;
};

static __attribute__((__unused__)) const char *
get_object_type_str(enum mcp251xfd_dump_object_type object_type)
{
	switch (object_type) {
	case MCP251XFD_DUMP_OBJECT_TYPE_REG:
		return "reg";
	case MCP251XFD_DUMP_OBJECT_TYPE_TEF:
		return "tef";
	case MCP251XFD_DUMP_OBJECT_TYPE_RX:
		return "rx";
	case MCP251XFD_DUMP_OBJECT_TYPE_TX:
		return "tx";
	case MCP251XFD_DUMP_OBJECT_TYPE_END:
		return "end";
	default:
		return "<unknown>";
	}
}

static __attribute__((__unused__)) const char *
get_ring_key_str(enum mcp251xfd_dump_object_ring_key key)
{
	switch (key) {
	case MCP251XFD_DUMP_OBJECT_RING_KEY_HEAD:
		return "head";
	case MCP251XFD_DUMP_OBJECT_RING_KEY_TAIL:
		return "tail";
	case MCP251XFD_DUMP_OBJECT_RING_KEY_BASE:
		return "base";
	case MCP251XFD_DUMP_OBJECT_RING_KEY_NR:
		return "nr";
	case MCP251XFD_DUMP_OBJECT_RING_KEY_FIFO_NR:
		return "fifo-nr";
	case MCP251XFD_DUMP_OBJECT_RING_KEY_OBJ_NUM:
		return "obj-num";
	case MCP251XFD_DUMP_OBJECT_RING_KEY_OBJ_SIZE:
		return "obj-size";
	default:
		return "<unknown>";
	}
}

static int
do_dev_coredump_read_reg(const struct mcp251xfd_priv *priv,
			 const struct mcp251xfd_dump_iter *iter,
			 struct mcp251xfd_mem *mem)
{
	const struct mcp251xfd_dump_object_reg *object;

	for (object = iter->object_start;
	     (void *)(object + 1) <= iter->object_end;
	     object++) {
		uint32_t reg, val;

		reg = le32toh(object->reg);
		val = le32toh(object->val);

		pr_debug("%s: object=0x%04zx reg=0x%04x - val=0x%08x\n",
			 __func__,
			 (void *)object - iter->start,
			 reg, val);

		if (reg > ARRAY_SIZE(mem->buf))
			return -EINVAL;

		*(uint32_t *)(mem->buf + reg) = val;
	}

	return 0;
}

static int
do_dev_coredump_read_ring(const struct mcp251xfd_priv *priv,
			  const struct mcp251xfd_dump_iter *iter,
			  struct mcp251xfd_ring *ring)
{
	const struct mcp251xfd_dump_object_reg *object;

	for (object = iter->object_start;
	     (void *)(object + 1) <= iter->object_end;
	     object++) {
		enum mcp251xfd_dump_object_ring_key key;
		uint32_t val;

		key = le32toh(object->reg);
		val = le32toh(object->val);

		pr_debug("%s: reg=0x%04zx key=0x%02x: %8s - val=0x%08x\n",
			 __func__,
			 (void *)object - iter->start,
			 key, get_ring_key_str(key), val);

		switch (key) {
		case MCP251XFD_DUMP_OBJECT_RING_KEY_HEAD:
			ring->head = val;
			break;
		case MCP251XFD_DUMP_OBJECT_RING_KEY_TAIL:
			ring->tail = val;
			break;
		case MCP251XFD_DUMP_OBJECT_RING_KEY_BASE:
			ring->base = val;
			break;
		case MCP251XFD_DUMP_OBJECT_RING_KEY_NR:
			ring->nr = val;
			break;
		case MCP251XFD_DUMP_OBJECT_RING_KEY_FIFO_NR:
			ring->fifo_nr = val;
			break;
		case MCP251XFD_DUMP_OBJECT_RING_KEY_OBJ_NUM:
			ring->obj_num = val;
			break;
		case MCP251XFD_DUMP_OBJECT_RING_KEY_OBJ_SIZE:
			ring->obj_size = val;
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int
do_dev_coredump_read(struct mcp251xfd_priv *priv,
		     struct mcp251xfd_mem *mem,
		     const void *dump, size_t dump_len)
{
	struct mcp251xfd_dump_iter iter[] = {
		{
			.start = dump,
			.hdr = dump,
		},
	};

	while ((void *)(iter->hdr + 1) <= iter->start + dump_len &&
	       le32toh(iter->hdr->magic) == MCP251XFD_DUMP_MAGIC) {
		const struct mcp251xfd_dump_object_header *hdr = iter->hdr;
		enum mcp251xfd_dump_object_type object_type;
		size_t object_offset, object_len;
		int err;

		object_type = le32toh(hdr->type);
		object_offset = le32toh(hdr->offset);
		object_len = le32toh(hdr->len);

		if (object_offset + object_len > dump_len)
			return -EFAULT;

		iter->object_start = iter->start + object_offset;
		iter->object_end = iter->object_start + object_len;

		pr_debug("%s: hdr=0x%04zx type=0x%08x: %8s - offset=0x%04zx len=0x%04zx end=0x%04zx\n",
			 __func__,
			 (void *)iter->hdr - iter->start,
			 object_type, get_object_type_str(object_type),
			 object_offset, object_len, object_offset + object_len);

		switch (object_type) {
		case MCP251XFD_DUMP_OBJECT_TYPE_REG:
			err = do_dev_coredump_read_reg(priv, iter, mem);
			break;
		case MCP251XFD_DUMP_OBJECT_TYPE_TEF:
			err = do_dev_coredump_read_ring(priv, iter, priv->tef);
			break;
		case MCP251XFD_DUMP_OBJECT_TYPE_RX:
			err = do_dev_coredump_read_ring(priv, iter, priv->rx);
			break;
		case MCP251XFD_DUMP_OBJECT_TYPE_TX:
			err = do_dev_coredump_read_ring(priv, iter, priv->tx);
			break;
		case MCP251XFD_DUMP_OBJECT_TYPE_END:
			return 0;
		default:
			return -EINVAL;
		}

		if (err)
			return err;

		iter->hdr++;
	}

	return -EINVAL;
}

int mcp251xfd_dev_coredump_read(struct mcp251xfd_priv *priv,
				struct mcp251xfd_mem *mem,
				const char *dump_path)
{
	struct stat statbuf;
	size_t dump_len;
	void *dump;
	int fd, err;

	fd = open(dump_path, O_RDONLY);
	if (fd < 0)
		return -errno;

	err = fstat(fd, &statbuf);
	if (err < 0) {
		err = -errno;
		goto out_close;
	}
	dump_len = statbuf.st_size;

	dump = mmap(NULL, dump_len, PROT_READ, MAP_SHARED, fd, 0x0);
	if (dump == MAP_FAILED) {
		err = -errno;
		goto out_close;
	}

	err = do_dev_coredump_read(priv, mem, dump, dump_len);

	munmap(dump, dump_len);
 out_close:
	close(fd);
	return err;
}
