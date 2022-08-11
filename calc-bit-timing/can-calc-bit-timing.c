/* SPDX-License-Identifier: GPL-2.0-only */
/* can-calc-bit-timing.c: Calculate CAN bit timing parameters
 *
 * Copyright (C) 2008 Wolfgang Grandegger <wg@grandegger.com>
 * Copyright (C) 2016, 2021, 2022 Marc Kleine-Budde <mkl@pengutronix.de>
 *
 * Derived from:
 *   can_baud.c - CAN baudrate calculation
 *   Code based on LinCAN sources and H8S2638 project
 *   Copyright 2004-2006 Pavel Pisa - DCE FELK CVUT cz
 *   Copyright 2005      Stanislav Marek
 *   email:pisa@cmp.felk.cvut.cz
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 */

#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/can/netlink.h>
#include <linux/types.h>

enum {
	OPT_TQ = UCHAR_MAX + 1,
	OPT_PROP_SEG,
	OPT_PHASE_SEG1,
	OPT_PHASE_SEG2,
	OPT_SJW,
	OPT_BRP,
	OPT_TSEG1,
	OPT_TSEG2,
	OPT_ALG,
};

/* imported from kernel */

/**
 * abs - return absolute value of an argument
 * @x: the value.  If it is unsigned type, it is converted to signed type first.
 *     char is treated as if it was signed (regardless of whether it really is)
 *     but the macro's return type is preserved as char.
 *
 * Return: an absolute value of x.
 */
#define abs(x)	__abs_choose_expr(x, long long,				\
		__abs_choose_expr(x, long,				\
		__abs_choose_expr(x, int,				\
		__abs_choose_expr(x, short,				\
		__abs_choose_expr(x, char,				\
		__builtin_choose_expr(					\
			__builtin_types_compatible_p(typeof(x), char),	\
			(char)({ signed char __x = (x); __x < 0 ? -__x:__x; }), \
			((void)0)))))))

#define __abs_choose_expr(x, type, other) __builtin_choose_expr(	\
	__builtin_types_compatible_p(typeof(x),   signed type) ||	\
	__builtin_types_compatible_p(typeof(x), unsigned type),		\
	({ signed type __x = (x); __x < 0 ? -__x : __x; }), other)

/*
 * min()/max()/clamp() macros that also do
 * strict type-checking.. See the
 * "unnecessary" pointer comparison.
 */
#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })

#define max(x, y) ({				\
	typeof(x) _max1 = (x);			\
	typeof(y) _max2 = (y);			\
	(void) (&_max1 == &_max2);		\
	_max1 > _max2 ? _max1 : _max2; })

/**
 * clamp - return a value clamped to a given range with strict typechecking
 * @val: current value
 * @lo: lowest allowable value
 * @hi: highest allowable value
 *
 * This macro does strict typechecking of lo/hi to make sure they are of the
 * same type as val.  See the unnecessary pointer comparisons.
 */
#define clamp(val, lo, hi) min((typeof(val))max(val, lo), hi)

#define do_div(n, base) ({					\
	uint32_t __base = (base);				\
	uint32_t __rem;						\
	__rem = ((uint64_t)(n)) % __base;			\
	(n) = ((uint64_t)(n)) / __base;				\
	__rem;							\
})

/* */

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* we don't want to see these prints */
#define netdev_err(dev, format, arg...) do { } while (0)
#define netdev_warn(dev, format, arg...) do { } while (0)

/* define in-kernel-types */
typedef __u64 u64;
typedef __u32 u32;

struct calc_ref_clk {
	__u32 clk;	/* CAN system clock frequency in Hz */
	const char *name;
};

/*
 * minimal structs, just enough to be source level compatible
 */
struct can_priv {
	struct can_clock clock;
};

struct net_device {
	struct can_priv	priv;
};

struct calc_bittiming_const {
	const struct can_bittiming_const bittiming_const;
	const struct can_bittiming_const data_bittiming_const;

	const struct calc_ref_clk ref_clk[16];

	const void (*printf_btr)(struct can_bittiming *bt, bool hdr);
	const void (*printf_data_btr)(struct can_bittiming *bt, bool hdr);
};

struct alg {
	union {
		int (*calc_bittiming)(struct net_device *dev, struct can_bittiming *bt,
				      const struct can_bittiming_const *btc);
		int (*calc_bittiming_const)(const struct net_device *dev, struct can_bittiming *bt,
					    const struct can_bittiming_const *btc);
	};
	union {
		int (*fixup_bittiming)(struct net_device *dev, struct can_bittiming *bt,
				       const struct can_bittiming_const *btc);
		int (*fixup_bittiming_const)(const struct net_device *dev, struct can_bittiming *bt,
					     const struct can_bittiming_const *btc);
	};
	const char *name;
};

struct calc_data {
	const struct can_bittiming_const *bittiming_const;
	const struct alg *alg;
	const void (*printf_btr)(struct can_bittiming *bt, bool hdr);
	const char *name;

	const struct calc_ref_clk *ref_clks;
	const unsigned int *bitrates;

	unsigned int sample_point;

	const struct calc_ref_clk *opt_ref_clk;
	const unsigned int *opt_bitrates;
	const unsigned int *opt_data_bitrates;
	const struct can_bittiming *opt_bt;

	bool quiet;
	bool fd_mode;
};

static inline void *netdev_priv(const struct net_device *dev)
{
	return (void *)&dev->priv;
}

static void print_usage(char *cmd)
{
	printf("%s - calculate CAN bit timing parameters.\n", cmd);
	printf("Usage: %s [options] [<CAN-contoller-name>]\n"
	       "Options:\n"
	       "\t-q             don't print header line\n"
	       "\t-l             list all support CAN controller names\n"
	       "\t-b <bitrate>   arbitration bit-rate in bits/sec\n"
	       "\t-d <bitrate>   data bit-rate in bits/sec\n"
	       "\t-s <samp_pt>   sample-point in one-tenth of a percent\n"
	       "\t               or 0 for CIA recommended sample points\n"
	       "\t-c <clock>     real CAN system clock in Hz\n"
	       "\t--alg <alg>    choose specified algorithm for bit-timing calculation\n"
	       "\n"
	       "Or supply low level bit timing parameters to decode them:\n"
	       "\n"
	       "\t--prop-seg     Propagation segment in TQs\n"
	       "\t--phase-seg1   Phase buffer segment 1 in TQs\n"
	       "\t--phase-seg2   Phase buffer segment 2 in TQs\n"
	       "\t--sjw          Synchronisation jump width in TQs\n"
	       "\t--brp          Bit-rate prescaler\n"
	       "\t--tseg1        Time segment 1 = prop-seg + phase-seg1\n"
	       "\t--tseg2        Time segment 2 = phase_seg2\n",
	       cmd);
}

static void printf_btr_nop(struct can_bittiming *bt, bool hdr)
{
}

#define RCAR_CAN_BCR_TSEG1(x)	(((x) & 0x0f) << 20)
#define RCAR_CAN_BCR_BPR(x)	(((x) & 0x3ff) << 8)
#define RCAR_CAN_BCR_SJW(x)	(((x) & 0x3) << 4)
#define RCAR_CAN_BCR_TSEG2(x)	((x) & 0x07)

static void printf_btr_rcar_can(struct can_bittiming *bt, bool hdr)
{
	if (hdr) {
		printf("%10s", "CiBCR");
	} else {
		uint32_t bcr;

		bcr = RCAR_CAN_BCR_TSEG1(bt->phase_seg1 + bt->prop_seg - 1) |
			RCAR_CAN_BCR_BPR(bt->brp - 1) |
			RCAR_CAN_BCR_SJW(bt->sjw - 1) |
			RCAR_CAN_BCR_TSEG2(bt->phase_seg2 - 1);

		printf("0x%08x", bcr << 8);
	}
}

static void printf_btr_mcp251x(struct can_bittiming *bt, bool hdr)
{
	uint8_t cnf1, cnf2, cnf3;

	if (hdr) {
		printf("CNF1 CNF2 CNF3");
	} else {
		cnf1 = ((bt->sjw - 1) << 6) | (bt->brp - 1);
		cnf2 = 0x80 | ((bt->phase_seg1 - 1) << 3) | (bt->prop_seg - 1);
		cnf3 = bt->phase_seg2 - 1;
		printf("0x%02x 0x%02x 0x%02x", cnf1, cnf2, cnf3);
	}
}

static void printf_btr_mcp251xfd(struct can_bittiming *bt, bool hdr)
{
	if (hdr) {
		printf("%10s", "NBTCFG");
	} else {
		uint32_t nbtcfg = ((bt->brp - 1) << 24) |
			((bt->prop_seg + bt->phase_seg1 - 1) << 16) |
			((bt->phase_seg2 - 1) << 8) |
			(bt->sjw - 1);
		printf("0x%08x", nbtcfg);
	}
}

static void printf_btr_bxcan(struct can_bittiming *bt, bool hdr)
{
	if (hdr) {
		printf("%10s", "CAN_BTR");
	} else {
		uint32_t btr;

		btr = (((bt->brp -1) & 0x3ff) << 0) |
			(((bt->prop_seg + bt->phase_seg1 -1) & 0xf) << 16) |
			(((bt->phase_seg2 -1) & 0x7) << 20) |
			(((bt->sjw -1) & 0x3) << 24);

		printf("0x%08x", btr);
	}
}

static void printf_btr_at91(struct can_bittiming *bt, bool hdr)
{
	if (hdr) {
		printf("%10s", "CAN_BR");
	} else {
		uint32_t br = ((bt->phase_seg2 - 1) |
			       ((bt->phase_seg1 - 1) << 4) |
			       ((bt->prop_seg - 1) << 8) |
			       ((bt->sjw - 1) << 12) |
			       ((bt->brp - 1) << 16));
		printf("0x%08x", br);
	}
}

static void printf_btr_c_can(struct can_bittiming *bt, bool hdr)
{
	if (hdr) {
		printf("%13s", "BTR BRPEXT");
	} else {
		uint32_t btr;
		uint32_t brpext;

		btr = (((bt->brp -1) & 0x3f) << 0) |
			(((bt->sjw -1) & 0x3) << 6) |
			(((bt->prop_seg + bt->phase_seg1 -1) & 0xf) << 8) |
			(((bt->phase_seg2 -1) & 0x7) << 12);
		brpext = ((bt->brp -1) >> 6) & 0xf;

		printf("0x%04x 0x%04x", btr, brpext);
	}
}

static void printf_btr_flexcan(struct can_bittiming *bt, bool hdr)
{
	if (hdr) {
		printf("%10s", "CAN_CTRL");
	} else {
		uint32_t ctrl = (((bt->brp        - 1) << 24) |
				 ((bt->sjw        - 1) << 22) |
				 ((bt->phase_seg1 - 1) << 19) |
				 ((bt->phase_seg2 - 1) << 16) |
				 ((bt->prop_seg   - 1) <<  0));

		printf("0x%08x", ctrl);
	}
}

static void printf_btr_mcan(struct can_bittiming *bt, bool hdr)
{
	if (hdr) {
		printf("%10s", "NBTP");
	} else {
		uint32_t nbtp;


		nbtp = (((bt->brp -1) & 0x1ff) << 16) |
			(((bt->sjw -1) & 0x7f) << 25) |
			(((bt->prop_seg + bt->phase_seg1 -1) & 0xff) << 8) |
			(((bt->phase_seg2 -1) & 0x7f) << 0);

		printf("0x%08x", nbtp);
	}
}

static void printf_btr_sja1000(struct can_bittiming *bt, bool hdr)
{
	uint8_t btr0, btr1;

	if (hdr) {
		printf("%9s", "BTR0 BTR1");
	} else {
		btr0 = ((bt->brp - 1) & 0x3f) | (((bt->sjw - 1) & 0x3) << 6);
		btr1 = ((bt->prop_seg + bt->phase_seg1 - 1) & 0xf) |
			(((bt->phase_seg2 - 1) & 0x7) << 4);
		printf("0x%02x 0x%02x", btr0, btr1);
	}
}

static void printf_btr_ti_hecc(struct can_bittiming *bt, bool hdr)
{
	if (hdr) {
		printf("%10s", "CANBTC");
	} else {
		uint32_t can_btc;

		can_btc = (bt->phase_seg2 - 1) & 0x7;
		can_btc |= ((bt->phase_seg1 + bt->prop_seg - 1)
			    & 0xF) << 3;
		can_btc |= ((bt->sjw - 1) & 0x3) << 8;
		can_btc |= ((bt->brp - 1) & 0xFF) << 16;

		printf("0x%08x", can_btc);
	}
}

static const struct calc_bittiming_const can_calc_consts[] = {
	{
		.bittiming_const = {
			.name = "rcar_can",
			.tseg1_min = 4,
			.tseg1_max = 16,
			.tseg2_min = 2,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 1024,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 65000000, },
		},
		.printf_btr = printf_btr_rcar_can,
	}, {
		.bittiming_const = {
			.name = "rcar_canfd",
			.tseg1_min = 2,
			.tseg1_max = 128,
			.tseg2_min = 2,
			.tseg2_max = 32,
			.sjw_max = 32,
			.brp_min = 1,
			.brp_max = 1024,
			.brp_inc = 1,
		},
		.data_bittiming_const = {
			.name = "rcar_canfd",
			.tseg1_min = 2,
			.tseg1_max = 16,
			.tseg2_min = 2,
			.tseg2_max = 8,
			.sjw_max = 8,
			.brp_min = 1,
			.brp_max = 256,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 20000000, .name = "CIA recommendation" },
			{ .clk = 40000000, .name = "CIA recommendation" },
		},
	}, {
		.bittiming_const = {
			.name = "rcar_canfd (CC)",
			.tseg1_min = 4,
			.tseg1_max = 16,
			.tseg2_min = 2,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 1024,
			.brp_inc = 1,
		},
	}, {	/* -------- SPI -------- */
		.bittiming_const = {
			.name = "hi311x",
			.tseg1_min = 2,
			.tseg1_max = 16,
			.tseg2_min = 2,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 64,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk =  24000000, },
		},
	}, {
		.bittiming_const = {
			.name = "mcp251x",
			.tseg1_min = 3,
			.tseg1_max = 16,
			.tseg2_min = 2,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 64,
			.brp_inc = 1,
		},
		.ref_clk = {
			/* The mcp251x uses half of the external OSC clock as the base clock */
			{ .clk =  8000000 / 2, .name = "8 MHz OSC" },
			{ .clk = 12000000 / 2, .name = "12 MHz OSC" },
			{ .clk = 16000000 / 2, .name = "16 MHz OSC" },
			{ .clk = 20000000 / 2, .name = "20 MHz OSC" },
		},
		.printf_btr = printf_btr_mcp251x,
	}, {
		.bittiming_const = {
			.name = "mcp251xfd",
			.tseg1_min = 2,
			.tseg1_max = 256,
			.tseg2_min = 1,
			.tseg2_max = 128,
			.sjw_max = 128,
			.brp_min = 1,
			.brp_max = 256,
			.brp_inc = 1,
		},
		.data_bittiming_const = {
			.name = "mcp251xfd",
			.tseg1_min = 1,
			.tseg1_max = 32,
			.tseg2_min = 1,
			.tseg2_max = 16,
			.sjw_max = 16,
			.brp_min = 1,
			.brp_max = 256,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 20000000, .name = "CIA recommendation" },
			{ .clk = 40000000, .name = "CIA recommendation" },
		},
		.printf_btr = printf_btr_mcp251xfd,
	}, {	/* -------- USB -------- */
		.bittiming_const = {
			.name = "usb_8dev",
			.tseg1_min = 1,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 1024,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 32000000, },
		}
	}, {
		.bittiming_const = {
			.name = "ems_usb",
			.tseg1_min = 1,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 64,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 8000000, },
		},
	}, {

#define ESD_USB2_TSEG1_MIN 1
#define ESD_USB2_TSEG1_MAX 16
#define ESD_USB2_TSEG2_MIN 1
#define ESD_USB2_TSEG2_MAX 8
#define ESD_USB2_SJW_MAX 4
#define ESD_USB2_BRP_MIN 1
#define ESD_USB2_BRP_MAX 1024
#define ESD_USB2_BRP_INC 1

#define ESD_USB2_CAN_CLOCK 60000000
#define ESD_USBM_CAN_CLOCK 36000000

		.bittiming_const = {
			.name = "esd_usb2",
			.tseg1_min = ESD_USB2_TSEG1_MIN,
			.tseg1_max = ESD_USB2_TSEG1_MAX,
			.tseg2_min = ESD_USB2_TSEG2_MIN,
			.tseg2_max = ESD_USB2_TSEG2_MAX,
			.sjw_max = ESD_USB2_SJW_MAX,
			.brp_min = ESD_USB2_BRP_MIN,
			.brp_max = ESD_USB2_BRP_MAX,
			.brp_inc = ESD_USB2_BRP_INC,
		},
		.ref_clk = {
			{ .clk = ESD_USB2_CAN_CLOCK, .name = "CAN-USB/2", },
			{ .clk = ESD_USBM_CAN_CLOCK, .name = "CAN-USB/Micro", },
		},
	}, {	/* gs_usb */
		.bittiming_const = {
			.name = "bxcan",	// stm32f072
			.tseg1_min = 1,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 1024,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 48000000, },
		},
		.printf_btr = printf_btr_bxcan,
	}, {	/* gs_usb */
		.bittiming_const = {
			.name = "CANtact Pro",	// LPC65616
			.tseg1_min = 1,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 1024,
			.brp_inc = 1,
		},
		.data_bittiming_const = {
			.name = "CANtact Pro",	// LPC65616
			.tseg1_min = 1,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 1024,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 24000000, .name = "CANtact Pro (original)", },
			{ .clk = 40000000, .name = "CIA recommendation" },
		},
	}, {

#define KVASER_USB_TSEG1_MIN 1
#define KVASER_USB_TSEG1_MAX 16
#define KVASER_USB_TSEG2_MIN 1
#define KVASER_USB_TSEG2_MAX 8
#define KVASER_USB_SJW_MAX 4
#define KVASER_USB_BRP_MIN 1
#define KVASER_USB_BRP_MAX 64
#define KVASER_USB_BRP_INC 1

		.bittiming_const = {
			.name = "kvaser_usb",
			.tseg1_min = KVASER_USB_TSEG1_MIN,
			.tseg1_max = KVASER_USB_TSEG1_MAX,
			.tseg2_min = KVASER_USB_TSEG2_MIN,
			.tseg2_max = KVASER_USB_TSEG2_MAX,
			.sjw_max = KVASER_USB_SJW_MAX,
			.brp_min = KVASER_USB_BRP_MIN,
			.brp_max = KVASER_USB_BRP_MAX,
			.brp_inc = KVASER_USB_BRP_INC,
		},
		.ref_clk = {
			{ .clk = 8000000, },
		},
	}, {
		.bittiming_const = {
			.name = "kvaser_usb_kcan",
			.tseg1_min = 1,
			.tseg1_max = 255,
			.tseg2_min = 1,
			.tseg2_max = 32,
			.sjw_max = 16,
			.brp_min = 1,
			.brp_max = 8192,
			.brp_inc = 1,
		},
		.data_bittiming_const = {
			.name = "kvaser_usb_kcan",
			.tseg1_min = 1,
			.tseg1_max = 255,
			.tseg2_min = 1,
			.tseg2_max = 32,
			.sjw_max = 16,
			.brp_min = 1,
			.brp_max = 8192,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 80000000, },
		},
	}, {
		.bittiming_const = {
			.name = "kvaser_usb_flex",
			.tseg1_min = 4,
			.tseg1_max = 16,
			.tseg2_min = 2,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 256,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 24000000, },
		},
	}, {
		.bittiming_const = {
			.name = "pcan_usb_pro",
			.tseg1_min = 1,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 1024,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 56000000, },
		},
	}, {

#define PUCAN_TSLOW_BRP_BITS 10
#define PUCAN_TSLOW_TSGEG1_BITS 8
#define PUCAN_TSLOW_TSGEG2_BITS 7
#define PUCAN_TSLOW_SJW_BITS 7

#define PUCAN_TFAST_BRP_BITS 10
#define PUCAN_TFAST_TSGEG1_BITS 5
#define PUCAN_TFAST_TSGEG2_BITS 4
#define PUCAN_TFAST_SJW_BITS 4

		.bittiming_const = {
			.name = "pcan_usb_fd",
			.tseg1_min = 1,
			.tseg1_max = (1 << PUCAN_TSLOW_TSGEG1_BITS),
			.tseg2_min = 1,
			.tseg2_max = (1 << PUCAN_TSLOW_TSGEG2_BITS),
			.sjw_max = (1 << PUCAN_TSLOW_SJW_BITS),
			.brp_min = 1,
			.brp_max = (1 << PUCAN_TSLOW_BRP_BITS),
			.brp_inc = 1,
		},
		.data_bittiming_const = {
			.name = "pcan_usb_fd",
			.tseg1_min = 1,
			.tseg1_max = (1 << PUCAN_TFAST_TSGEG1_BITS),
			.tseg2_min = 1,
			.tseg2_max = (1 << PUCAN_TFAST_TSGEG2_BITS),
			.sjw_max = (1 << PUCAN_TFAST_SJW_BITS),
			.brp_min = 1,
			.brp_max = (1 << PUCAN_TFAST_BRP_BITS),
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 80000000, },
		},
	}, {
		.bittiming_const = {
			.name = "softing",
			.tseg1_min = 1,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 32,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 8000000, },
			{ .clk = 16000000, },
		},
	}, {
		.bittiming_const = {
			.name = "at91",
			.tseg1_min = 4,
			.tseg1_max = 16,
			.tseg2_min = 2,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 2,
			.brp_max = 128,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 99532800, .name = "ronetix PM9263", },
			{ .clk = 100000000, },
		},
		.printf_btr = printf_btr_at91,
	}, {
		.bittiming_const = {
			.name = "cc770",
			.tseg1_min = 1,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 64,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 8000000 },
		}
	}, {
		.bittiming_const = {
			.name = "c_can",
			.tseg1_min = 2,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 1024,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 24000000, },
		},
		.printf_btr = printf_btr_c_can,
	}, {
		.bittiming_const = {
			.name = "flexcan",
			.tseg1_min = 4,
			.tseg1_max = 16,
			.tseg2_min = 2,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 256,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 24000000, .name = "mx28" },
			{ .clk = 30000000, .name = "mx6" },
			{ .clk = 49875000, },
			{ .clk = 66000000, },
			{ .clk = 66500000, .name = "mx25" },
			{ .clk = 66666666, },
			{ .clk = 83368421, .name = "vybrid" },
		},
		.printf_btr = printf_btr_flexcan,
	}, {
		.bittiming_const = {
			.name = "flexcan-fd",
			.tseg1_min = 2,
			.tseg1_max = 96,
			.tseg2_min = 2,
			.tseg2_max = 32,
			.sjw_max = 16,
			.brp_min = 1,
			.brp_max = 1024,
			.brp_inc = 1,
		},
		.data_bittiming_const = {
			.name = "flexcan-fd",
			.tseg1_min = 2,
			.tseg1_max = 39,
			.tseg2_min = 2,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 1024,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 20000000, .name = "CIA recommendation" },
			{ .clk = 40000000, .name = "CIA recommendation" },
		},
	}, {

#define GRCAN_CONF_PS1_MIN 1
#define GRCAN_CONF_PS1_MAX 15
#define GRCAN_CONF_PS2_MIN 2
#define GRCAN_CONF_PS2_MAX 8
#define GRCAN_CONF_RSJ_MAX 4
#define GRCAN_CONF_SCALER_MIN 0
#define GRCAN_CONF_SCALER_MAX 255
#define GRCAN_CONF_SCALER_INC 1

		.bittiming_const = {
			.name = "grcan",
			.tseg1_min = GRCAN_CONF_PS1_MIN + 1,
			.tseg1_max = GRCAN_CONF_PS1_MAX + 1,
			.tseg2_min = GRCAN_CONF_PS2_MIN,
			.tseg2_max = GRCAN_CONF_PS2_MAX,
			.sjw_max = GRCAN_CONF_RSJ_MAX,
			.brp_min = GRCAN_CONF_SCALER_MIN + 1,
			.brp_max = GRCAN_CONF_SCALER_MAX + 1,
			.brp_inc = GRCAN_CONF_SCALER_INC,
		},
	}, {
		.bittiming_const = {
			.name = "ifi_canfd",
			.tseg1_min = 1,
			.tseg1_max = 256,
			.tseg2_min = 2,
			.tseg2_max = 256,
			.sjw_max = 128,
			.brp_min = 2,
			.brp_max = 512,
			.brp_inc = 1,
		},
		.data_bittiming_const = {
			.name = "ifi_canfd",
			.tseg1_min = 1,
			.tseg1_max = 256,
			.tseg2_min = 2,
			.tseg2_max = 256,
			.sjw_max = 128,
			.brp_min = 2,
			.brp_max = 512,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 20000000, .name = "CIA recommendation" },
			{ .clk = 40000000, .name = "CIA recommendation" },
		},
	}, {
		.bittiming_const = {
			.name = "janz-ican3",
			.tseg1_min = 1,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 64,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 8000000, },
		},
	}, {
		.bittiming_const = {
			.name = "kvaser_pciefd",
			.tseg1_min = 1,
			.tseg1_max = 512,
			.tseg2_min = 1,
			.tseg2_max = 32,
			.sjw_max = 16,
			.brp_min = 1,
			.brp_max = 8192,
			.brp_inc = 1,
		},
		.data_bittiming_const = {
			.name = "kvaser_pciefd",
			.tseg1_min = 1,
			.tseg1_max = 512,
			.tseg2_min = 1,
			.tseg2_max = 32,
			.sjw_max = 16,
			.brp_min = 1,
			.brp_max = 8192,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 20000000, .name = "CIA recommendation" },
			{ .clk = 40000000, .name = "CIA recommendation" },
		},
	}, {
		.bittiming_const = {
			.name = "mscan",
			.tseg1_min = 4,
			.tseg1_max = 16,
			.tseg2_min = 2,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 64,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 32000000, },
			{ .clk = 33000000, },
			{ .clk = 33300000, },
			{ .clk = 33333333, },
			{ .clk = 66660000, .name = "mpc5121", },
			{ .clk = 66666666, .name = "mpc5121" },
		},
	}, {
		.bittiming_const = {
			.name = "mcan-v3.0",
			.tseg1_min = 2,
			.tseg1_max = 64,
			.tseg2_min = 1,
			.tseg2_max = 16,
			.sjw_max = 16,
			.brp_min = 1,
			.brp_max = 1024,
			.brp_inc = 1,
		},
		.data_bittiming_const = {
			.name = "mcan-v3.0",
			.tseg1_min = 2,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 32,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 20000000, .name = "CIA recommendation" },
			{ .clk = 40000000, .name = "CIA recommendation" },
		},
		.printf_btr = printf_btr_mcan,
	}, {
		.bittiming_const = {
			.name = "mcan-v3.1+",
			.tseg1_min = 2,
			.tseg1_max = 256,
			.tseg2_min = 2,
			.tseg2_max = 128,
			.sjw_max = 128,
			.brp_min = 1,
			.brp_max = 512,
			.brp_inc = 1,
		},
		.data_bittiming_const = {
			.name = "mcan-v3.1+",
			.tseg1_min = 1,
			.tseg1_max = 32,
			.tseg2_min = 1,
			.tseg2_max = 16,
			.sjw_max = 16,
			.brp_min = 1,
			.brp_max = 32,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 20000000, .name = "CIA recommendation" },
			{ .clk = 40000000, .name = "CIA recommendation" },
			{ .clk = 24000000, .name = "stm32mp1 - ck_hse" },
			{ .clk = 24573875, .name = "stm32mp1 - pll3_q" },
			{ .clk = 29700000, .name = "stm32mp1 - pll4_q" },
			{ .clk = 48000000, .name = "stm32mp1 lxatac (new)" },
			{ .clk = 60000000, .name = "stm32mp1 ecu02.5- pll4_r" },
			{ .clk = 62500000, .name = "stm32mp1 lxatac (old) - pll4_r" },
			{ .clk = 74250000, .name = "stm32mp1 - pll4_r" },
		},
		.printf_btr = printf_btr_mcan,
	}, {

#define PUCAN_TSLOW_BRP_BITS 10
#define PUCAN_TSLOW_TSGEG1_BITS 8
#define PUCAN_TSLOW_TSGEG2_BITS 7
#define PUCAN_TSLOW_SJW_BITS 7

#define PUCAN_TFAST_BRP_BITS 10
#define PUCAN_TFAST_TSGEG1_BITS 5
#define PUCAN_TFAST_TSGEG2_BITS 4
#define PUCAN_TFAST_SJW_BITS 4

		.bittiming_const = {
			.name = "peak_canfd",
			.tseg1_min = 1,
			.tseg1_max = (1 << PUCAN_TSLOW_TSGEG1_BITS),
			.tseg2_min = 1,
			.tseg2_max = (1 << PUCAN_TSLOW_TSGEG2_BITS),
			.sjw_max = (1 << PUCAN_TSLOW_SJW_BITS),
			.brp_min = 1,
			.brp_max = (1 << PUCAN_TSLOW_BRP_BITS),
			.brp_inc = 1,
		},
		.data_bittiming_const = {
			.name = "peak_canfd",
			.tseg1_min = 1,
			.tseg1_max = (1 << PUCAN_TFAST_TSGEG1_BITS),
			.tseg2_min = 1,
			.tseg2_max = (1 << PUCAN_TFAST_TSGEG2_BITS),
			.sjw_max = (1 << PUCAN_TFAST_SJW_BITS),
			.brp_min = 1,
			.brp_max = (1 << PUCAN_TFAST_BRP_BITS),
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 20000000, },
			{ .clk = 24000000, },
			{ .clk = 30000000, },
			{ .clk = 40000000, },
			{ .clk = 60000000, },
			{ .clk = 80000000, },
		},
	}, {
		.bittiming_const = {
			.name = "sja1000",
			.tseg1_min = 1,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 64,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 16000000 / 2, },
			{ .clk = 24000000 / 2, .name = "f81601" },
		},
		.printf_btr = printf_btr_sja1000,
	}, {
		.bittiming_const = {
			.name = "sun4i_can",
			.tseg1_min = 1,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 64,
			.brp_inc = 1,
		},
	}, {
		.bittiming_const = {
			.name = "ti_hecc",
			.tseg1_min = 1,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 256,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 13000000, },
		},
		.printf_btr = printf_btr_ti_hecc,
	}, {
		.bittiming_const = {
			.name = "xilinx_can",
			.tseg1_min = 1,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 4,
			.brp_min = 1,
			.brp_max = 256,
			.brp_inc = 1,
		},
	}, {
		.bittiming_const = {
			.name = "xilinx_can_fd",
			.tseg1_min = 1,
			.tseg1_max = 64,
			.tseg2_min = 1,
			.tseg2_max = 16,
			.sjw_max = 16,
			.brp_min = 1,
			.brp_max = 256,
			.brp_inc = 1,
		},
		.data_bittiming_const = {
			.name = "xilinx_can_fd",
			.tseg1_min = 1,
			.tseg1_max = 16,
			.tseg2_min = 1,
			.tseg2_max = 8,
			.sjw_max = 8,
			.brp_min = 1,
			.brp_max = 256,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 20000000, .name = "CIA recommendation" },
			{ .clk = 40000000, .name = "CIA recommendation" },
		},
	}, {
		.bittiming_const = {
			.name = "xilinx_can_fd2",
			.tseg1_min = 1,
			.tseg1_max = 256,
			.tseg2_min = 1,
			.tseg2_max = 128,
			.sjw_max = 128,
			.brp_min = 2,
			.brp_max = 256,
			.brp_inc = 1,
		},
		.data_bittiming_const = {
			.name = "xilinx_can_fd2",
			.tseg1_min = 1,
			.tseg1_max = 32,
			.tseg2_min = 1,
			.tseg2_max = 16,
			.sjw_max = 16,
			.brp_min = 2,
			.brp_max = 256,
			.brp_inc = 1,
		},
		.ref_clk = {
			{ .clk = 20000000, .name = "CIA recommendation" },
			{ .clk = 40000000, .name = "CIA recommendation" },
			{ .clk = 79999999, .name = "Versal ACAP" },
			{ .clk = 80000000, .name = "Versal ACAP" },
		},
	},
};

static const unsigned int common_bitrates[] = {
	1000000,
	800000,
	666666,
	500000,
	250000,
	125000,
	100000,
	83333,
	50000,
	33333,
	20000,
	10000,
	0
};

static const unsigned int common_data_bitrates[] = {
	12000000,
	10000000,
	8000000,
	5000000,
	4000000,
	2000000,
	1000000,
	0
};

#define CAN_CALC_MAX_ERROR 50 /* in one-tenth of a percent */
#define CAN_CALC_SYNC_SEG 1
#define CAN_SYNC_SEG 1
#define CAN_KBPS 1000
#define KILO 1000UL

#define can_update_spt can_update_spt_v2_6_31
#define can_calc_bittiming can_calc_bittiming_v2_6_31
#define can_fixup_bittiming can_fixup_bittiming_v2_6_31
#include "can-calc-bit-timing-v2_6_31.c"
#undef can_update_spt
#undef can_calc_bittiming
#undef can_fixup_bittiming

#define can_update_spt can_update_spt_v3_18
#define can_calc_bittiming can_calc_bittiming_v3_18
#define can_fixup_bittiming can_fixup_bittiming_v3_18
#include "can-calc-bit-timing-v3_18.c"
#undef can_update_spt
#undef can_calc_bittiming
#undef can_fixup_bittiming

#define can_update_sample_point can_update_sample_point_v4_8
#define can_calc_bittiming can_calc_bittiming_v4_8
#define can_fixup_bittiming can_fixup_bittiming_v4_8
#include "can-calc-bit-timing-v4_8.c"
#undef can_update_sample_point
#undef can_calc_bittiming
#undef can_fixup_bittiming

#define can_update_sample_point can_update_sample_point_v5_16
#define can_calc_bittiming can_calc_bittiming_v5_16
#define can_fixup_bittiming can_fixup_bittiming_v5_16
#include "can-calc-bit-timing-v5_16.c"
#undef can_update_sample_point
#undef can_calc_bittiming
#undef can_fixup_bittiming

#define can_update_sample_point can_update_sample_point_v5_19
#define can_calc_bittiming can_calc_bittiming_v5_19
#define can_fixup_bittiming can_fixup_bittiming_v5_19
#include "can-calc-bit-timing-v5_19.c"
#undef can_update_sample_point
#undef can_calc_bittiming
#undef can_fixup_bittiming

static const struct alg alg_list[] = {
	/* 1st will be default */
	{
		.calc_bittiming_const = can_calc_bittiming_v5_19,
		.fixup_bittiming_const = can_fixup_bittiming_v5_19,
		.name = "v5.19",
	}, {
		.calc_bittiming = can_calc_bittiming_v5_16,
		.fixup_bittiming = can_fixup_bittiming_v5_16,
		.name = "v5.16",
	}, {
		.calc_bittiming = can_calc_bittiming_v4_8,
		.fixup_bittiming = can_fixup_bittiming_v4_8,
		.name = "v4.8",
	}, {
		.calc_bittiming = can_calc_bittiming_v3_18,
		.fixup_bittiming = can_fixup_bittiming_v3_18,
		.name = "v3.18",
	}, {
		.calc_bittiming = can_calc_bittiming_v2_6_31,
		.fixup_bittiming = can_fixup_bittiming_v2_6_31,
		.name = "v2.6.31",
	},
};

static __u32 get_cia_sample_point(__u32 bitrate)
{
	__u32 sampl_pt;

	if (bitrate > 800000)
		sampl_pt = 750;
	else if (bitrate > 500000)
		sampl_pt = 800;
	else
		sampl_pt = 875;

	return sampl_pt;
}

static void print_bittiming_one(const struct alg *alg,
				const struct can_bittiming_const *bittiming_const,
				const struct can_bittiming *ref_bt,
				const struct calc_ref_clk *ref_clk,
				unsigned int bitrate_nominal,
				unsigned int sample_point_nominal,
				void (*printf_btr)(struct can_bittiming *bt, bool hdr),
				bool quiet,
				bool fd_mode)
{
	struct net_device dev = {
		.priv.clock.freq = ref_clk->clk,
	};
	struct can_bittiming bt = {
		.bitrate = bitrate_nominal,
		.sample_point = sample_point_nominal,
	};
	unsigned int bitrate_error, sample_point_error;

	if (!quiet) {
		printf("%sBit timing parameters for %s with %.6f MHz ref clock %s%s%susing algo '%s'\n"
		       " nominal                                  real  Bitrt    nom   real  SampP\n"
		       " Bitrate TQ[ns] PrS PhS1 PhS2 SJW BRP  Bitrate  Error  SampP  SampP  Error   ",
		       fd_mode ? "Data " : "",
		       bittiming_const->name,
		       ref_clk->clk / 1000000.0,
		       ref_clk->name ? "(" : "",
		       ref_clk->name ? ref_clk->name : "",
		       ref_clk->name ? ") " : "",
		       alg->name);

		printf_btr(&bt, true);
		printf("\n");
	}

	if (ref_bt) {
		bt = *ref_bt;

		if (alg->fixup_bittiming(&dev, &bt, bittiming_const)) {
			printf("%8d ***parameters exceed controller's range***\n", bitrate_nominal);
			return;
		}
	} else {
		if (alg->calc_bittiming(&dev, &bt, bittiming_const)) {
			printf("%8d ***bitrate not possible***\n", bitrate_nominal);
			return;
		}
	}

	bitrate_error = abs(bitrate_nominal - bt.bitrate);
	sample_point_error = abs(sample_point_nominal - bt.sample_point);

	printf("%8d "				/* Bitrate */
	       "%6d %3d %4d %4d "		/* TQ[ns], PrS, PhS1, PhS2 */
	       "%3d %3d "			/* SJW, BRP */
	       "%8d  ",				/* real Bitrate */
	       bitrate_nominal,
	       bt.tq, bt.prop_seg, bt.phase_seg1, bt.phase_seg2,
	       bt.sjw, bt.brp,
	       bt.bitrate);

	if (100.0 * bitrate_error / bitrate_nominal > 99.9)
		printf("≥100%%  ");
	else
		printf("%4.1f%%  ",		/* Bitrate Error */
		       100.0 * bitrate_error / bitrate_nominal);

	printf("%4.1f%%  %4.1f%%  ",		/* nom Sample Point, real Sample Point */
	       sample_point_nominal / 10.0,
	       bt.sample_point / 10.0);

	if (100.0 * sample_point_error / sample_point_nominal > 99.9)
		printf("≥100%%   ");
	else
		printf("%4.1f%%   ",		/* Sample Point Error */
		       100.0 * sample_point_error / sample_point_nominal);

	printf_btr(&bt, false);
	printf("\n");
}

static void print_bittiming(const struct calc_data *data)
{
	const struct calc_ref_clk *ref_clks = data->ref_clks;

	if (!ref_clks->clk && !data->quiet)
		printf("Skipping bit timing parameter calculation for %s, no ref clock defined\n\n",
		       data->bittiming_const->name);

	while (ref_clks->clk) {
		void (*printf_btr)(struct can_bittiming *bt, bool hdr);
		unsigned int const *bitrates = data->bitrates;
		bool quiet = data->quiet;

		if (data->printf_btr)
			printf_btr = data->printf_btr;
		else
			printf_btr = printf_btr_nop;

		while (*bitrates) {
			unsigned int sample_point;

			/* get nominal sample point */
			if (data->sample_point)
				sample_point = data->sample_point;
			else
				sample_point = get_cia_sample_point(*bitrates);

			print_bittiming_one(data->alg,
					    data->bittiming_const,
					    data->opt_bt,
					    ref_clks,
					    *bitrates,
					    sample_point,
					    printf_btr,
					    quiet,
					    data->fd_mode);
			bitrates++;
			quiet = true;
		}

		printf("\n");
		ref_clks++;
	}
}

static void do_list_calc_bittiming_list(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(alg_list); i++)
		printf("    %s\n", alg_list[i].name);
}

static void do_list(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(can_calc_consts); i++)
		printf("%s\n", can_calc_consts[i].bittiming_const.name);
}

static void do_calc(struct calc_data *data)
{
	unsigned int i;
	bool found = false;

	for (i = 0; i < ARRAY_SIZE(can_calc_consts); i++) {
		const struct calc_bittiming_const *btc;

		btc = &can_calc_consts[i];

		if (data->name &&
		    strcmp(data->name, btc->bittiming_const.name) &&
		    strcmp(data->name, btc->data_bittiming_const.name))
			continue;

		found = true;

		if (btc->bittiming_const.name[0]) {
			data->bittiming_const = &btc->bittiming_const;
			data->printf_btr = btc->printf_btr;

			if (data->opt_ref_clk)
				data->ref_clks = data->opt_ref_clk;
			else
				data->ref_clks = btc->ref_clk;

			if (data->opt_bitrates)
				data->bitrates = data->opt_bitrates;
			else
				data->bitrates = common_bitrates;

			data->fd_mode = false;

			print_bittiming(data);
		}

		if (btc->data_bittiming_const.name[0]) {
			data->bittiming_const = &btc->data_bittiming_const;

			if (btc->printf_data_btr)
				data->printf_btr = btc->printf_data_btr;
			else
				data->printf_btr = btc->printf_btr;

			if (data->opt_ref_clk)
				data->ref_clks = data->opt_ref_clk;
			else
				data->ref_clks = btc->ref_clk;

			if (data->opt_data_bitrates)
				data->bitrates = data->opt_data_bitrates;
			else if (data->opt_bitrates)
				data->bitrates = data->opt_bitrates;
			else
				data->bitrates = common_data_bitrates;

			data->fd_mode = true;

			print_bittiming(data);
		}
	}

	if (!found) {
		printf("error: unknown CAN controller '%s', try one of these:\n\n", data->name);
		do_list();
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char *argv[])
{
	struct calc_ref_clk opt_ref_clk[] = {
		{ .name = "cmd-line" },
		{ /* sentinel */ }
	};
	struct can_bittiming opt_bt[1] = { };
	unsigned int opt_bitrate[] = {
		0,
		0 /* sentinel */
	};
	unsigned int opt_data_bitrate[] = {
		0,
		0 /* sentinel */
	};
	struct calc_data data[] = {
		{
			.alg = alg_list,
		}
	};
	const char *opt_alg_name = NULL;
	bool list = false;
	int opt;

	const struct option long_options[] = {
		{ "tq",		required_argument,	0, OPT_TQ, },
		{ "prop-seg",	required_argument,	0, OPT_PROP_SEG, },
		{ "phase-seg1",	required_argument,	0, OPT_PHASE_SEG1, },
		{ "phase-seg2",	required_argument,	0, OPT_PHASE_SEG2, },
		{ "sjw",	required_argument,	0, OPT_SJW, },
		{ "brp",	required_argument,	0, OPT_BRP, },
		{ "tseg1",	required_argument,	0, OPT_TSEG1, },
		{ "tseg2",	required_argument,	0, OPT_TSEG2, },
		{ "alg",	optional_argument,	0, OPT_ALG, },
		{ 0,		0,			0, 0 },
	};

	while ((opt = getopt_long(argc, argv, "b:c:d:lqs:?", long_options, NULL)) != -1) {
		switch (opt) {
		case 'b':
			opt_bitrate[0] = strtoul(optarg, NULL, 10);
			break;

		case 'c':
			opt_ref_clk->clk = strtoul(optarg, NULL, 10);
			break;

		case 'd':
			opt_data_bitrate[0] = strtoul(optarg, NULL, 10);
			break;

		case 'l':
			list = true;
			break;

		case 'q':
			data->quiet = true;
			break;

		case 's':
			data->sample_point = strtoul(optarg, NULL, 10);
			break;

		case '?':
			print_usage(basename(argv[0]));
			exit(EXIT_SUCCESS);
			break;

		case OPT_TQ:
			opt_bt->tq = strtoul(optarg, NULL, 10);
			break;

		case OPT_PROP_SEG:
			opt_bt->prop_seg = strtoul(optarg, NULL, 10);
			break;

		case OPT_PHASE_SEG1:
			opt_bt->phase_seg1 = strtoul(optarg, NULL, 10);
			break;

		case OPT_PHASE_SEG2:
			opt_bt->phase_seg2 = strtoul(optarg, NULL, 10);
			break;

		case OPT_SJW:
			opt_bt->sjw = strtoul(optarg, NULL, 10);
			break;

		case OPT_BRP:
			opt_bt->brp = strtoul(optarg, NULL, 10);
			break;

		case OPT_TSEG1: {
			__u32 tseg1;

			tseg1 = strtoul(optarg, NULL, 10);
			opt_bt->prop_seg = tseg1 / 2;
			opt_bt->phase_seg1 = tseg1 - opt_bt->prop_seg;
			break;
		}

		case OPT_TSEG2:
			opt_bt->phase_seg2 = strtoul(optarg, NULL, 10);
			break;

		case OPT_ALG:
			if (!optarg) {
				printf("Supported CAN calc bit timing algorithms:\n\n");
				do_list_calc_bittiming_list();
				printf("\n");
				exit(EXIT_SUCCESS);
			}
			opt_alg_name = optarg;
			break;

		default:
			print_usage(basename(argv[0]));
			exit(EXIT_FAILURE);
			break;
		}
	}

	if (argc > optind + 1) {
		print_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	if (argc == optind + 1)
		data->name = argv[optind];

	if (list) {
		do_list();
		exit(EXIT_SUCCESS);
	}

	if (data->sample_point && (data->sample_point >= 1000 || data->sample_point < 100))
		print_usage(argv[0]);

	if (opt_alg_name) {
		bool alg_found = false;
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(alg_list); i++) {
			if (!strcmp(opt_alg_name, alg_list[i].name)) {
				data->alg = &alg_list[i];
				alg_found = true;
			}
		}

		if (!alg_found) {
			printf("error: unknown CAN calc bit timing algorithm '%s', try one of these:\n\n", opt_alg_name);
			do_list_calc_bittiming_list();
			exit(EXIT_FAILURE);
		}
	}

	if (opt_ref_clk->clk)
		data->opt_ref_clk = opt_ref_clk;
	if (opt_bitrate[0])
		data->opt_bitrates = opt_bitrate;
	if (opt_data_bitrate[0])
		data->opt_data_bitrates = opt_data_bitrate;
	if (opt_bt->prop_seg)
		data->opt_bt = opt_bt;

	do_calc(data);

	exit(EXIT_SUCCESS);
}
