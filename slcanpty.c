/*
 *  $Id$
 */

/*
 * slcanpty.c -  creates a pty for applications using the slcan ASCII protocol
 * and converts the ASCII data to a CAN network interface (and vice versa)
 *
 * Copyright (c)2009 Oliver Hartkopp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Send feedback to <socketcan-users@lists.berlios.de>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/can.h>
#include <linux/can/raw.h>

/* maximum rx buffer len: extended CAN frame with timestamp */
#define SLC_MTU (sizeof("T1111222281122334455667788EA5F\r")+1)

#define DEBUG

static int asc2nibble(char c)
{

	if ((c >= '0') && (c <= '9'))
		return c - '0';

	if ((c >= 'A') && (c <= 'F'))
		return c - 'A' + 10;

	if ((c >= 'a') && (c <= 'f'))
		return c - 'a' + 10;

	return 16; /* error */
}

int main(int argc, char **argv)
{
	fd_set rdfs;
	int p; /* pty master file */ 
	int s; /* can raw socket */ 
	int nbytes;
	struct sockaddr_can addr;
	struct termios topts;
	struct ifreq ifr;
	int running = 1;
	int tstamp = 0;
	int is_open = 0;
	char txcmd, rxcmd;
	char txbuf[SLC_MTU];
	char rxbuf[200];
	char replybuf[SLC_MTU];
	int txp, rxp;
	struct can_frame txf, rxf;
	struct can_filter fi;
	int tmp, i;

	/* check command line options */
	if (argc != 3) {
		fprintf(stderr, "\n");
		fprintf(stderr, "%s creates a pty for applications using"
			" the slcan ASCII protocol and\n", argv[0]);
		fprintf(stderr, "converts the ASCII data to a CAN network"
			" interface (and vice versa)\n\n");
		fprintf(stderr, "Usage: %s <pty> <can interface>\n", argv[0]);
		fprintf(stderr, "e.g. '%s /dev/ptyc0 can0' creates"
			" /dev/ttyc0 for the slcan application\n", argv[0]);
		fprintf(stderr, "\n");
		return 1;
	}

	/* open pty */
	p = open(argv[1], O_RDWR);
	if (p < 0) {
		perror("open pty");
		return 1;
	}

	if (tcgetattr(p, &topts)) {
		perror("tcgetattr");
		return 1;
	}

	/* disable local echo which would cause double frames */
	topts.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK |
			   ECHONL | ECHOPRT | ECHOKE | ICRNL);
	tcsetattr(p, TCSANOW, &topts);

	/* open socket */
	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) {
		perror("socket");
		return 1;
	}

	addr.can_family = AF_CAN;

	strcpy(ifr.ifr_name, argv[2]);
	if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
		perror("SIOCGIFINDEX");
		return 1;
	}
	addr.can_ifindex = ifr.ifr_ifindex;

	/* disable reception of CAN frames until we are opened by 'O' */
	setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0);

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return 1;
	}

	/* open filter by default */
	fi.can_id   = 0;
	fi.can_mask = 0;

	while (running) {

		FD_ZERO(&rdfs);
		FD_SET(0, &rdfs);
		FD_SET(p, &rdfs);
		FD_SET(s, &rdfs);

		if (select(s+1, &rdfs, NULL, NULL, NULL) < 0) {
			perror("select");
			return 1;
		}

		if (FD_ISSET(0, &rdfs)) {
			running = 0;
			continue;
		}

		if (FD_ISSET(p, &rdfs)) {
			/* read rxdata from pty */
			nbytes = read(p, &rxbuf, sizeof(rxbuf)-1);
			if (nbytes < 0) {
				perror("read pty");
				return 1;
			}

rx_restart:
			/* remove trailing '\r' characters */
			while (rxbuf[0] == '\r' && nbytes > 0) {
				for (tmp = 0; tmp < nbytes; tmp++)
					rxbuf[tmp] = rxbuf[tmp+1];
				nbytes--;
			}

			if (!nbytes)
				continue;

			rxcmd = rxbuf[0];
			rxbuf[nbytes] = 0;

#ifdef DEBUG
			for (tmp = 0; tmp < nbytes; tmp++)
				if (rxbuf[tmp] == '\r')
					putchar('@');
				else
					putchar(rxbuf[tmp]);
			printf("\n");
#endif

			/* check for filter configuration commands */
			if (rxcmd == 'm' || rxcmd == 'M') {
				rxbuf[9] = 0; /* terminate filter string */
				rxp = 9;
#if 0
				/* the filter is no SocketCAN filter :-( */

				/* TODO: behave like a SJA1000 filter */

				if (rxcmd == 'm') {
					fi.can_id = strtoul(rxbuf+1,NULL,16);
					fi.can_id &= CAN_EFF_MASK;
				} else {
					fi.can_mask = strtoul(rxbuf+1,NULL,16);
					fi.can_mask &= CAN_EFF_MASK;
				}

				/* set only when both values are defined */
				if (is_open)
					setsockopt(s, SOL_CAN_RAW,
						   CAN_RAW_FILTER, &fi,
						   sizeof(struct can_filter));
#endif
				goto rx_out_ack;
			}


			/* check for timestamp on/off command */
			if (rxcmd == 'Z') {
				tstamp = rxbuf[1] & 0x01;
				rxp = 2;
				goto rx_out_ack;
			}

			/* check for 'O'pen command */
			if (rxcmd == 'O') {
				setsockopt(s, SOL_CAN_RAW,
					   CAN_RAW_FILTER, &fi,
					   sizeof(struct can_filter));
				rxp = 1;
				is_open = 1;
				goto rx_out_ack;
			}

			/* check for 'C'lose command */
			if (rxcmd == 'C') {
				setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER,
					   NULL, 0);
				rxp = 1;
				is_open = 0;
				goto rx_out_ack;
			}

			/* check for 'V'ersion command */
			if (rxcmd == 'V') {
				sprintf(replybuf, "V1013\r");
				tmp = strlen(replybuf);
				rxp = 1;
				goto rx_out;
			}

			/* check for serial 'N'umber command */
			if (rxcmd == 'N') {
				sprintf(replybuf, "N4242\r");
				tmp = strlen(replybuf);
				rxp = 1;
				goto rx_out;
			}

			/* check for read status 'F'lags */
			if (rxcmd == 'F') {
				sprintf(replybuf, "F00\r");
				tmp = strlen(replybuf);
				rxp = 1;
				goto rx_out;
			}

			/* correctly answer unsupported commands */
			if (rxcmd == 'U') {
				rxp = 2;
				goto rx_out_ack;
			}
			if (rxcmd == 'S') {
				rxp = 2;
				goto rx_out_ack;
			}
			if (rxcmd == 's') {
				rxp = 5;
				goto rx_out_ack;
			}
			if (rxcmd == 'P' || rxcmd == 'A') {
				rxp = 1;
				goto rx_out_nack;
			}
			if (rxcmd == 'X') {
				rxp = 2;
				if (rxbuf[1] & 0x01)
					goto rx_out_ack;
				else
					goto rx_out_nack;
			}

			/* catch unknown commands */
			if ((rxcmd != 't') && (rxcmd != 'T') &&
			    (rxcmd != 'r') && (rxcmd != 'R')) {
				rxp = nbytes-1;
				goto rx_out_nack;
			}

			if (rxcmd & 0x20) /* tiny chars 'r' 't' => SFF */
				rxp = 4; /* dlc position tiiid */
			else
				rxp = 9; /* dlc position Tiiiiiiiid */

			*(unsigned long long *) (&rxf.data) = 0ULL; /* clear */

			if ((rxcmd | 0x20) == 'r' && rxbuf[rxp] != '0') {
				/* RTR frame without dlc information */

				rxf.can_dlc = rxbuf[rxp]; /* save */

				rxbuf[rxp] = 0; /* terminate can_id string */

				rxf.can_id = strtoul(rxbuf+1, NULL, 16);
				rxf.can_id |= CAN_RTR_FLAG;

				if (!(rxcmd & 0x20)) /* NO tiny chars => EFF */
					rxf.can_id |= CAN_EFF_FLAG;

				rxbuf[rxp]  = rxf.can_dlc; /* restore */
				rxf.can_dlc = 0;
				rxp--; /* we have no dlc component here */

			} else {

				if (!(rxbuf[rxp] >= '0' && rxbuf[rxp] < '9'))
					goto rx_out_nack;

				rxf.can_dlc = rxbuf[rxp] & 0x0F; /* get dlc */

				rxbuf[rxp] = 0; /* terminate can_id string */

				rxf.can_id = strtoul(rxbuf+1, NULL, 16);

				if (!(rxcmd & 0x20)) /* NO tiny chars => EFF */
					rxf.can_id |= CAN_EFF_FLAG;

				if ((rxcmd | 0x20) == 'r') /* RTR frame */
					rxf.can_id |= CAN_RTR_FLAG;

				for (i = 0, rxp++; i < rxf.can_dlc; i++) {

					tmp = asc2nibble(rxbuf[rxp++]);
					if (tmp > 0x0F)
						goto rx_out_nack;
					rxf.data[i] = (tmp << 4);
					tmp = asc2nibble(rxbuf[rxp++]);
					if (tmp > 0x0F)
						goto rx_out_nack;
					rxf.data[i] |= tmp;
				}
				/* point to last real data */
				if (rxf.can_dlc)
					rxp--;
			}

			nbytes = write(s, &rxf, sizeof(rxf));
			if (nbytes != sizeof(rxf)) {
				perror("write socket");
				return 1;
			}

rx_out_ack:
			replybuf[0] = '\r';
			tmp = 1;
			goto rx_out;
rx_out_nack:
			replybuf[0] = '\a';
			tmp = 1;
rx_out:
			tmp = write(p, replybuf, tmp);
			if (tmp < 0) {
				perror("write pty replybuf");
				return 1;
			}

			/* check if there is another command in this buffer */
			if (nbytes > rxp+1) {
				for (tmp = 0, rxp++; rxp+tmp < nbytes; tmp++)
					rxbuf[tmp] = rxbuf[rxp+tmp];
				nbytes = tmp;
				goto rx_restart;
			}
		}

		if (FD_ISSET(s, &rdfs)) {
			/* read txframe from CAN interface */
			nbytes = read(s, &txf, sizeof(txf));
			if (nbytes != sizeof(txf)) {
				perror("read socket");
				return 1;
			}

			/* convert to slcan ASCII txf */
			if (txf.can_id & CAN_RTR_FLAG)
				txcmd = 'R'; /* becomes 'r' in SFF format */
			else
				txcmd = 'T'; /* becomes 't' in SFF format */

			if (txf.can_id & CAN_EFF_FLAG)
				sprintf(txbuf, "%c%08X%d", txcmd,
					txf.can_id & CAN_EFF_MASK,
					txf.can_dlc);
			else
				sprintf(txbuf, "%c%03X%d", txcmd | 0x20,
					txf.can_id & CAN_SFF_MASK,
					txf.can_dlc);

			txp = strlen(txbuf);

			for (i = 0; i < txf.can_dlc; i++)
				sprintf(&txbuf[txp + 2*i], "%02X",
					txf.data[i]);

			if (tstamp) {
				struct timeval tv;

				if (ioctl(s, SIOCGSTAMP, &tv) < 0)
					perror("SIOCGSTAMP");

				sprintf(&txbuf[txp + 2*txf.can_dlc], "%04lX",
					(tv.tv_sec%60)*1000 + tv.tv_usec/1000);
			}

			strcat(txbuf, "\r"); /* add terminating character */
			nbytes = write(p, txbuf, strlen(txbuf));
			if (nbytes < 0) {
				perror("write pty");
				return 1;
			}
			fflush(NULL);
		}
	}

	close(p);
	close(s);

	return 0;
}
