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

/* needed on some 64 bit platforms to get consistent 64-bit types */
#define __SANE_USERSPACE_TYPES__

#include <linux/can.h>
#include <linux/can/j1939.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>

#ifndef J1939_LIB_H
#define J1939_LIB_H

#ifdef __cplusplus
extern "C" {
#endif

struct libj1939_cmn {
	int epoll_fd;
	struct epoll_event *epoll_events;
	size_t epoll_events_size;
	struct timespec next_send_time;
	struct timespec last_time;
};

void libj1939_parse_canaddr(char *spec, struct sockaddr_can *paddr);
extern int libj1939_str2addr(const char *str, char **endp, struct sockaddr_can *can);
extern const char *libj1939_addr2str(const struct sockaddr_can *can);

void libj1939_init_sockaddr_can(struct sockaddr_can *sac, uint32_t pgn);

int libj1939_open_socket(void);
int libj1939_bind_socket(int sock, struct sockaddr_can *addr);
int libj1939_socket_prio(int sock, int prio);
int libj1939_set_broadcast(int sock);
int libj1939_add_socket_to_epoll(int epoll_fd, int sock, uint32_t events);
int libj1939_create_epoll(void);

int libj1939_prepare_for_events(struct libj1939_cmn *cmn, int *nfds,
				bool dont_wait);

#ifdef __cplusplus
}
#endif

#endif
