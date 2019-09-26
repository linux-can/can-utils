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

#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/j1939.h>

#ifndef J1939_LIB_H
#define J1939_LIB_H

#ifdef __cplusplus
extern "C" {
#endif

void libj1939_parse_canaddr(char *spec, struct sockaddr_can *paddr);
extern int libj1939_str2addr(const char *str, char **endp, struct sockaddr_can *can);
extern const char *libj1939_addr2str(const struct sockaddr_can *can);

#ifdef __cplusplus
}
#endif

#endif
