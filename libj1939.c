/*
 * Copyright (c) 2011 EIA Electronics
 *
 * Authors:
 * Kurt Van Dijck <kurt.van.dijck@eia.be>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>

#include <error.h>
#include <unistd.h>
#include <fcntl.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include <net/if.h>

#include "libj1939.h"

/* static data */
static struct if_nameindex *saved;

__attribute__((destructor))
static void libj1939_cleanup(void)
{
	if (saved)
		if_freenameindex(saved);
	saved = 0;
}

static inline void fetch_names(void)
{
	if (!saved) {
		saved = if_nameindex();
		if (!saved)
			error(1, errno, "if_nameindex()");
	}
}

/* retrieve name */
static const char *libj1939_ifnam(int ifindex)
{
	const struct if_nameindex *lp, *cached = saved;

	fetch_names();

	for (lp = saved; lp->if_index; ++lp) {
		if (lp->if_index == ifindex)
			return lp->if_name;
	}
	if (cached) {
		/*
		 * the list was not recent
		 * iterate twice, but force a refresh now
		 * recursion stops since the 'saved' pointer is cleaned
		 */
		libj1939_cleanup();
		return libj1939_ifnam(ifindex);
	} else
		return NULL;
}

/* retrieve index */
static int libj1939_ifindex(const char *str)
{
	const struct if_nameindex *lp, *cached = saved;
	char *endp;
	int ret;

	ret = strtol(str, &endp, 0);
	if (!*endp)
		/* did some good parse */
		return ret;

	fetch_names();
	for (lp = saved; lp->if_index; ++lp) {
		if (!strcmp(lp->if_name, str))
			return lp->if_index;
	}
	if (cached) {
		libj1939_cleanup();
		return libj1939_ifindex(str);
	} else
		return 0;
}

int libj1939_str2addr(const char *str, char **endp, struct sockaddr_can *can)
{
	char *p;
	const char *pstr;
	uint64_t tmp64;
	unsigned long tmp;

	if (!endp)
		endp = &p;
	memset(can, 0, sizeof(*can));
	can->can_family = AF_CAN;
	can->can_addr.j1939.name = J1939_NO_NAME;
	can->can_addr.j1939.addr = J1939_NO_ADDR;
	can->can_addr.j1939.pgn = J1939_NO_PGN;

	pstr = strchr(str, ':');
	if (pstr) {
		char tmp[IFNAMSIZ];
		if ((pstr - str) >= IFNAMSIZ)
			return -1;
		strncpy(tmp, str, pstr - str);
		tmp[pstr - str] = 0;
		can->can_ifindex = libj1939_ifindex(tmp);
	} else {
		can->can_ifindex = libj1939_ifindex(str);
		if (can->can_ifindex) {
			if (endp)
				*endp = (char *)&str[strlen(str)];
			return 0;
		}
	}
	if (pstr)
		++pstr;
	else
		pstr = str;


	tmp64 = strtoull(pstr, endp, 16);
	if (*endp <= pstr)
		return 0;
	if ((*endp - pstr) == 2)
		can->can_addr.j1939.addr = tmp64;
	else
		can->can_addr.j1939.name = tmp64;
	if (!**endp)
		return 0;

	str = *endp + 1;
	tmp = strtoul(str, endp, 16);
	if (*endp > str)
		can->can_addr.j1939.pgn = tmp;
	return 0;
}

const char *libj1939_addr2str(const struct sockaddr_can *can)
{
	char *str;
	static char buf[128];

	str = buf;
	if (can->can_ifindex) {
		const char *ifname;
		ifname = libj1939_ifnam(can->can_ifindex);
		if (!ifname)
			str += sprintf(str, "#%i:", can->can_ifindex);
		else
			str += sprintf(str, "%s:", ifname);
	}
	if (can->can_addr.j1939.name) {
		str += sprintf(str, "%016llx", (unsigned long long)can->can_addr.j1939.name);
		if (can->can_addr.j1939.pgn == 0x0ee00)
			str += sprintf(str, ".%02x", can->can_addr.j1939.addr);
	} else if (can->can_addr.j1939.addr <= 0xfe)
		str += sprintf(str, "%02x", can->can_addr.j1939.addr);
	else
		str += sprintf(str, "-");
	if (can->can_addr.j1939.pgn <= 0x3ffff)
		str += sprintf(str, ",%05x", can->can_addr.j1939.pgn);

	return buf;
}

