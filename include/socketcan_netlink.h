/* 
 * Header file for CAN netlink support 
 * (C) 2009 Luotao Fu <l.fu@pengutronix.de> 
 */

#ifndef _NMS_H
#define _NMS_H

#define IFLA_CAN_MAX	(__IFLA_CAN_MAX - 1)
#define IF_UP 1
#define IF_DOWN 2

#define GET_STATE 1
#define GET_RESTART_MS 2
#define GET_BITTIMING 3

int if_down(int fd, const char *name);
int if_up(int fd, const char *name);

int set_restart(const char *name);
int set_bitrate(const char *name, __u32 bitrate);
int set_restart_ms(const char *name, __u32 restart_ms);

int get_state(const char *name);
__u32 get_restart_ms(const char *name);
int get_bittiming(const char *name, struct can_bittiming *bt);

#endif
