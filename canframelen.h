#ifndef CANFRAMELEN_H
#define CANFRAMELEN_H

#include <linux/can.h>

/**
 * Frame length calculation modes.
 *
 * CFL_WORSTCASE corresponds to *worst* case calculation for
 * stuff-bits - see (1)-(3) in [1]. The worst case number of bits on
 * the wire can be calculated as:
 *
 * (34 + 8n - 1)/4 + 34 + 8n + 13 for SFF frames (11 bit CAN-ID) => 55 + 10n
 * (54 + 8n - 1)/4 + 54 + 8n + 13 for EFF frames (29 bit CAN-ID) => 80 + 10n
 *
 * while 'n' is the data length code (number of payload bytes)
 *
 * [1] "Controller Area Network (CAN) schedulability analysis:
 *     Refuted, revisited and revised", Real-Time Syst (2007)
 *     35:239-272.
 *
 */
enum cfl_mode {
	CFL_NO_BITSTUFFING, /* plain bit calculation without bitstuffing */
	CFL_WORSTCASE, /* worst case estimation - see above */
	CFL_EXACT, /* exact calculation of stuffed bits based on frame
		    * content and CRC */
};

/**
 * Calculates the number of bits a frame needs on the wire (including
 * inter frame space).
 *
 * Mode determines how to deal with stuffed bits.
 */
unsigned can_frame_length(struct canfd_frame *frame, enum cfl_mode mode, int mtu);

#endif
