/*
 * libsocketcan.h
 *
 * (C) 2009 Luotao Fu <l.fu@pengutronix.de>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 *
 * This library is distributed in the hope that it will be useful, but without
 * any warranty; without even the implied warranty of merchantability or fitness
 * for a particular purpose. see the gnu lesser general public license for more
 * details.
 *
 * you should have received a copy of the gnu lesser general public license
 * along with this library; if not, write to the free software foundation, inc.,
 * 59 temple place, suite 330, boston, ma 02111-1307 usa
 */

#ifndef _socketcan_netlink_h
#define _socketcan_netlink_h

/**
 * @file
 * @brief API overview
 */

#include <can_netlink.h>

int can_do_restart(const char *name);
int can_do_stop(const char *name);
int can_do_start(const char *name);

int can_set_restart_ms(const char *name, __u32 restart_ms);
int can_set_bittiming(const char *name, struct can_bittiming *bt);
int can_set_ctrlmode(const char *name, struct can_ctrlmode *cm);
int can_set_bitrate(const char *name, __u32 bitrate);
int can_set_bitrate_samplepoint(const char *name, __u32 bitrate, __u32 sample_point);

int can_get_restart_ms(const char *name, __u32 *restart_ms);
int can_get_bittiming(const char *name, struct can_bittiming *bt);
int can_get_ctrlmode(const char *name, struct can_ctrlmode *cm);
int can_get_state(const char *name, int *state);
int can_get_clock(const char *name, struct can_clock *clock);
int can_get_bittiming_const(const char *name, struct can_bittiming_const *btc);
int can_get_berr_counter(const char *name, struct can_berr_counter *bc);

#endif
