/* can-calc-bit-timing.c: Calculate CAN bit timing parameters
 *
 * Copyright (C) 2008 Wolfgang Grandegger <wg@grandegger.com>
 *
 * Derived from:
 *   can_baud.c - CAN baudrate calculation
 *   Code based on LinCAN sources and H8S2638 project
 *   Copyright 2004-2006 Pavel Pisa - DCE FELK CVUT cz
 *   Copyright 2005      Stanislav Marek
 *   email:pisa@cmp.felk.cvut.cz
 *
 *   This software is released under the GPL-License.
 */

#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/types.h>

/* seems not to be defined in errno.h */
#ifndef ENOTSUPP
#define ENOTSUPP	524	/* Operation is not supported */
#endif

/* usefull defines */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define do_div(a,b) a = (a) / (b)

#define abs(x) ({				\
		long __x = (x);			\
		(__x < 0) ? -__x : __x;		\
	})

/**
 * clamp - return a value clamped to a given range with strict typechecking
 * @val: current value
 * @min: minimum allowable value
 * @max: maximum allowable value
 *
 * This macro does strict typechecking of min/max to make sure they are of the
 * same type as val.  See the unnecessary pointer comparisons.
 */
#define clamp(val, min, max) ({			\
	typeof(val) __val = (val);		\
	typeof(min) __min = (min);		\
	typeof(max) __max = (max);		\
	(void) (&__val == &__min);		\
	(void) (&__val == &__max);		\
	__val = __val < __min ? __min: __val;	\
	__val > __max ? __max: __val; })

/* we don't want to see these prints */
#define dev_err(dev, format, arg...)	do { } while (0)
#define dev_warn(dev, format, arg...)	do { } while (0)

/* define in-kernel-types */
typedef __u64 u64;
typedef __u32 u32;


/*
 * CAN bit-timing parameters
 *
 * For futher information, please read chapter "8 BIT TIMING
 * REQUIREMENTS" of the "Bosch CAN Specification version 2.0"
 * at http://www.semiconductors.bosch.de/pdf/can2spec.pdf.
 */
struct can_bittiming {
	__u32 bitrate;		/* Bit-rate in bits/second */
	__u32 sample_point;	/* Sample point in one-tenth of a percent */
	__u32 tq;		/* Time quanta (TQ) in nanoseconds */
	__u32 prop_seg;		/* Propagation segment in TQs */
	__u32 phase_seg1;	/* Phase buffer segment 1 in TQs */
	__u32 phase_seg2;	/* Phase buffer segment 2 in TQs */
	__u32 sjw;		/* Synchronisation jump width in TQs */
	__u32 brp;		/* Bit-rate prescaler */
};

/*
 * CAN harware-dependent bit-timing constant
 *
 * Used for calculating and checking bit-timing parameters
 */
struct can_bittiming_const {
	char name[16];		/* Name of the CAN controller hardware */
	__u32 tseg1_min;	/* Time segement 1 = prop_seg + phase_seg1 */
	__u32 tseg1_max;
	__u32 tseg2_min;	/* Time segement 2 = phase_seg2 */
	__u32 tseg2_max;
	__u32 sjw_max;		/* Synchronisation jump width */
	__u32 brp_min;		/* Bit-rate prescaler */
	__u32 brp_max;
	__u32 brp_inc;

	/* added for can-calc-bit-timing utility */
	__u32 ref_clk;		/* CAN system clock frequency in Hz */
	void (*printf_btr)(struct can_bittiming *bt, int hdr);
};

/*
 * CAN clock parameters
 */
struct can_clock {
	__u32 freq;		/* CAN system clock frequency in Hz */
};


/*
 * minimal structs, just enough to be source level compatible
 */
struct can_priv {
	const struct can_bittiming_const *bittiming_const;
	struct can_clock clock;
};

struct net_device {
	struct can_priv	priv;
};

static inline void *netdev_priv(const struct net_device *dev)
{
	return (void *)&dev->priv;
}

static void print_usage(char* cmd)
{
	printf("Usage: %s [options] [<CAN-contoller-name>]\n"
	       "\tOptions:\n"
	       "\t-q           : don't print header line\n"
	       "\t-l           : list all support CAN controller names\n"
	       "\t-b <bitrate> : bit-rate in bits/sec\n"
	       "\t-s <samp_pt> : sample-point in one-tenth of a percent\n"
	       "\t               or 0 for CIA recommended sample points\n"
	       "\t-c <clock>   : real CAN system clock in Hz\n",
	       cmd);

	exit(EXIT_FAILURE);
}

static void printf_btr_sja1000(struct can_bittiming *bt, int hdr)
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

static void printf_btr_at91(struct can_bittiming *bt, int hdr)
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

static void printf_btr_flexcan(struct can_bittiming *bt, int hdr)
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

static void printf_btr_mcp251x(struct can_bittiming *bt, int hdr)
{
	uint8_t cnf1, cnf2, cnf3;

	if (hdr) {
		printf("CNF1 CNF2 CNF3");
	} else {
		cnf1 = ((bt->sjw - 1) << 6) | bt->brp;
		cnf2 = 0x80 | ((bt->phase_seg1 - 1) << 3) | (bt->prop_seg - 1);
		cnf3 = bt->phase_seg2 - 1;
		printf("0x%02x 0x%02x 0x%02x", cnf1, cnf2, cnf3);
	}
}

static struct can_bittiming_const can_calc_consts[] = {
	{
		.name = "sja1000",
		.tseg1_min = 1,
		.tseg1_max = 16,
		.tseg2_min = 1,
		.tseg2_max = 8,
		.sjw_max = 4,
		.brp_min = 1,
		.brp_max = 64,
		.brp_inc = 1,

		.ref_clk = 8000000,
		.printf_btr = printf_btr_sja1000,
	},
	{
		.name = "mscan",
		.tseg1_min = 4,
		.tseg1_max = 16,
		.tseg2_min = 2,
		.tseg2_max = 8,
		.sjw_max = 4,
		.brp_min = 1,
		.brp_max = 64,
		.brp_inc = 1,

		.ref_clk = 32000000,
		.printf_btr = printf_btr_sja1000,
	},
	{
		.name = "mscan",
		.tseg1_min = 4,
		.tseg1_max = 16,
		.tseg2_min = 2,
		.tseg2_max = 8,
		.sjw_max = 4,
		.brp_min = 1,
		.brp_max = 64,
		.brp_inc = 1,

		.ref_clk = 33000000,
		.printf_btr = printf_btr_sja1000,
	},
	{
		.name = "mscan",
		.tseg1_min = 4,
		.tseg1_max = 16,
		.tseg2_min = 2,
		.tseg2_max = 8,
		.sjw_max = 4,
		.brp_min = 1,
		.brp_max = 64,
		.brp_inc = 1,

		.ref_clk = 33300000,
		.printf_btr = printf_btr_sja1000,
	},
	{
		.name = "mscan",
		.tseg1_min = 4,
		.tseg1_max = 16,
		.tseg2_min = 2,
		.tseg2_max = 8,
		.sjw_max = 4,
		.brp_min = 1,
		.brp_max = 64,
		.brp_inc = 1,

		.ref_clk = 33333333,
		.printf_btr = printf_btr_sja1000,
	},
	{
		.name = "at91",
		.tseg1_min = 4,
		.tseg1_max = 16,
		.tseg2_min = 2,
		.tseg2_max = 8,
		.sjw_max = 4,
		.brp_min = 2,
		.brp_max = 128,
		.brp_inc = 1,

		.ref_clk = 100000000,
		.printf_btr = printf_btr_at91,
	},
	{
		.name = "at91",
		.tseg1_min = 4,
		.tseg1_max = 16,
		.tseg2_min = 2,
		.tseg2_max = 8,
		.sjw_max = 4,
		.brp_min = 2,
		.brp_max = 128,
		.brp_inc = 1,

		/* real world clock as found on the ronetix PM9263 */
		.ref_clk = 99532800,
		.printf_btr = printf_btr_at91,
	},
	{
		.name = "flexcan",
		.tseg1_min = 4,
		.tseg1_max = 16,
		.tseg2_min = 2,
		.tseg2_max = 8,
		.sjw_max = 4,
		.brp_min = 1,
		.brp_max = 256,
		.brp_inc = 1,

		.ref_clk = 49875000,
		.printf_btr = printf_btr_flexcan,
	},
	{
		.name = "flexcan",
		.tseg1_min = 4,
		.tseg1_max = 16,
		.tseg2_min = 2,
		.tseg2_max = 8,
		.sjw_max = 4,
		.brp_min = 1,
		.brp_max = 256,
		.brp_inc = 1,

		.ref_clk = 66500000,
		.printf_btr = printf_btr_flexcan,
	},
	{
		.name = "mcp251x",
		.tseg1_min = 3,
		.tseg1_max = 16,
		.tseg2_min = 2,
		.tseg2_max = 8,
		.sjw_max = 4,
		.brp_min = 1,
		.brp_max = 64,
		.brp_inc = 1,

		.ref_clk = 8000000,
		.printf_btr = printf_btr_mcp251x,
	},
	{
		.name = "mcp251x",
		.tseg1_min = 3,
		.tseg1_max = 16,
		.tseg2_min = 2,
		.tseg2_max = 8,
		.sjw_max = 4,
		.brp_min = 1,
		.brp_max = 64,
		.brp_inc = 1,

		.ref_clk = 16000000,
		.printf_btr = printf_btr_mcp251x,
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

static int can_update_spt(const struct can_bittiming_const *btc,
			  int sampl_pt, int tseg, int *tseg1, int *tseg2)
{
	*tseg2 = tseg + 1 - (sampl_pt * (tseg + 1)) / 1000;
	if (*tseg2 < btc->tseg2_min)
		*tseg2 = btc->tseg2_min;
	if (*tseg2 > btc->tseg2_max)
		*tseg2 = btc->tseg2_max;
	*tseg1 = tseg - *tseg2;
	if (*tseg1 > btc->tseg1_max) {
		*tseg1 = btc->tseg1_max;
		*tseg2 = tseg - *tseg1;
	}
	return 1000 * (tseg + 1 - *tseg2) / (tseg + 1);
}

static int can_calc_bittiming(struct net_device *dev, struct can_bittiming *bt)
{
	struct can_priv *priv = netdev_priv(dev);
	const struct can_bittiming_const *btc = priv->bittiming_const;
	long rate, best_rate = 0;
	long best_error = 1000000000, error = 0;
	int best_tseg = 0, best_brp = 0, brp = 0;
	int tsegall, tseg = 0, tseg1 = 0, tseg2 = 0;
	int spt_error = 1000, spt = 0, sampl_pt;
	u64 v64;

	if (!priv->bittiming_const)
		return -ENOTSUPP;

	/* Use CIA recommended sample points */
	if (bt->sample_point) {
		sampl_pt = bt->sample_point;
	} else {
		if (bt->bitrate > 800000)
			sampl_pt = 750;
		else if (bt->bitrate > 500000)
			sampl_pt = 800;
		else
			sampl_pt = 875;
	}

	/* tseg even = round down, odd = round up */
	for (tseg = (btc->tseg1_max + btc->tseg2_max) * 2 + 1;
	     tseg >= (btc->tseg1_min + btc->tseg2_min) * 2; tseg--) {
		tsegall = 1 + tseg / 2;
		/* Compute all possible tseg choices (tseg=tseg1+tseg2) */
		brp = priv->clock.freq / (tsegall * bt->bitrate) + tseg % 2;
		/* chose brp step which is possible in system */
		brp = (brp / btc->brp_inc) * btc->brp_inc;
		if ((brp < btc->brp_min) || (brp > btc->brp_max))
			continue;
		rate = priv->clock.freq / (brp * tsegall);
		error = bt->bitrate - rate;
		/* tseg brp biterror */
		if (error < 0)
			error = -error;
		if (error > best_error)
			continue;
		best_error = error;
		if (error == 0) {
			spt = can_update_spt(btc, sampl_pt, tseg / 2,
					     &tseg1, &tseg2);
			error = sampl_pt - spt;
			if (error < 0)
				error = -error;
			if (error > spt_error)
				continue;
			spt_error = error;
		}
		best_tseg = tseg / 2;
		best_brp = brp;
		best_rate = rate;
		if (error == 0)
			break;
	}

	if (best_error) {
		/* Error in one-tenth of a percent */
		error = (best_error * 1000) / bt->bitrate;
		if (error > CAN_CALC_MAX_ERROR) {
			dev_err(dev->dev.parent,
				"bitrate error %ld.%ld%% too high\n",
				error / 10, error % 10);
			return -EDOM;
		} else {
			dev_warn(dev->dev.parent, "bitrate error %ld.%ld%%\n",
				 error / 10, error % 10);
		}
	}

	/* real sample point */
	bt->sample_point = can_update_spt(btc, sampl_pt, best_tseg,
					  &tseg1, &tseg2);

	v64 = (u64)best_brp * 1000000000UL;
	do_div(v64, priv->clock.freq);
	bt->tq = (u32)v64;
	bt->prop_seg = tseg1 / 2;
	bt->phase_seg1 = tseg1 - bt->prop_seg;
	bt->phase_seg2 = tseg2;
	bt->sjw = 1;
	bt->brp = best_brp;

	/* real bit-rate */
	bt->bitrate = priv->clock.freq / (bt->brp * (tseg1 + tseg2 + 1));

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

static void print_bit_timing(const struct can_bittiming_const *btc,
			     __u32 bitrate, __u32 sample_point, __u32 ref_clk,
			     int quiet)
{
	struct net_device dev = {
		.priv.bittiming_const = btc,
		.priv.clock.freq = ref_clk,
	};
	struct can_bittiming bt = {
		.bitrate = bitrate,
		.sample_point = sample_point,
	};
	long rate_error, spt_error;

	if (!quiet) {
		printf("Bit timing parameters for %s with %.6f MHz ref clock\n"
		       "nominal                                 real Bitrt   nom  real SampP\n"
		       "Bitrate TQ[ns] PrS PhS1 PhS2 SJW BRP Bitrate Error SampP SampP Error ",
		       btc->name,
		       ref_clk / 1000000.0);

		btc->printf_btr(&bt, 1);
		printf("\n");
	}

	if (can_calc_bittiming(&dev, &bt)) {
		printf("%7d ***bitrate not possible***\n", bitrate);
		return;
	}

	/* get nominal sample point */
	if (!sample_point)
		sample_point = get_cia_sample_point(bitrate);

	rate_error = abs((__s32)(bitrate - bt.bitrate));
	spt_error = abs((__s32)(sample_point - bt.sample_point));

	printf("%7d "
	       "%6d %3d %4d %4d "
	       "%3d %3d "
	       "%7d %4.1f%% "
	       "%4.1f%% %4.1f%% %4.1f%% ",
	       bitrate,
	       bt.tq, bt.prop_seg, bt.phase_seg1, bt.phase_seg2,
	       bt.sjw, bt.brp,

	       bt.bitrate,
	       100.0 * rate_error / bitrate,

	       sample_point / 10.0,
	       bt.sample_point / 10.0,
	       100.0 * spt_error / sample_point);

	btc->printf_btr(&bt, 0);
	printf("\n");
}

static void do_list(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(can_calc_consts); i++)
		printf("%s\n", can_calc_consts[i].name);
}

int main(int argc, char *argv[])
{
	__u32 bitrate = 0;
	__u32 opt_ref_clk = 0, ref_clk;
	int sampl_pt = 0;
	int quiet = 0;
	int list = 0;
	char *name = NULL;
	unsigned int i, j;
	int opt, found = 0;

	const struct can_bittiming_const *btc = NULL;

	while ((opt = getopt(argc, argv, "b:c:lps:")) != -1) {
		switch (opt) {
		case 'b':
			bitrate = atoi(optarg);
			break;

		case 'c':
			opt_ref_clk = atoi(optarg);
			break;

		case 'l':
			list = 1;
			break;

		case 'q':
			quiet = 1;
			break;

		case 's':
			sampl_pt = atoi(optarg);
			break;

		default:
			print_usage(argv[0]);
			break;
		}
	}

	if (argc > optind + 1)
		print_usage(argv[0]);

	if (argc == optind + 1)
		name = argv[optind];

	if (list) {
		do_list();
		exit(EXIT_SUCCESS);
	}

	if (sampl_pt && (sampl_pt >= 1000 || sampl_pt < 100))
		print_usage(argv[0]);

	for (i = 0; i < ARRAY_SIZE(can_calc_consts); i++) {
		if (name && strcmp(can_calc_consts[i].name, name))
			continue;

		found = 1;
		btc = &can_calc_consts[i];

		if (opt_ref_clk)
			ref_clk = opt_ref_clk;
		else
			ref_clk = btc->ref_clk;

		if (bitrate) {
			print_bit_timing(btc, bitrate, sampl_pt, ref_clk, quiet);
		} else {
			for (j = 0; j < ARRAY_SIZE(common_bitrates); j++)
				print_bit_timing(btc, common_bitrates[j],
						 sampl_pt, ref_clk, j);
		}
		printf("\n");
	}

	if (!found) {
		printf("error: unknown CAN controller '%s', try one of these:\n\n", name);
		do_list();
		exit(EXIT_FAILURE);
	}

	exit(EXIT_SUCCESS);
}
