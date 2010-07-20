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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>

#define do_div(a,b) a = (a) / (b)

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

	exit(1);
}

struct can_bittime {
	uint32_t brp;
	uint8_t prop_seg;
	uint8_t phase_seg1;
	uint8_t phase_seg2;
	uint8_t sjw;
	uint32_t tq;
	uint32_t error;
	int sampl_pt;
};

struct can_bittiming_const {
	char name[32];
	int prop_seg_min;
	int prop_seg_max;
	int phase_seg1_min;
	int phase_seg1_max;
	int phase_seg2_min;
	int phase_seg2_max;
	int sjw_max;
	int brp_min;
	int brp_max;
	int brp_inc;
	void (*printf_btr)(struct can_bittime *bt, int hdr);
};

static void printf_btr_sja1000(struct can_bittime *bt, int hdr)
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

static void printf_btr_at91(struct can_bittime *bt, int hdr)
{
	if (hdr) {
		printf("CAN_BR");
	} else {
		uint32_t br = ((bt->phase_seg2 - 1) |
			       ((bt->phase_seg1 - 1) << 4) |
			       ((bt->prop_seg - 1) << 8) |
			       ((bt->sjw - 1) << 12) |
			       ((bt->brp - 1) << 16));
		printf("0x%08x", br);
	}
}

static void printf_btr_mcp2510(struct can_bittime *bt, int hdr)
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

static void printf_btr_rtcantl1(struct can_bittime *bt, int hdr)
{
	uint16_t bcr0, bcr1;

	if (hdr) {
		printf("__BCR0 __BCR1");
	} else {
		bcr1 = ((((bt->prop_seg + bt->phase_seg1 - 1) & 0x0F) << 12) |
			(((bt->phase_seg2 - 1) & 0x07) << 8) |
			(((bt->sjw - 1) & 0x03) << 4));
		bcr0 =  ((bt->brp - 1) & 0xFF);
		printf("0x%04x 0x%04x", bcr0, bcr1);
	}
}

struct can_bittiming_const can_calc_consts[] = {
	{
		"sja1000",
		/* Note: only prop_seg + bt->phase_seg1 matters */
		.phase_seg1_min = 1,
		.phase_seg1_max = 16,
		.phase_seg2_min = 1,
		.phase_seg2_max = 8,
		.sjw_max = 4,
		.brp_min = 1,
		.brp_max = 64,
		.brp_inc = 1,
		.printf_btr = printf_btr_sja1000,
	},
	{
		"mscan",
		/* Note: only prop_seg + bt->phase_seg1 matters */
		.phase_seg1_min = 4,
		.phase_seg1_max = 16,
		.phase_seg2_min = 2,
		.phase_seg2_max = 8,
		.sjw_max = 4,
		.brp_min = 1,
		.brp_max = 64,
		.brp_inc = 1,
		.printf_btr = printf_btr_sja1000,
	},
	{
		"at91",
		.prop_seg_min = 1,
		.prop_seg_max = 8,
		.phase_seg1_min = 1,
		.phase_seg1_max = 8,
		.phase_seg2_min = 2,
		.phase_seg2_max = 8,
		.sjw_max = 4,
		.brp_min = 1,
		.brp_max = 128,
		.brp_inc = 1,
		.printf_btr = printf_btr_at91,
	},
	{
		"mcp2510",
		.prop_seg_min = 1,
		.prop_seg_max = 8,
		.phase_seg1_min = 1,
		.phase_seg1_max = 8,
		.phase_seg2_min = 2,
		.phase_seg2_max = 8,
		.sjw_max = 4,
		.brp_min = 1,
		.brp_max = 64,
		.brp_inc = 1,
		.printf_btr = printf_btr_mcp2510,
	},
	{
		"rtcantl1",
		.prop_seg_min = 2,
		.prop_seg_max = 8,
		.phase_seg1_min = 2,
		.phase_seg1_max = 8,
		.phase_seg2_min = 2,
		.phase_seg2_max = 8,
		.sjw_max = 4,
		.brp_min = 1,
		.brp_max = 256,
		.brp_inc = 1,
		.printf_btr = printf_btr_rtcantl1,
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
	10000
};

static int can_update_spt(const struct can_bittiming_const *btc,
			  int sampl_pt, int tseg, int *tseg1, int *tseg2)
{
	*tseg2 = tseg + 1 - (sampl_pt * (tseg + 1)) / 1000;
	if (*tseg2 < btc->phase_seg2_min)
		*tseg2 = btc->phase_seg2_min;
	if (*tseg2 > btc->phase_seg2_max)
		*tseg2 = btc->phase_seg2_max;
	*tseg1 = tseg - *tseg2;
	if (*tseg1 > btc->prop_seg_max + btc->phase_seg1_max) {
		*tseg1 = btc->prop_seg_max + btc->phase_seg1_max;
		*tseg2 = tseg - *tseg1;
	}
	return 1000 * (tseg + 1 - *tseg2) / (tseg + 1);
}

int can_calc_bittiming(struct can_bittime *bt, long bitrate,
		       int sampl_pt, long clock,
		       const struct can_bittiming_const *btc)
{
	long best_error = 1000000000, error;
	int best_tseg = 0, best_brp = 0, brp = 0;
	int spt_error = 1000, spt = 0;
	long rate, best_rate = 0;
	int tseg = 0, tseg1 = 0, tseg2 = 0;
	uint64_t v64;

	if (sampl_pt == 0) {
		/* Use CIA recommended sample points */
		if (bitrate > 800000)
			sampl_pt = 750;
		else if (bitrate > 500000)
			sampl_pt = 800;
		else
			sampl_pt = 875;
	}

#ifdef DEBUG
	printf("tseg brp bitrate biterror\n");
#endif

	/* tseg even = round down, odd = round up */
	for (tseg = (btc->prop_seg_max + btc->phase_seg1_max +
		     btc->phase_seg2_max) * 2 + 1;
	     tseg >= (btc->prop_seg_min + btc->phase_seg1_min +
		      btc->phase_seg2_min) * 2; tseg--) {
		/* Compute all posibilities of tseg choices (tseg=tseg1+tseg2) */
		brp = clock / ((1 + tseg / 2) * bitrate) + tseg % 2;
		/* chose brp step which is possible in system */
		brp = (brp / btc->brp_inc) * btc->brp_inc;
		if ((brp < btc->brp_min) || (brp > btc->brp_max))
			continue;
		rate = clock / (brp * (1 + tseg / 2));
		error = bitrate - rate;
		/* tseg brp biterror */
#if DEBUG
		printf("%4d %3d %7ld %8ld %03d\n", tseg, brp, rate, error,
		       can_update_spt(btc, sampl_pt, tseg / 2,
				      &tseg1, &tseg2));
#endif
		if (error < 0)
			error = -error;
		if (error > best_error)
			continue;
		best_error = error;
		if (error == 0) {
			spt = can_update_spt(btc, sampl_pt, tseg / 2,
					     &tseg1, &tseg2);
			error = sampl_pt - spt;
			//printf("%d %d %d\n", sampl_pt, error, spt_error);
			if (error < 0)
				error = -error;
			if (error > spt_error)
				continue;
			spt_error = error;
			//printf("%d\n", spt_error);
		}
		//printf("error=%d\n", best_error);
		best_tseg = tseg / 2;
		best_brp = brp;
		best_rate = rate;
		if (error == 0)
			break;
	}

	if (best_error && (bitrate / best_error < 10))
		return -1;

	spt = can_update_spt(btc, sampl_pt, best_tseg,
			     &tseg1, &tseg2);

	if (tseg2 > tseg1) {
		/* sample point < 50% */
		bt->phase_seg1 = tseg1 / 2;
	} else {
		/* keep phase_seg{1,2} equal around the sample point */
		bt->phase_seg1 = tseg2;
	}
	bt->prop_seg = tseg1 - bt->phase_seg1;
	/* Check prop_seg range if necessary */
	if (btc->prop_seg_min || btc->prop_seg_max) {
		if (bt->prop_seg < btc->prop_seg_min)
			bt->prop_seg = btc->prop_seg_min;
		else if (bt->prop_seg > btc->prop_seg_max)
			bt->prop_seg = btc->prop_seg_max;
		bt->phase_seg1 = tseg1 - bt->prop_seg;
	}
	bt->phase_seg2 = tseg2;
	bt->sjw = 1;
	bt->brp = best_brp;
	bt->error = best_error;
	bt->sampl_pt = spt;
	v64 = (uint64_t)bt->brp * 1000000000UL;
	v64 /= clock;
	bt->tq = (int)v64;

	return 0;
}

void print_bit_timing(const struct can_bittiming_const *btc,
		      long bitrate, int sampl_pt, long ref_clk, int quiet)
{
	struct can_bittime bt;

	memset(&bt, 0, sizeof(bt));

	if (!quiet) {
		printf("Bit timing parameters for %s using %ldHz\n",
		       btc->name, ref_clk);
		printf("Bitrate TQ[ns] PrS PhS1 PhS2 SJW BRP SampP Error ");
		btc->printf_btr(&bt, 1);
		printf("\n");
	}

	if (can_calc_bittiming(&bt, bitrate, sampl_pt, ref_clk, btc)) {
		printf("%7ld ***bitrate not possible***\n", bitrate);
		return;
	}

	printf("%7ld %6d %3d %4d %4d %3d %3d %2d.%d%% %4.1f%% ",
	       bitrate, bt.tq, bt.prop_seg, bt.phase_seg1,
	       bt.phase_seg2, bt.sjw, bt.brp,
	       bt.sampl_pt / 10, bt.sampl_pt % 10,
	       (double)100 * bt.error / bitrate);
	btc->printf_btr(&bt, 0);
	printf("\n");
}

int main(int argc, char *argv[])
{
	long bitrate = 0;
	long ref_clk = 8000000;
	int sampl_pt = 0;
	int quiet = 0;
	int list = 0;
	char *name = NULL;
	int i, opt;

	const struct can_bittiming_const *btc = NULL;

	while ((opt = getopt(argc, argv, "b:c:lps:")) != -1) {
		switch (opt) {
		case 'b':
			bitrate = atoi(optarg);
			break;

		case 'c':
			ref_clk = atoi(optarg);
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
		for (i = 0; i < sizeof(can_calc_consts) /
			     sizeof(struct can_bittiming_const); i++)
			printf("%s\n", can_calc_consts[i].name);
		return 0;
	}

	if (sampl_pt && (sampl_pt >= 1000 || sampl_pt < 100))
		print_usage(argv[0]);

	if (name) {
		for (i = 0; i < sizeof(can_calc_consts) /
			     sizeof(struct can_bittiming_const); i++) {
			if (!strcmp(can_calc_consts[i].name, name)) {
				btc = &can_calc_consts[i];
				break;
			}
		}
		if (!btc)
			print_usage(argv[0]);

	} else {
		btc = &can_calc_consts[0];
	}

	if (bitrate) {
		print_bit_timing(btc, bitrate, sampl_pt, ref_clk, quiet);
	} else {
		for (i = 0; i < sizeof(common_bitrates) / sizeof(long); i++)
			print_bit_timing(btc, common_bitrates[i], sampl_pt,
					 ref_clk, i);
	}

	return 0;
}
