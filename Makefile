#
#  $Id$
#
#  Copyright (c) 2002-2005 Volkswagen Group Electronic Research
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#  1. Redistributions of source code must retain the above copyright
#     notice, this list of conditions, the following disclaimer and
#     the referenced file 'COPYING'.
#  2. Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#  3. Neither the name of Volkswagen nor the names of its contributors
#     may be used to endorse or promote products derived from this software
#     without specific prior written permission.
#
#  Alternatively, provided that this notice is retained in full, this
#  software may be distributed under the terms of the GNU General
#  Public License ("GPL") version 2 as distributed in the 'COPYING'
#  file from the main directory of the linux kernel source.
#
#  The provided data structures and external interfaces from this code
#  are not restricted to be used by modules with a GPL compatible license.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
#  DAMAGE.
#
#  Send feedback to <linux-can@vger.kernel.org>

DESTDIR =
PREFIX = /usr/local

MAKEFLAGS = -k

CFLAGS    = -O2 -Wall -Wno-parentheses -Iinclude \
	    -fno-strict-aliasing \
	    -DSO_RXQ_OVFL=40 \
	    -DPF_CAN=29 \
	    -DAF_CAN=PF_CAN

PROGRAMS_ISOTP = isotpdump isotprecv isotpsend isotpsniffer isotptun isotpserver
PROGRAMS_CANGW = cangw
PROGRAMS_SLCAN = slcan_attach slcand
PROGRAMS = can-calc-bit-timing candump cansniffer cansend canplayer cangen canbusload\
	   log2long log2asc asc2log\
	   canlogserver bcmserver\
	   $(PROGRAMS_ISOTP)\
	   $(PROGRAMS_CANGW)\
	   $(PROGRAMS_SLCAN)\
	   slcanpty canfdtest

all: $(PROGRAMS)

clean:
	rm -f $(PROGRAMS) *.o

install:
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f $(PROGRAMS) $(DESTDIR)$(PREFIX)/bin

distclean:
	rm -f $(PROGRAMS) *.o *~

cansend.o:	lib.h
cangen.o:	lib.h
candump.o:	lib.h
canplayer.o:	lib.h
canlogserver.o:	lib.h
canbusload.o:	lib.h
log2long.o:	lib.h
log2asc.o:	lib.h
asc2log.o:	lib.h

cansend:	cansend.o	lib.o
cangen:		cangen.o	lib.o
candump:	candump.o	lib.o
canplayer:	canplayer.o	lib.o
canlogserver:	canlogserver.o	lib.o
log2long:	log2long.o	lib.o
log2asc:	log2asc.o	lib.o
asc2log:	asc2log.o	lib.o
