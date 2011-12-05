
/*
 * socketcan/can/ioctl.h
 *
 * Definitions for CAN controller setup (work in progress)
 *
 * $Id$
 *
 * Send feedback to <linux-can@vger.kernel.org>
 *
 */

#ifndef CAN_IOCTL_H
#define CAN_IOCTL_H

#include <linux/sockios.h>

/*
 * CAN bitrate
 */
#define CAN_BITRATE_UNCONFIGURED	((__u32) 0xFFFFFFFFU)
#define CAN_BITRATE_UNKNOWN		0
#define CAN_BITRATE_DEFAULT		500000

/*
 * CAN custom bit time
 */
enum can_bittimes {
	CAN_BITTIME_STD,
	CAN_BITTIME_BTR
};

/* TSEG1 of controllers usually is a sum of synch_seg (always 1),
 * prop_seg and phase_seg1, TSEG2 = phase_seg2 */

struct can_bittime_std {
	__u32 brp;        /* baud rate prescaler */
	__u8  prop_seg;   /* from 1 to 8 */
	__u8  phase_seg1; /* from 1 to 8 */
	__u8  phase_seg2; /* from 1 to 8 */
	__u8  sjw:7;      /* from 1 to 4 */
	__u8  sam:1;      /* 1 - enable triple sampling */
};

struct can_bittime_btr {
	__u8  btr0;
	__u8  btr1;
};

struct can_bittime {
	enum can_bittimes type;
	union {
		struct can_bittime_std std;
		struct can_bittime_btr btr;
	};
};

/*
 * CAN mode
 */
enum can_mode {
	CAN_MODE_STOP = 0,
	CAN_MODE_START,
	CAN_MODE_SLEEP
};

/*
 * CAN controller mode
 */
#define CAN_CTRLMODE_LOOPBACK   0x1
#define CAN_CTRLMODE_LISTENONLY 0x2

/*
 * CAN operational and error states
 */
enum can_state {
	CAN_STATE_ACTIVE = 0,
	CAN_STATE_BUS_WARNING,
	CAN_STATE_BUS_PASSIVE,
	CAN_STATE_BUS_OFF,
	CAN_STATE_STOPPED,
	CAN_STATE_SLEEPING
};

/*
 * CAN device statistics
 */
struct can_device_stats {
	int error_warning;
	int data_overrun;
	int wakeup;
	int bus_error;
	int error_passive;
	int arbitration_lost;
	int restarts;
	int bus_error_at_init;
};

#endif /* CAN_IOCTL_H */
