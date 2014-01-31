/*
 * canframelen.h
 *
 * Copyright (c) 2013, 2014 Czech Technical University in Prague
 *
 * Author: Michal Sojka <sojkam1@fel.cvut.cz>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Czech Technical University in Prague nor the
 *    names of its contributors may be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * The provided data structures and external interfaces from this code
 * are not restricted to be used by modules with a GPL compatible license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * Send feedback to <linux-can@vger.kernel.org>
 *
 */

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
