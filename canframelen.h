#ifndef CANFRAMELEN_H
#define CANFRAMELEN_H

#include <linux/can.h>

/**
 * Frame length calculation modes.
 *
 * CFL_WORSTCASE corresponds to Ken Tindells *worst* case calculation
 * for stuff-bits (see "Guaranteeing Message Latencies on Controller
 * Area Network" 1st ICC'94) the needed bits on the wire can be
 * calculated as:
 *
 * (34 + 8n)/5 + 47 + 8n for SFF frames (11 bit CAN-ID) => (269 + 48n)/5
 * (54 + 8n)/5 + 67 + 8n for EFF frames (29 bit CAN-ID) => (389 + 48n)/5
 *
 * while 'n' is the data length code (number of payload bytes)
 *
 */
enum cfl_mode {
	CFL_NO_BITSTUFFING, /* plain bit calculation without bitstuffing */
	CFL_WORSTCASE, /* with bitstuffing following Tindells estimation */
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
