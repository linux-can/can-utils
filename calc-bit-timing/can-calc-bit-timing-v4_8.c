/* SPDX-License-Identifier: GPL-2.0-only */

/*
 * imported from v4.8-rc1~140^2~304^2~11
 *
 */

/*
 * Copyright (C) 2005 Marc Kleine-Budde, Pengutronix
 * Copyright (C) 2006 Andrey Volkov, Varma Electronics
 * Copyright (C) 2008-2009 Wolfgang Grandegger <wg@grandegger.com>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

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
