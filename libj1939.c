/* SPDX-License-Identifier: GPL-2.0-only */
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

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/kernel.h>
#include <net/if.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "libj1939.h"
#include "lib.h"

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
			err(1, "if_nameindex()");
	}
}

/* retrieve name */
static const char *libj1939_ifnam(int ifindex)
{
	const struct if_nameindex *lp, *cached = saved;

	fetch_names();

	for (lp = saved; lp->if_index; ++lp) {
		if (lp->if_index == (unsigned int)ifindex)
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
	}
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
	}
	return 0;
}

void libj1939_parse_canaddr(char *spec, struct sockaddr_can *paddr)
{
	char *str;

	str = strsep(&spec, ":");
	if (strlen(str))
		paddr->can_ifindex = if_nametoindex(str);

	str = strsep(&spec, ",");
	if (str && strlen(str))
		paddr->can_addr.j1939.addr = strtoul(str, NULL, 0);

	str = strsep(&spec, ",");
	if (str && strlen(str))
		paddr->can_addr.j1939.pgn = strtoul(str, NULL, 0);

	str = strsep(&spec, ",");
	if (str && strlen(str))
		paddr->can_addr.j1939.name = strtoull(str, NULL, 0);
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
		if (can->can_addr.j1939.pgn == J1939_PGN_ADDRESS_CLAIMED)
			str += sprintf(str, ".%02x", can->can_addr.j1939.addr);
	} else if (can->can_addr.j1939.addr <= 0xfe)
		str += sprintf(str, "%02x", can->can_addr.j1939.addr);
	else
		str += sprintf(str, "-");
	if (can->can_addr.j1939.pgn <= J1939_PGN_MAX)
		str += sprintf(str, ",%05x", can->can_addr.j1939.pgn);

	return buf;
}

void libj1939_init_sockaddr_can(struct sockaddr_can *sac, uint32_t pgn)
{
	sac->can_family = AF_CAN;
	sac->can_addr.j1939.addr = J1939_NO_ADDR;
	sac->can_addr.j1939.name = J1939_NO_NAME;
	sac->can_addr.j1939.pgn = pgn;
}

/**
 * libj1939_open_socket - Open a new J1939 socket
 *
 * This function opens a new J1939 socket.
 *
 * Return: The file descriptor of the new socket, or a negative error code.
 */
int libj1939_open_socket(void)
{
	int ret;

	/* Create a new CAN J1939 socket */
	ret = socket(PF_CAN, SOCK_DGRAM, CAN_J1939);
	if (ret < 0) {
		/* Get the error code and print an error message */
		ret = -errno;
		pr_err("socket(j1939): %d (%s)", ret, strerror(ret));
		return ret;
	}
	return ret;
}

/**
 * libj1939_bind_socket - Bind a J1939 socket to a specific address
 * @sock: The file descriptor of the socket
 * @addr: The address to bind to
 *
 * This function binds a J1939 socket to a specific address.
 *
 * Return: 0 on success, or a negative error code.
 */
int libj1939_bind_socket(int sock, struct sockaddr_can *addr)
{
	int ret;

	ret = bind(sock, (void *)addr, sizeof(*addr));
	if (ret < 0) {
		ret = -errno;
		pr_err("failed to bind: %d (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

/**
 * libj1939_connect_socket - Connects a socket to a CAN address.
 * @sock: The socket file descriptor.
 * @addr: The CAN address to connect to.
 *
 * This function attempts to establish a connection between the given socket
 * and the specified CAN address. If the connection fails, it logs an error
 * message with the error code and a description of the error.
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int libj1939_connect_socket(int sock, struct sockaddr_can *addr)
{
	int ret;

	ret = connect(sock, (void *)addr, sizeof(*addr));
	if (ret < 0) {
		ret = -errno;
		pr_err("failed to connect socket: %d (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

/**
 * libj1939_socket_prio - Set the priority of a J1939 socket
 * @sock: The file descriptor of the socket
 * @prio: The priority to set
 *
 * This function sets the priority of a J1939 socket.
 *
 * Return: 0 on success, or a negative error code.
 */
int libj1939_socket_prio(int sock, int prio)
{
	int ret;

	ret = setsockopt(sock, SOL_CAN_J1939, SO_J1939_SEND_PRIO,
			 &prio, sizeof(prio));
	if (ret < 0) {
		ret = -errno;
		pr_warn("Failed to set priority %i. Error %i (%s)", prio, ret,
			strerror(ret));
		return ret;
	}

	return 0;
}

/**
 * libj1939_set_broadcast - Enable broadcast on a J1939 socket
 * @sock: The file descriptor of the socket
 *
 * This function enables broadcast on a J1939 socket.
 *
 * Return: 0 on success, or a negative error code.
 */
int libj1939_set_broadcast(int sock)
{
	int broadcast = true;
	int ret;

	ret = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast,
			 sizeof(broadcast));
	if (ret < 0) {
		ret = -errno;
		pr_err("setsockopt(SO_BROADCAST): %d (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

/**
 * libj1939_add_socket_to_epoll - Add a socket to an epoll instance
 * @epoll_fd: The file descriptor of the epoll instance
 * @sock: The file descriptor of the socket
 * @events: The events to monitor
 *
 * This function adds a socket to an epoll instance.
 *
 * Return: 0 on success, or a negative error code.
 */
int libj1939_add_socket_to_epoll(int epoll_fd, int sock, uint32_t events)
{
	struct epoll_event ev = {0};
	int ret;

	ev.events = events;
	ev.data.fd = sock;

	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &ev);
	if (ret < 0) {
		ret = errno;
		pr_err("epoll_ctl(EPOLL_CTL_ADD): %d (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}

/**
 * libj1939_create_epoll - Create a new epoll instance
 *
 * This function creates a new epoll instance.
 *
 * Return: The file descriptor of the new epoll instance, or a negative error
 * code.
 */
int libj1939_create_epoll(void)
{
	int ret, epoll_fd;

	epoll_fd = epoll_create1(0);
	if (epoll_fd < 0) {
		ret = -errno;
		pr_err("epoll_create1: %d (%s)", ret, strerror(ret));
		return ret;
	}

	return epoll_fd;
}

/**
 * libj1939_get_timeout_ms - Get the timeout in milliseconds until a specific
 *			     time
 * @ts: The time to wait for
 * @return: The timeout in milliseconds until the specified time
 *
 * This function calculates the timeout in milliseconds until a specific time.
 *
 * Return: The timeout in milliseconds until the specified time.
 */
static int libj1939_get_timeout_ms(struct timespec *ts)
{
	struct timespec curr_time;
	int64_t time_diff;
	int timeout_ms;

	clock_gettime(CLOCK_MONOTONIC, &curr_time);
	time_diff = timespec_diff_ms(ts, &curr_time);
	if (time_diff < 0) {
		/* Too late to send next message. Send it now */
		timeout_ms = 0;
	} else {
		if (time_diff > INT_MAX) {
			pr_warn("timeout too long: %" PRId64 " ms", time_diff);
			time_diff = INT_MAX;
		}

		timeout_ms = time_diff;
	}

	return timeout_ms;
}

/**
 * libj1939_prepare_for_events - Prepare and wait for events on an epoll
 * @cmn: The common J1939 instance data
 * @nfds: The number of file descriptors that are ready
 * @dont_wait: Don't wait for events, just check if there are any
 *
 * This function calculates the timeout until the next message should be sent
 * or any other event should be handled, prepares the epoll instance for events
 * by waiting for the specified timeout or until an event occurs, and waits for
 * events on the epoll instance.
 *
 * Return: 0 on success, or a negative error code.
 */
int libj1939_prepare_for_events(struct libj1939_cmn *cmn, int *nfds,
				bool dont_wait)
{
	int ret, timeout_ms;

	if (dont_wait)
		timeout_ms = 0;
	else
		timeout_ms = libj1939_get_timeout_ms(&cmn->next_send_time);

	ret = epoll_wait(cmn->epoll_fd, cmn->epoll_events,
			 cmn->epoll_events_size, timeout_ms);
	if (ret < 0) {
		ret = -errno;
		if (ret != -EINTR) {
			*nfds = 0;
			return ret;
		}
	}

	*nfds = ret;

	ret = clock_gettime(CLOCK_MONOTONIC, &cmn->last_time);
	if (ret < 0) {
		ret = -errno;
		pr_err("failed to get time: %i (%s)", ret, strerror(ret));
		return ret;
	}

	return 0;
}
