/*
 *  $Id$
 */

/*
 * tst-bcm-server.c
 *
 * Test programm that implements a socket server which understands ASCII
 * messages for simple broadcast manager frame send commands.
 *
 * < interface command ival_s ival_us can_id can_dlc [data]* >
 *
 * Only the items 'can_id' and 'data' are given in (ASCII) hexadecimal values.
 *
 * ## TX path:
 *
 * The commands are 'A'dd, 'U'pdate, 'D'elete and 'S'end.
 * e.g.
 *
 * Send the CAN frame 123#1122334455667788 every second on vcan1
 * < vcan1 A 1 0 123 8 11 22 33 44 55 66 77 88 >
 *
 * Send the CAN frame 123#1122334455667788 every 10 usecs on vcan1
 * < vcan1 A 0 10 123 8 11 22 33 44 55 66 77 88 >
 *
 * Send the CAN frame 123#42424242 every 20 msecs on vcan1
 * < vcan1 A 0 20000 123 4 42 42 42 42 >
 *
 * Update the CAN frame 123#42424242 with 123#112233 - no change of timers
 * < vcan1 U 0 0 123 3 11 22 33 >
 *
 * Delete the cyclic send job from above
 * < vcan1 D 0 0 123 0 >
 *
 * Send a single CAN frame without cyclic transmission
 * < can0 S 0 0 123 0 >
 *
 * When the socket is closed the cyclic transmissions are terminated.
 *
 * ## RX path:
 *
 * The commands are 'R'eceive setup, 'F'ilter ID Setup and 'X' for delete.
 * e.g.
 *
 * Receive CAN ID 0x123 from vcan1 and check for changes in the first byte
 * < vcan1 R 0 0 123 1 FF >
 *
 * Receive CAN ID 0x123 from vcan1 and check for changes in given mask
 * < vcan1 R 0 0 123 8 FF 00 F8 00 00 00 00 00 >
 *
 * As above but throttle receive update rate down to 1.5 seconds
 * < vcan1 R 1 500000 123 8 FF 00 F8 00 00 00 00 00 >
 *
 * Filter for CAN ID 0x123 from vcan1 without content filtering
 * < vcan1 F 0 0 123 0 >
 *
 * Delete receive filter ('R' or 'F') for CAN ID 0x123
 * < vcan1 X 0 0 123 0 >
 *
 * CAN messages received by the given filters are send in the format:
 * < interface can_id can_dlc [data]* >
 *
 * e.g. when receiving a CAN message from vcan1 with
 * can_id 0x123 , data length 4 and data 0x11, 0x22, 0x33 and 0x44
 *
 * < vcan1 123 4 11 22 33 44 >
 *
 * ##
 *
 * Authors:
 * Andre Naujoks (the socket server stuff)
 * Oliver Hartkopp (the rest)
 *
 * Copyright (c) 2002-2009 Volkswagen Group Electronic Research
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <net/if.h>
#include <netinet/in.h>

#include <linux/can.h>
#include <linux/can/bcm.h>

#define MAXLEN 100
#define PORT 28600

void childdied(int i)
{
	wait(NULL);
}

int main(int argc, char **argv)
{

	int sl, sa, sc;
	int i, ret;
	int idx = 0;
	struct sockaddr_in  saddr, clientaddr;
	struct sockaddr_can caddr;
	socklen_t caddrlen = sizeof(caddr);
	struct ifreq ifr;
	fd_set readfds;
	socklen_t sin_size = sizeof(clientaddr);
	struct sigaction signalaction;
	sigset_t sigset;

	char buf[MAXLEN];
	char rxmsg[50];

	struct {
		struct bcm_msg_head msg_head;
		struct can_frame frame;
	} msg;

	sigemptyset(&sigset);
	signalaction.sa_handler = &childdied;
	signalaction.sa_mask = sigset;
	signalaction.sa_flags = 0;
	sigaction(SIGCHLD, &signalaction, NULL);  /* signal for dying child */

	if((sl = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("inetsocket");
		exit(1);
	}

	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	saddr.sin_port = htons(PORT);

	while(bind(sl,(struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
		printf(".");fflush(NULL);
		usleep(100000);
	}

	if (listen(sl,3) != 0) {
		perror("listen");
		exit(1);
	}

	while (1) { 
		sa = accept(sl,(struct sockaddr *)&clientaddr, &sin_size);
		if (sa > 0 ){

			if (fork())
				close(sa);
			else
				break;
		}
		else {
			if (errno != EINTR) {
				/*
				 * If the cause for the error was NOT the
				 * signal from a dying child => give an error
				 */
				perror("accept");
				exit(1);
			}
		}
	}

	/* open BCM socket */

	if ((sc = socket(PF_CAN, SOCK_DGRAM, CAN_BCM)) < 0) {
		perror("bcmsocket");
		return 1;
	}

	memset(&caddr, 0, sizeof(caddr));
	caddr.can_family = PF_CAN;
	/* can_ifindex is set to 0 (any device) => need for sendto() */

	if (connect(sc, (struct sockaddr *)&caddr, sizeof(caddr)) < 0) {
		perror("connect");
		return 1;
	}

	while (1) {

		FD_ZERO(&readfds);
		FD_SET(sc, &readfds);
		FD_SET(sa, &readfds);

		ret = select((sc > sa)?sc+1:sa+1, &readfds, NULL, NULL, NULL);

		if (FD_ISSET(sc, &readfds)) {

			ret = recvfrom(sc, &msg, sizeof(msg), 0,
				       (struct sockaddr*)&caddr, &caddrlen);

			ifr.ifr_ifindex = caddr.can_ifindex;
			ioctl(sc, SIOCGIFNAME, &ifr);

			sprintf(rxmsg, "< %s %03X %d ", ifr.ifr_name,
				msg.msg_head.can_id, msg.frame.can_dlc);

			for ( i = 0; i < msg.frame.can_dlc; i++)
				sprintf(rxmsg + strlen(rxmsg), "%02X ",
					msg.frame.data[i]);

			/* delimiter '\0' for Adobe(TM) Flash(TM) XML sockets */
			strcat(rxmsg, ">\0");

			send(sa, rxmsg, strlen(rxmsg) + 1, 0);
		}


		if (FD_ISSET(sa, &readfds)) {

			char cmd;
			int items;

			if (read(sa, buf+idx, 1) < 1)
				exit(1);

			if (!idx) {
				if (buf[0] == '<')
					idx = 1;

				continue;
			}

			if (idx > MAXLEN-2) {
				idx = 0;
				continue;
			}

			if (buf[idx] != '>') {
				idx++;
				continue;
			}

			buf[idx+1] = 0;
			idx = 0;

			//printf("read '%s'\n", buf);

			/* prepare bcm message settings */
			memset(&msg, 0, sizeof(msg));
			msg.msg_head.nframes = 1;

			items = sscanf(buf, "< %6s %c %lu %lu %x %hhu "
				       "%hhx %hhx %hhx %hhx %hhx %hhx "
				       "%hhx %hhx >",
				       ifr.ifr_name,
				       &cmd, 
				       &msg.msg_head.ival2.tv_sec,
				       &msg.msg_head.ival2.tv_usec,
				       &msg.msg_head.can_id,
				       &msg.frame.can_dlc,
				       &msg.frame.data[0],
				       &msg.frame.data[1],
				       &msg.frame.data[2],
				       &msg.frame.data[3],
				       &msg.frame.data[4],
				       &msg.frame.data[5],
				       &msg.frame.data[6],
				       &msg.frame.data[7]);

			if (items < 6)
				break;
			if (msg.frame.can_dlc > 8)
				break;
			if (items != 6 + msg.frame.can_dlc)
				break;

			msg.frame.can_id = msg.msg_head.can_id;

			switch (cmd) {
			case 'S':
				msg.msg_head.opcode = TX_SEND;
				break;
			case 'A':
				msg.msg_head.opcode = TX_SETUP;
				msg.msg_head.flags |= SETTIMER | STARTTIMER;
				break;
			case 'U':
				msg.msg_head.opcode = TX_SETUP;
				msg.msg_head.flags  = 0;
				break;
			case 'D':
				msg.msg_head.opcode = TX_DELETE;
				break;

			case 'R':
				msg.msg_head.opcode = RX_SETUP;
				msg.msg_head.flags  = SETTIMER;
				break;
			case 'F':
				msg.msg_head.opcode = RX_SETUP;
				msg.msg_head.flags  = RX_FILTER_ID | SETTIMER;
				break;
			case 'X':
				msg.msg_head.opcode = RX_DELETE;
				break;
			default:
				printf("unknown command '%c'.\n", cmd);
				exit(1);
			}

			if (!ioctl(sc, SIOCGIFINDEX, &ifr)) {
				caddr.can_ifindex = ifr.ifr_ifindex;
				sendto(sc, &msg, sizeof(msg), 0,
				       (struct sockaddr*)&caddr, sizeof(caddr));
			}
		}
	}

	close(sc);
	close(sa);

	return 0;
}
