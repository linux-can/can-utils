/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * imported from v6.3-rc1~162^2~124^2^2~1
 *
 */

void can_sjw_set_default(struct can_bittiming *bt)
{
	if (bt->sjw)
		return;

	/* If user space provides no sjw, use sane default of phase_seg2 / 2 */
	bt->sjw = max(1U, min(bt->phase_seg1, bt->phase_seg2 / 2));
}

int can_sjw_check(const struct net_device *dev, const struct can_bittiming *bt,
		  const struct can_bittiming_const *btc, struct netlink_ext_ack *extack)
{
	if (bt->sjw > btc->sjw_max) {
		NL_SET_ERR_MSG_FMT(extack, "sjw: %u greater than max sjw: %u",
				   bt->sjw, btc->sjw_max);
		return -EINVAL;
	}

	if (bt->sjw > bt->phase_seg1) {
		NL_SET_ERR_MSG_FMT(extack,
				   "sjw: %u greater than phase-seg1: %u",
				   bt->sjw, bt->phase_seg1);
		return -EINVAL;
	}

	if (bt->sjw > bt->phase_seg2) {
		NL_SET_ERR_MSG_FMT(extack,
				   "sjw: %u greater than phase-seg2: %u",
				   bt->sjw, bt->phase_seg2);
		return -EINVAL;
	}

	return 0;
}

/*
 * can_bit_time() - Duration of one bit
 *
 * Please refer to ISO 11898-1:2015, section 11.3.1.1 "Bit time" for
 * additional information.
 *
 * Return: the number of time quanta in one bit.
 */
static inline unsigned int can_bit_time(const struct can_bittiming *bt)
{
	return CAN_SYNC_SEG + bt->prop_seg + bt->phase_seg1 + bt->phase_seg2;
}

/* Copyright (C) 2005 Marc Kleine-Budde, Pengutronix
 * Copyright (C) 2006 Andrey Volkov, Varma Electronics
 * Copyright (C) 2008-2009 Wolfgang Grandegger <wg@grandegger.com>
 */

/* Bit-timing calculation derived from:
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
static int
can_update_sample_point(const struct can_bittiming_const *btc,
			const unsigned int sample_point_nominal, const unsigned int tseg,
			unsigned int *tseg1_ptr, unsigned int *tseg2_ptr,
			unsigned int *sample_point_error_ptr)
{
	unsigned int sample_point_error, best_sample_point_error = UINT_MAX;
	unsigned int sample_point, best_sample_point = 0;
	unsigned int tseg1, tseg2;
	int i;

	for (i = 0; i <= 1; i++) {
		tseg2 = tseg + CAN_SYNC_SEG -
			(sample_point_nominal * (tseg + CAN_SYNC_SEG)) /
			1000 - i;
		tseg2 = clamp(tseg2, btc->tseg2_min, btc->tseg2_max);
		tseg1 = tseg - tseg2;
		if (tseg1 > btc->tseg1_max) {
			tseg1 = btc->tseg1_max;
			tseg2 = tseg - tseg1;
		}

		sample_point = 1000 * (tseg + CAN_SYNC_SEG - tseg2) /
			(tseg + CAN_SYNC_SEG);
		sample_point_error = abs(sample_point_nominal - sample_point);

		if (sample_point <= sample_point_nominal &&
		    sample_point_error < best_sample_point_error) {
			best_sample_point = sample_point;
			best_sample_point_error = sample_point_error;
			*tseg1_ptr = tseg1;
			*tseg2_ptr = tseg2;
		}
	}

	if (sample_point_error_ptr)
		*sample_point_error_ptr = best_sample_point_error;

	return best_sample_point;
}

int can_calc_bittiming(const struct net_device *dev, struct can_bittiming *bt,
		       const struct can_bittiming_const *btc, struct netlink_ext_ack *extack)
{
	struct can_priv *priv = netdev_priv(dev);
	unsigned int bitrate;			/* current bitrate */
	unsigned int bitrate_error;		/* difference between current and nominal value */
	unsigned int best_bitrate_error = UINT_MAX;
	unsigned int sample_point_error;	/* difference between current and nominal value */
	unsigned int best_sample_point_error = UINT_MAX;
	unsigned int sample_point_nominal;	/* nominal sample point */
	unsigned int best_tseg = 0;		/* current best value for tseg */
	unsigned int best_brp = 0;		/* current best value for brp */
	unsigned int brp, tsegall, tseg, tseg1 = 0, tseg2 = 0;
	u64 v64;
	int err;

	/* Use CiA recommended sample points */
	if (bt->sample_point) {
		sample_point_nominal = bt->sample_point;
	} else {
		if (bt->bitrate > 800 * KILO /* BPS */)
			sample_point_nominal = 750;
		else if (bt->bitrate > 500 * KILO /* BPS */)
			sample_point_nominal = 800;
		else
			sample_point_nominal = 875;
	}

	/* tseg even = round down, odd = round up */
	for (tseg = (btc->tseg1_max + btc->tseg2_max) * 2 + 1;
	     tseg >= (btc->tseg1_min + btc->tseg2_min) * 2; tseg--) {
		tsegall = CAN_SYNC_SEG + tseg / 2;

		/* Compute all possible tseg choices (tseg=tseg1+tseg2) */
		brp = priv->clock.freq / (tsegall * bt->bitrate) + tseg % 2;

		/* choose brp step which is possible in system */
		brp = (brp / btc->brp_inc) * btc->brp_inc;
		if (brp < btc->brp_min || brp > btc->brp_max)
			continue;

		bitrate = priv->clock.freq / (brp * tsegall);
		bitrate_error = abs(bt->bitrate - bitrate);

		/* tseg brp biterror */
		if (bitrate_error > best_bitrate_error)
			continue;

		/* reset sample point error if we have a better bitrate */
		if (bitrate_error < best_bitrate_error)
			best_sample_point_error = UINT_MAX;

		can_update_sample_point(btc, sample_point_nominal, tseg / 2,
					&tseg1, &tseg2, &sample_point_error);
		if (sample_point_error >= best_sample_point_error)
			continue;

		best_sample_point_error = sample_point_error;
		best_bitrate_error = bitrate_error;
		best_tseg = tseg / 2;
		best_brp = brp;

		if (bitrate_error == 0 && sample_point_error == 0)
			break;
	}

	if (best_bitrate_error) {
		/* Error in one-tenth of a percent */
		v64 = (u64)best_bitrate_error * 1000;
		do_div(v64, bt->bitrate);
		bitrate_error = (u32)v64;
		if (bitrate_error > CAN_CALC_MAX_ERROR) {
			NL_SET_ERR_MSG_FMT(extack,
					   "bitrate error: %u.%u%% too high",
					   bitrate_error / 10, bitrate_error % 10);
			return -EINVAL;
		}
		NL_SET_ERR_MSG_FMT(extack,
				   "bitrate error: %u.%u%%",
				   bitrate_error / 10, bitrate_error % 10);
	}

	/* real sample point */
	bt->sample_point = can_update_sample_point(btc, sample_point_nominal,
						   best_tseg, &tseg1, &tseg2,
						   NULL);

	v64 = (u64)best_brp * 1000 * 1000 * 1000;
	do_div(v64, priv->clock.freq);
	bt->tq = (u32)v64;
	bt->prop_seg = tseg1 / 2;
	bt->phase_seg1 = tseg1 - bt->prop_seg;
	bt->phase_seg2 = tseg2;

	can_sjw_set_default(bt);

	err = can_sjw_check(dev, bt, btc, extack);
	if (err)
		return err;

	bt->brp = best_brp;

	/* real bitrate */
	bt->bitrate = priv->clock.freq /
		(bt->brp * can_bit_time(bt));

	return 0;
}

/* Checks the validity of the specified bit-timing parameters prop_seg,
 * phase_seg1, phase_seg2 and sjw and tries to determine the bitrate
 * prescaler value brp. You can find more information in the header
 * file linux/can/netlink.h.
 */
static int can_fixup_bittiming(const struct net_device *dev, struct can_bittiming *bt,
			       const struct can_bittiming_const *btc,
			       struct netlink_ext_ack *extack)
{
	const unsigned int tseg1 = bt->prop_seg + bt->phase_seg1;
	const struct can_priv *priv = netdev_priv(dev);
	u64 brp64;
	int err;

	if (tseg1 < btc->tseg1_min) {
		NL_SET_ERR_MSG_FMT(extack, "prop-seg + phase-seg1: %u less than tseg1-min: %u",
				   tseg1, btc->tseg1_min);
		return -EINVAL;
	}
	if (tseg1 > btc->tseg1_max) {
		NL_SET_ERR_MSG_FMT(extack, "prop-seg + phase-seg1: %u greater than tseg1-max: %u",
				   tseg1, btc->tseg1_max);
		return -EINVAL;
	}
	if (bt->phase_seg2 < btc->tseg2_min) {
		NL_SET_ERR_MSG_FMT(extack, "phase-seg2: %u less than tseg2-min: %u",
				   bt->phase_seg2, btc->tseg2_min);
		return -EINVAL;
	}
	if (bt->phase_seg2 > btc->tseg2_max) {
		NL_SET_ERR_MSG_FMT(extack, "phase-seg2: %u greater than tseg2-max: %u",
				   bt->phase_seg2, btc->tseg2_max);
		return -EINVAL;
	}

	can_sjw_set_default(bt);

	err = can_sjw_check(dev, bt, btc, extack);
	if (err)
		return err;

	brp64 = (u64)priv->clock.freq * (u64)bt->tq;
	if (btc->brp_inc > 1)
		do_div(brp64, btc->brp_inc);
	brp64 += 500000000UL - 1;
	do_div(brp64, 1000000000UL); /* the practicable BRP */
	if (btc->brp_inc > 1)
		brp64 *= btc->brp_inc;
	bt->brp = (u32)brp64;

	if (bt->brp < btc->brp_min) {
		NL_SET_ERR_MSG_FMT(extack, "resulting brp: %u less than brp-min: %u",
				   bt->brp, btc->brp_min);
		return -EINVAL;
	}
	if (bt->brp > btc->brp_max) {
		NL_SET_ERR_MSG_FMT(extack, "resulting brp: %u greater than brp-max: %u",
				   bt->brp, btc->brp_max);
		return -EINVAL;
	}

	bt->bitrate = priv->clock.freq / (bt->brp * can_bit_time(bt));
	bt->sample_point = ((CAN_SYNC_SEG + tseg1) * 1000) / can_bit_time(bt);
	bt->tq = DIV_U64_ROUND_CLOSEST(mul_u32_u32(bt->brp, NSEC_PER_SEC),
				       priv->clock.freq);

	return 0;
}
