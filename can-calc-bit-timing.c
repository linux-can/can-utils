/* SPDX-License-Identifier: GPL-2.0-only */
/* can-calc-bit-timing.c: Calculate CAN bit timing parameters
 *
 * Copyright (C) 2008 Wolfgang Grandegger <wg@grandegger.com>
 * Copyright (C) 2016, 2021 Marc Kleine-Budde <mkl@pengutronix.de>
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
			(char)({ signed char __x = (x); __x<0?-__x:__x; }), \
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

# define do_div(n,base) ({					\
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
	char *name;
};

struct calc_bittiming_const {
	struct can_bittiming_const bittiming_const;

	const struct calc_ref_clk ref_clk[16];
	void (*printf_btr)(struct can_bittiming *bt, bool hdr);
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
	       "\t-b <bitrate>   bit-rate in bits/sec\n"
	       "\t-s <samp_pt>   sample-point in one-tenth of a percent\n"
	       "\t               or 0 for CIA recommended sample points\n"
	       "\t-c <clock>     real CAN system clock in Hz\n"
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

static void printf_btr_sja1000(struct can_bittiming *bt, bool hdr)
{
	uint8_t btr0, btr1;

	if (hdr) {
		printf("BTR0 BTR1");
	} else {
		btr0 = ((bt->brp - 1) & 0x3f) | (((bt->sjw - 1) & 0x3) << 6);
		btr1 = ((bt->prop_seg + bt->phase_seg1 - 1) & 0xf) |
			(((bt->phase_seg2 - 1) & 0x7) << 4);
		printf("0x%02x 0x%02x", btr0, btr1);
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
		printf("NBTCFG");
	} else {
		uint32_t nbtcfg = ((bt->brp - 1) << 24) |
			((bt->prop_seg + bt->phase_seg1 - 1) << 16) |
			((bt->phase_seg2 - 1) << 8) |
			(bt->sjw - 1);
		printf("0x%08x", nbtcfg);
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

static void printf_btr_c_can(struct can_bittiming *bt, bool hdr)
{
	if (hdr) {
		printf("%s", "  BTR  BRPEXT");
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

static struct calc_bittiming_const can_calc_consts[] = {
	{
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
			{ .clk = 8000000, },
		},
		.printf_btr = printf_btr_sja1000,
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
			{ .clk = 66500000, },
			{ .clk = 66666666, },
			{ .clk = 83368421, .name = "vybrid" },
		},
		.printf_btr = printf_btr_flexcan,
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
		.ref_clk = {
			{ .clk = 20000000, },
			{ .clk = 40000000, },
		},
		.printf_btr = printf_btr_mcp251xfd,
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
			.name = "bxcan",
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
		.ref_clk = {
			{ .clk = 40000000, },
		},
		.printf_btr = printf_btr_mcan,
	},
};

static long common_bitrates[] = {
	1000000,
	800000,
	500000,
	250000,
	125000,
	100000,
	50000,
	20000,
	10000,
};

#define CAN_CALC_MAX_ERROR 50 /* in one-tenth of a percent */
#define CAN_CALC_SYNC_SEG 1

/*
 * Bit-timing calculation derived from:
 *
 * Code based on LinCAN sources and H8S2638 project
 * Copyright 2004-2006 Pavel Pisa - DCE FELK CVUT cz
 * Copyright 2005      Stanislav Marek
 * email: pisa@cmp.felk.cvut.cz
 *
 * Calculates proper bit-timing parameters for a specified bit-rate
 * and sample-point, which can then be used to set the bit-timing
 * registers of the CAN controller. You can find more information
 * in the header file linux/can/netlink.h.
 */
static int can_update_spt(const struct can_bittiming_const *btc,
			  unsigned int spt_nominal, unsigned int tseg,
			  unsigned int *tseg1_ptr, unsigned int *tseg2_ptr,
			  unsigned int *spt_error_ptr)
{
	unsigned int spt_error, best_spt_error = UINT_MAX;
	unsigned int spt, best_spt = 0;
	unsigned int tseg1, tseg2;
	int i;

	for (i = 0; i <= 1; i++) {
		tseg2 = tseg + CAN_CALC_SYNC_SEG - (spt_nominal * (tseg + CAN_CALC_SYNC_SEG)) / 1000 - i;
		tseg2 = clamp(tseg2, btc->tseg2_min, btc->tseg2_max);
		tseg1 = tseg - tseg2;
		if (tseg1 > btc->tseg1_max) {
			tseg1 = btc->tseg1_max;
			tseg2 = tseg - tseg1;
		}

		spt = 1000 * (tseg + CAN_CALC_SYNC_SEG - tseg2) / (tseg + CAN_CALC_SYNC_SEG);
		spt_error = abs(spt_nominal - spt);

		if ((spt <= spt_nominal) && (spt_error < best_spt_error)) {
			best_spt = spt;
			best_spt_error = spt_error;
			*tseg1_ptr = tseg1;
			*tseg2_ptr = tseg2;
		}
	}

	if (spt_error_ptr)
		*spt_error_ptr = best_spt_error;

	return best_spt;
}

static int can_calc_bittiming(struct net_device *dev, struct can_bittiming *bt,
			      const struct can_bittiming_const *btc)
{
	struct can_priv *priv = netdev_priv(dev);
	unsigned int rate;		/* current bitrate */
	unsigned int rate_error;	/* difference between current and nominal value */
	unsigned int best_rate_error = UINT_MAX;
	unsigned int spt_error;		/* difference between current and nominal value */
	unsigned int best_spt_error = UINT_MAX;
	unsigned int spt_nominal;	/* nominal sample point */
	unsigned int best_tseg = 0;	/* current best value for tseg */
	unsigned int best_brp = 0;	/* current best value for brp */
	unsigned int brp, tsegall, tseg, tseg1 = 0, tseg2 = 0;
	u64 v64;

	/* Use CiA recommended sample points */
	if (bt->sample_point) {
		spt_nominal = bt->sample_point;
	} else {
		if (bt->bitrate > 800000)
			spt_nominal = 750;
		else if (bt->bitrate > 500000)
			spt_nominal = 800;
		else
			spt_nominal = 875;
	}

	/* tseg even = round down, odd = round up */
	for (tseg = (btc->tseg1_max + btc->tseg2_max) * 2 + 1;
	     tseg >= (btc->tseg1_min + btc->tseg2_min) * 2; tseg--) {
		tsegall = CAN_CALC_SYNC_SEG + tseg / 2;

		/* Compute all possible tseg choices (tseg=tseg1+tseg2) */
		brp = priv->clock.freq / (tsegall * bt->bitrate) + tseg % 2;

		/* choose brp step which is possible in system */
		brp = (brp / btc->brp_inc) * btc->brp_inc;
		if ((brp < btc->brp_min) || (brp > btc->brp_max))
			continue;

		rate = priv->clock.freq / (brp * tsegall);
		rate_error = abs(bt->bitrate - rate);

		/* tseg brp biterror */
		if (rate_error > best_rate_error)
			continue;

		/* reset sample point error if we have a better bitrate */
		if (rate_error < best_rate_error)
			best_spt_error = UINT_MAX;

		can_update_spt(btc, spt_nominal, tseg / 2, &tseg1, &tseg2, &spt_error);
		if (spt_error > best_spt_error)
			continue;

		best_spt_error = spt_error;
		best_rate_error = rate_error;
		best_tseg = tseg / 2;
		best_brp = brp;

		if (rate_error == 0 && spt_error == 0)
			break;
	}

	if (best_rate_error) {
		/* Error in one-tenth of a percent */
		rate_error = (best_rate_error * 1000) / bt->bitrate;
		if (rate_error > CAN_CALC_MAX_ERROR) {
			netdev_err(dev,
				   "bitrate error %ld.%ld%% too high\n",
				   rate_error / 10, rate_error % 10);
			return -EDOM;
		}
		netdev_warn(dev, "bitrate error %ld.%ld%%\n",
			    rate_error / 10, rate_error % 10);
	}

	/* real sample point */
	bt->sample_point = can_update_spt(btc, spt_nominal, best_tseg,
					  &tseg1, &tseg2, NULL);

	v64 = (u64)best_brp * 1000 * 1000 * 1000;
	do_div(v64, priv->clock.freq);
	bt->tq = (u32)v64;
	bt->prop_seg = tseg1 / 2;
	bt->phase_seg1 = tseg1 - bt->prop_seg;
	bt->phase_seg2 = tseg2;

	/* check for sjw user settings */
	if (!bt->sjw || !btc->sjw_max) {
		bt->sjw = 1;
	} else {
		/* bt->sjw is at least 1 -> sanitize upper bound to sjw_max */
		if (bt->sjw > btc->sjw_max)
			bt->sjw = btc->sjw_max;
		/* bt->sjw must not be higher than tseg2 */
		if (tseg2 < bt->sjw)
			bt->sjw = tseg2;
	}

	bt->brp = best_brp;

	/* real bit-rate */
	bt->bitrate = priv->clock.freq / (bt->brp * (CAN_CALC_SYNC_SEG + tseg1 + tseg2));

	return 0;
}

static int can_fixup_bittiming(struct net_device *dev, struct can_bittiming *bt,
			       const struct can_bittiming_const *btc)
{
	struct can_priv *priv = netdev_priv(dev);
	int tseg1, alltseg;
	u64 brp64, v64;

	tseg1 = bt->prop_seg + bt->phase_seg1;
	if (!bt->sjw)
		bt->sjw = 1;
	if (bt->sjw > btc->sjw_max ||
	    tseg1 < btc->tseg1_min || tseg1 > btc->tseg1_max ||
	    bt->phase_seg2 < btc->tseg2_min || bt->phase_seg2 > btc->tseg2_max)
		return -ERANGE;

	if (!bt->brp) {
		brp64 = (u64)priv->clock.freq * (u64)bt->tq;
		if (btc->brp_inc > 1)
			do_div(brp64, btc->brp_inc);
		brp64 += 500000000UL - 1;
		do_div(brp64, 1000000000UL); /* the practicable BRP */
		if (btc->brp_inc > 1)
			brp64 *= btc->brp_inc;
		bt->brp = brp64;
	}

	v64 = bt->brp * 1000 * 1000 * 1000;
	do_div(v64, priv->clock.freq);
	bt->tq = v64;

	if (bt->brp < btc->brp_min || bt->brp > btc->brp_max)
		return -EINVAL;

	alltseg = CAN_CALC_SYNC_SEG + bt->prop_seg + bt->phase_seg1 + bt->phase_seg2;
	bt->bitrate = priv->clock.freq / (bt->brp * alltseg);
	bt->sample_point = ((CAN_CALC_SYNC_SEG + tseg1) * 1000) / alltseg;

	return 0;
}

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

static void print_bit_timing(const struct calc_bittiming_const *btc,
			     const struct can_bittiming *ref_bt,
			     const struct calc_ref_clk *ref_clk,
			     unsigned int bitrate_nominal,
			     unsigned int spt_nominal,
			     bool quiet)
{
	struct net_device dev = {
		.priv.clock.freq = ref_clk->clk,
	};
	struct can_bittiming bt = {
		.bitrate = bitrate_nominal,
		.sample_point = spt_nominal,
	};
	long rate_error, spt_error;

	if (!quiet) {
		printf("Bit timing parameters for %s%s%s%s with %.6f MHz ref clock\n"
		       "nominal                                 real Bitrt   nom  real SampP\n"
		       "Bitrate TQ[ns] PrS PhS1 PhS2 SJW BRP Bitrate Error SampP SampP Error ",
		       btc->bittiming_const.name,
		       ref_clk->name ? " (" : "",
		       ref_clk->name ? ref_clk->name : "",
		       ref_clk->name ? ")" : "",
		       ref_clk->clk / 1000000.0);

		if (btc->printf_btr)
			btc->printf_btr(&bt, true);
		printf("\n");
	}

	if (ref_bt) {
		bt = *ref_bt;

		if (can_fixup_bittiming(&dev, &bt, &btc->bittiming_const)) {
			printf("%7d ***parameters exceed controller's range***\n", bitrate_nominal);
			return;
		}
	} else {
		if (can_calc_bittiming(&dev, &bt, &btc->bittiming_const)) {
			printf("%7d ***bitrate not possible***\n", bitrate_nominal);
			return;
		}
	}

	/* get nominal sample point */
	if (!spt_nominal)
		spt_nominal = get_cia_sample_point(bitrate_nominal);

	rate_error = abs(bitrate_nominal - bt.bitrate);
	spt_error = abs(spt_nominal - bt.sample_point);

	printf("%7d "				/* Bitrate */
	       "%6d %3d %4d %4d "		/* TQ[ns], PrS, PhS1, PhS2 */
	       "%3d %3d "			/* SJW, BRP */
	       "%7d ",				/* real Bitrate */
	       bitrate_nominal,
	       bt.tq, bt.prop_seg, bt.phase_seg1, bt.phase_seg2,
	       bt.sjw, bt.brp,
	       bt.bitrate);

	if (100.0 * rate_error / bitrate_nominal > 99.9)
		printf("≥100%% ");
	else
		printf("%4.1f%% ",
		       100.0 * rate_error / bitrate_nominal);

	printf("%4.1f%% %4.1f%% ",		/* nom SampP, real SampP */
	       spt_nominal / 10.0,
	       bt.sample_point / 10.0);

	if (100.0 * spt_error / spt_nominal > 99.9)
		printf("≥100%% ");
	else
		printf("%4.1f%% ",		/* SampP Error */
		       100.0 * spt_error / spt_nominal);

	if (btc->printf_btr)
		btc->printf_btr(&bt, false);
	printf("\n");
}

static void do_list(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(can_calc_consts); i++)
		printf("%s\n", can_calc_consts[i].bittiming_const.name);
}

static void do_calc(const char *name,
		    const struct can_bittiming *opt_ref_bt,
		    __u32 bitrate_nominal, unsigned int spt_nominal,
		    struct calc_ref_clk *opt_ref_clk, bool quiet)
{
	const struct calc_bittiming_const *btc;
	const struct calc_ref_clk *ref_clk;
	unsigned int i, j, k;
	bool found = false;

	for (i = 0; i < ARRAY_SIZE(can_calc_consts); i++) {
		if (name &&
		    strcmp(can_calc_consts[i].bittiming_const.name, name) != 0)
			continue;

		found = true;
		btc = &can_calc_consts[i];

		for (j = 0; j < ARRAY_SIZE(btc->ref_clk); j++) {
			if (opt_ref_clk)
				ref_clk = opt_ref_clk;
			else
				ref_clk = &btc->ref_clk[j];

			if (!ref_clk->clk)
				break;

			if (bitrate_nominal) {
				print_bit_timing(btc, opt_ref_bt, ref_clk, bitrate_nominal,
						 spt_nominal, quiet);
			} else {
				for (k = 0; k < ARRAY_SIZE(common_bitrates); k++)
					print_bit_timing(btc, opt_ref_bt, ref_clk,
							 common_bitrates[k],
							 spt_nominal, k);
			}
			printf("\n");

			if (opt_ref_clk)
				break;
		}
	}

	if (!found) {
		printf("error: unknown CAN controller '%s', try one of these:\n\n", name);
		do_list();
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char *argv[])
{
	__u32 bitrate_nominal = 0;
	unsigned int spt_nominal = 0;
	struct calc_ref_clk opt_ref_clk = {
		.name = "cmd-line",
	};
	struct can_bittiming bt = { 0 };
	bool quiet = false, list = false;
	const char *name = NULL;
	int opt;

	const struct option long_options[] = {
		{ "tq",         required_argument,	0, OPT_TQ, },
		{ "prop-seg",	required_argument,	0, OPT_PROP_SEG, },
		{ "phase-seg1",	required_argument,	0, OPT_PHASE_SEG1, },
		{ "phase-seg2",	required_argument,	0, OPT_PHASE_SEG2, },
		{ "sjw",	required_argument,	0, OPT_SJW, },
		{ "brp",	required_argument,	0, OPT_BRP, },
		{ "tseg1",	required_argument,	0, OPT_TSEG1, },
		{ "tseg2",	required_argument,	0, OPT_TSEG2, },
		{ 0,		0,			0, 0 },
	};

	while ((opt = getopt_long(argc, argv, "b:c:lqs:?", long_options, NULL)) != -1) {
		switch (opt) {
		case 'b':
			bitrate_nominal = atoi(optarg);
			break;

		case 'c':
			opt_ref_clk.clk = strtoul(optarg, NULL, 10);
			break;

		case 'l':
			list = true;
			break;

		case 'q':
			quiet = true;
			break;

		case 's':
			spt_nominal = strtoul(optarg, NULL, 10);
			break;

		case '?':
			print_usage(basename(argv[0]));
			exit(EXIT_SUCCESS);
			break;

		case OPT_TQ:
			bt.tq = strtoul(optarg, NULL, 10);
			break;

		case OPT_PROP_SEG:
			bt.prop_seg = strtoul(optarg, NULL, 10);
			break;

		case OPT_PHASE_SEG1:
			bt.phase_seg1 = strtoul(optarg, NULL, 10);
			break;

		case OPT_PHASE_SEG2:
			bt.phase_seg2 = strtoul(optarg, NULL, 10);
			break;

		case OPT_SJW:
			bt.sjw = strtoul(optarg, NULL, 10);
			break;

		case OPT_BRP:
			bt.brp = strtoul(optarg, NULL, 10);
			break;

		case OPT_TSEG1: {
			__u32 tseg1;

			tseg1 = strtoul(optarg, NULL, 10);
			bt.prop_seg = tseg1 / 2;
			bt.phase_seg1 = tseg1 - bt.prop_seg;
			break;
		}

		case OPT_TSEG2:
			bt.phase_seg2 = strtoul(optarg, NULL, 10);
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
		name = argv[optind];

	if (list) {
		do_list();
		exit(EXIT_SUCCESS);
	}

	if (spt_nominal && (spt_nominal >= 1000 || spt_nominal < 100)) {
		print_usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	do_calc(name,
		bt.prop_seg ? &bt: NULL,
		bitrate_nominal, spt_nominal,
		opt_ref_clk.clk ? &opt_ref_clk : NULL,
		quiet);

	exit(EXIT_SUCCESS);
}
