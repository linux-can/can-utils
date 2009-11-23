/*
 * socketcan_netlink.h
 *
 * (C) 2009 Luotao Fu <l.fu@pengutronix.de> 
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _SOCKETCAN_NETLINK_H
#define _SOCKETCAN_NETLINK_H

#include <linux/can/netlink.h>

#define GET_STATE 1
#define GET_RESTART_MS 2
#define GET_BITTIMING 3
#define GET_CTRLMODE 4

int scan_set_restart(const char *name);
int scan_set_bitrate(const char *name, __u32 bitrate);
int scan_set_restart_ms(const char *name, __u32 restart_ms);
int scan_set_ctrlmode(const char *name, __u32 mode, __u32 flags);

int scan_get_state(const char *name, int *state);
int scan_get_restart_ms(const char *name, __u32 *restart_ms);
int scan_get_bittiming(const char *name, struct can_bittiming *bt);
int scan_get_ctrlmode(const char *name, struct can_ctrlmode *cm);

#endif
