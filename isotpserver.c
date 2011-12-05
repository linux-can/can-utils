/*
 *  $Id$
 */

/*
 * isotpserver.c
 *
 * Implements a socket server which understands ASCII HEX
 * messages for simple TCP/IP <-> ISO 15765-2 bridging.
 *
 * General message format: <[data]+>
 *
 * e.g. for an eight bytes PDU
 *
 * <1122334455667788>
 *
 * Valid ISO 15625-2 PDUs have a length from 1-4095 bytes.
 *
 * Authors:
 * Andre Naujoks (the socket server stuff)
 * Oliver Hartkopp (the rest)
 *
 * Copyright (c) 2002-2010 Volkswagen Group Electronic Research
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
#include <libgen.h>
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
#include <linux/can/isotp.h>

#define NO_CAN_ID 0xFFFFFFFFU

int b64hex(char *asc, unsigned char *bin, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (!sscanf(asc+(i*2), "%2hhx", bin+i))
			return 1;	
	}
	return 0;
}

void childdied(int i)
{
	wait(NULL);
}

void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s -l <port> -s <can_id> -d <can_id> [options] <CAN interface>\n", prg);
	fprintf(stderr, "Options: (* = mandatory)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "ip adressing:\n");
	fprintf(stderr, " *       -l <port>    (local port for the server)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "isotp adressing:\n");
	fprintf(stderr, " *       -s <can_id>  (source can_id. Use 8 digits for extended IDs)\n");
	fprintf(stderr, " *       -d <can_id>  (destination can_id. Use 8 digits for extended IDs)\n");
	fprintf(stderr, "         -x <addr>    (extended addressing mode)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "padding:\n");
	fprintf(stderr, "         -p <byte>    (set and enable tx padding byte)\n");
	fprintf(stderr, "         -r <byte>    (set and enable rx padding byte)\n");
	fprintf(stderr, "         -P <mode>    (check padding in SF/CF. (l)ength (c)ontent (a)ll)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "rx path: (config, which is sent to the sender / data source)\n");
	fprintf(stderr, "         -b <bs>      (blocksize. 0 = off)\n");
	fprintf(stderr, "         -m <val>     (STmin in ms/ns. See spec.)\n");
	fprintf(stderr, "         -w <num>     (max. wait frame transmissions)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "tx path: (config, which changes local tx settings)\n");
	fprintf(stderr, "         -t <time ns> (transmit time in nanosecs)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "All values except for '-l' and '-t' are expected in hexadecimal values.\n");
	fprintf(stderr, "\n");
}

int main(int argc, char **argv)
{
	extern int optind, opterr, optopt;
	int opt;

	int sl, sa, sc; /* (L)isten, (A)ccept, (C)AN sockets */ 
	struct sockaddr_in  saddr, clientaddr;
	struct sockaddr_can caddr;
	static struct can_isotp_options opts;
	static struct can_isotp_fc_options fcopts;
	struct ifreq ifr;
	socklen_t sin_size = sizeof(clientaddr);
	socklen_t caddrlen = sizeof(caddr);

	struct sigaction signalaction;
	sigset_t sigset;

	fd_set readfds;

	int i, ret;
	int nbytes;

	int local_port = 0;
	int verbose = 0;

	int idx = 0; /* index in txmsg[] */

	unsigned char msg[4096];   /* isotp socket message buffer (4095 + test_for_too_long_byte)*/
	char rxmsg[8194]; /* isotp->tcp ASCII message buffer (4095*2 + < > \n null) */
	char txmsg[8193]; /* tcp->isotp ASCII message buffer (4095*2 + < > null) */

	/* mark missing mandatory commandline options as missing */
	caddr.can_addr.tp.tx_id = caddr.can_addr.tp.rx_id = NO_CAN_ID;

	while ((opt = getopt(argc, argv, "l:s:d:x:p:r:P:b:m:w:t:v?")) != -1) {
		switch (opt) {
		case 'l':
			local_port = strtoul(optarg, (char **)NULL, 10);
			break;

		case 's':
			caddr.can_addr.tp.tx_id = strtoul(optarg, (char **)NULL, 16);
			if (strlen(optarg) > 7)
				caddr.can_addr.tp.tx_id |= CAN_EFF_FLAG;
			break;

		case 'd':
			caddr.can_addr.tp.rx_id = strtoul(optarg, (char **)NULL, 16);
			if (strlen(optarg) > 7)
				caddr.can_addr.tp.rx_id |= CAN_EFF_FLAG;
			break;

		case 'x':
			opts.flags |= CAN_ISOTP_EXTEND_ADDR;
			opts.ext_address = strtoul(optarg, (char **)NULL, 16) & 0xFF;
			break;

		case 'p':
			opts.flags |= CAN_ISOTP_TX_PADDING;
			opts.txpad_content = strtoul(optarg, (char **)NULL, 16) & 0xFF;
			break;

		case 'r':
			opts.flags |= CAN_ISOTP_RX_PADDING;
			opts.rxpad_content = strtoul(optarg, (char **)NULL, 16) & 0xFF;
			break;

		case 'P':
			if (optarg[0] == 'l')
				opts.flags |= CAN_ISOTP_CHK_PAD_LEN;
			else if (optarg[0] == 'c')
				opts.flags |= CAN_ISOTP_CHK_PAD_DATA;
			else if (optarg[0] == 'a')
				opts.flags |= (CAN_ISOTP_CHK_PAD_DATA | CAN_ISOTP_CHK_PAD_DATA);
			else {
				printf("unknown padding check option '%c'.\n", optarg[0]);
				print_usage(basename(argv[0]));
				exit(0);
			}
			break;

		case 'b':
			fcopts.bs = strtoul(optarg, (char **)NULL, 16) & 0xFF;
			break;

		case 'm':
			fcopts.stmin = strtoul(optarg, (char **)NULL, 16) & 0xFF;
			break;

		case 'w':
			fcopts.wftmax = strtoul(optarg, (char **)NULL, 16) & 0xFF;
			break;

		case 't':
			opts.frame_txtime = strtoul(optarg, (char **)NULL, 10);
			break;

		case 'v':
			verbose = 1;
			break;

		case '?':
			print_usage(basename(argv[0]));
			exit(0);
			break;

		default:
			fprintf(stderr, "Unknown option %c\n", opt);
			print_usage(basename(argv[0]));
			exit(1);
			break;
		}
	}

	if ((argc - optind != 1) || (local_port == 0) ||
	    (caddr.can_addr.tp.tx_id == NO_CAN_ID) ||
	    (caddr.can_addr.tp.rx_id == NO_CAN_ID)) {
		print_usage(basename(argv[0]));
		exit(1);
	}
  
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
	saddr.sin_port = htons(local_port);

	while(bind(sl,(struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
		printf(".");
		fflush(NULL);
		usleep(100000);
	}

	if (listen(sl, 3) != 0) {
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

	if ((sc = socket(PF_CAN, SOCK_DGRAM, CAN_ISOTP)) < 0) {
		perror("socket");
		exit(1);
	}

	setsockopt(sc, SOL_CAN_ISOTP, CAN_ISOTP_OPTS, &opts, sizeof(opts));
	setsockopt(sc, SOL_CAN_ISOTP, CAN_ISOTP_RECV_FC, &fcopts, sizeof(fcopts));

	caddr.can_family = AF_CAN;
	strcpy(ifr.ifr_name, argv[optind]);
	if (ioctl(sc, SIOCGIFINDEX, &ifr) < 0) {
		perror("SIOCGIFINDEX");
		exit(1);
	}
	caddr.can_ifindex = ifr.ifr_ifindex;

	if (bind(sc, (struct sockaddr *)&caddr, caddrlen) < 0) {
		perror("bind");
		exit(1);
	}

	while (1) {

		FD_ZERO(&readfds);
		FD_SET(sc, &readfds);
		FD_SET(sa, &readfds);

		ret = select((sc > sa)?sc+1:sa+1, &readfds, NULL, NULL, NULL);

		if (FD_ISSET(sc, &readfds)) {


			nbytes = read(sc, &msg, 4096);

			if (nbytes < 1 || nbytes > 4095) {
				perror("read from isotp socket");
				exit(1);
			}

			rxmsg[0] = '<';

			for ( i = 0; i < nbytes; i++)
				sprintf(rxmsg + 1 + 2*i, "%02X", msg[i]);

			/* finalize string for sending */
			strcat(rxmsg, ">\n");

			if (verbose)
				printf("CAN>TCP %s", rxmsg);

			send(sa, rxmsg, strlen(rxmsg), 0);
		}


		if (FD_ISSET(sa, &readfds)) {

			if (read(sa, txmsg+idx, 1) < 1) {
				perror("read from tcp/ip socket");
				exit(1);
			}

			if (!idx) {
				if (txmsg[0] == '<')
					idx = 1;

				continue;
			}

			/* max len is 4095*2 + '<' + '>' = 8192. The buffer index starts with 0 */
			if (idx > 8191) {
				idx = 0;
				continue;
			}

			if (txmsg[idx] != '>') {
				idx++;
				continue;
			}

			txmsg[idx+1] = 0;
			idx = 0;

			/* must be an even number of bytes and at least one data byte <XX> */
			if (strlen(txmsg) < 4 || strlen(txmsg) % 2)
				continue;

			if (verbose)
				printf("TCP>CAN %s\n", txmsg);

			nbytes = (strlen(txmsg)-2)/2;
			if (b64hex(txmsg+1, msg, nbytes) == 0)
				send(sc, msg, nbytes, 0);
		}
	}

	close(sc);
	close(sa);

	return 0;
}
