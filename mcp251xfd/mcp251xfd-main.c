// SPDX-License-Identifier: GPL-2.0
//
// Microchip MCP251xFD Family CAN controller debug tool
//
// Copyright (c) 2020, 2021 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <stdlib.h>
#include <string.h>

#include <linux/kernel.h>

#include "mcp251xfd-dump-userspace.h"

static void print_usage(char *prg)
{
	fprintf(stderr,
		"%s - decode chip and driver state of mcp251xfd.\n"
		"\n"
		"Usage: %s [options] <file>\n"
		"\n"
		"        <file>      path to dev coredump file\n"
		"                        ('/var/log/devcoredump-19700101-234200.dump')\n"
		"                    path to regmap register file\n"
                "                        ('/sys/kernel/debug/regmap/spi1.0-crc/registers')\n"
		"                    shortcut to regmap register file\n"
		"                        ('spi0.0')\n"
		"\n"
		"Options:\n"
		"        -h, --help  this help\n"
		"\n",
		prg, prg);
}

int regmap_bulk_read(struct regmap *map, unsigned int reg,
		     void *val, size_t val_count)
{
	memcpy(val, map->mem->buf + reg,
	       val_count * sizeof(uint32_t));

	return 0;
}

int main(int argc, char *argv[])
{
	struct mcp251xfd_mem mem = { };
	struct regmap map = {
		.mem = &mem,
	};
	struct mcp251xfd_priv priv = {
		.map = &map,
	};
	const char *file_path;
	int opt, err;

	struct option long_options[] = {
		{ "help", no_argument, 0, 'h' },
		{ 0, 0, 0, 0 },
	};

	while ((opt = getopt_long(argc, argv, "ei:pq::rvh", long_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			print_usage(basename(argv[0]));
			exit(EXIT_SUCCESS);
			break;

		default:
			print_usage(basename(argv[0]));
			exit(EXIT_FAILURE);
			break;
		}
	}

	file_path = argv[optind];

	if (!file_path) {
		print_usage(basename(argv[0]));
		exit(EXIT_FAILURE);
	}

	err = mcp251xfd_dev_coredump_read(&priv, &mem, file_path);
	if (err)
		err = mcp251xfd_regmap_read(&priv, &mem, file_path);
	if (err) {
		fprintf(stderr, "Unable to read file: '%s'\n", file_path);
		exit(EXIT_FAILURE);
	}

	mcp251xfd_dump(&priv);

	exit(EXIT_SUCCESS);
}
