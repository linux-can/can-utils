/*
 * socketcan_netlink.h
 *
 * (C) 2009 Luotao Fu <l.fu@pengutronix.de>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _SOCKETCAN_NETLINK_H
#define _SOCKETCAN_NETLINK_H

#include <linux/can/netlink.h>

int scan_do_restart(const char *name);
int scan_do_stop(const char *name);
int scan_do_start(const char *name);

int scan_set_restart_ms(const char *name, __u32 restart_ms);
int scan_set_bittiming(const char *name, struct can_bittiming *bt);
int scan_set_ctrlmode(const char *name, struct can_ctrlmode *cm);
int scan_set_bitrate(const char *name, __u32 bitrate, __u32 sample_point);

int scan_get_restart_ms(const char *name, __u32 *restart_ms);
int scan_get_bittiming(const char *name, struct can_bittiming *bt);
int scan_get_ctrlmode(const char *name, struct can_ctrlmode *cm);
int scan_get_state(const char *name, int *state);
int scan_get_clock(const char *name, struct can_clock *clock);

#endif
