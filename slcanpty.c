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
 * Send feedback to <linux-can@vger.kernel.org>
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

/* read data from pty, send CAN frames to CAN socket and answer commands */
int pty2can(int pty, int socket, struct can_filter *fi,
	       int *is_open, int *tstamp)
{
	int nbytes;
	char cmd;
	char buf[200];
	char replybuf[10]; /* for answers to received commands */
	int ptr;
	struct can_frame frame;
	int tmp, i;

	nbytes = read(pty, &buf, sizeof(buf)-1);
	if (nbytes < 0) {
		perror("read pty");
		return 1;
	}

rx_restart:
	/* remove trailing '\r' characters to be robust against some apps */
	while (buf[0] == '\r' && nbytes > 0) {
		for (tmp = 0; tmp < nbytes; tmp++)
			buf[tmp] = buf[tmp+1];
		nbytes--;
	}

	if (!nbytes)
		return 0;

	cmd = buf[0];
	buf[nbytes] = 0;

#ifdef DEBUG
	for (tmp = 0; tmp < nbytes; tmp++)
		if (buf[tmp] == '\r')
			putchar('@');
		else
			putchar(buf[tmp]);
	printf("\n");
#endif

	/* check for filter configuration commands */
	if (cmd == 'm' || cmd == 'M') {
		buf[9] = 0; /* terminate filter string */
		ptr = 9;
#if 0
		/* the filter is no SocketCAN filter :-( */

		/* TODO: behave like a SJA1000 controller specific filter */

		if (cmd == 'm') {
			fi->can_id = strtoul(buf+1,NULL,16);
			fi->can_id &= CAN_EFF_MASK;
		} else {
			fi->can_mask = strtoul(buf+1,NULL,16);
			fi->can_mask &= CAN_EFF_MASK;
		}

		if (*is_open)
			setsockopt(socket, SOL_CAN_RAW,
				   CAN_RAW_FILTER, fi,
				   sizeof(struct can_filter));
#endif
		goto rx_out_ack;
	}


	/* check for timestamp on/off command */
	if (cmd == 'Z') {
		*tstamp = buf[1] & 0x01;
		ptr = 2;
		goto rx_out_ack;
	}

	/* check for 'O'pen command */
	if (cmd == 'O') {
		setsockopt(socket, SOL_CAN_RAW,
			   CAN_RAW_FILTER, fi,
			   sizeof(struct can_filter));
		ptr = 1;
		*is_open = 1;
		goto rx_out_ack;
	}

	/* check for 'C'lose command */
	if (cmd == 'C') {
		setsockopt(socket, SOL_CAN_RAW, CAN_RAW_FILTER,
			   NULL, 0);
		ptr = 1;
		*is_open = 0;
		goto rx_out_ack;
	}

	/* check for 'V'ersion command */
	if (cmd == 'V') {
		sprintf(replybuf, "V1013\r");
		tmp = strlen(replybuf);
		ptr = 1;
		goto rx_out;
	}

	/* check for serial 'N'umber command */
	if (cmd == 'N') {
		sprintf(replybuf, "N4242\r");
		tmp = strlen(replybuf);
		ptr = 1;
		goto rx_out;
	}

	/* check for read status 'F'lags */
	if (cmd == 'F') {
		sprintf(replybuf, "F00\r");
		tmp = strlen(replybuf);
		ptr = 1;
		goto rx_out;
	}

	/* correctly answer unsupported commands */
	if (cmd == 'U') {
		ptr = 2;
		goto rx_out_ack;
	}
	if (cmd == 'S') {
		ptr = 2;
		goto rx_out_ack;
	}
	if (cmd == 's') {
		ptr = 5;
		goto rx_out_ack;
	}
	if (cmd == 'P' || cmd == 'A') {
		ptr = 1;
		goto rx_out_nack;
	}
	if (cmd == 'X') {
		ptr = 2;
		if (buf[1] & 0x01)
			goto rx_out_ack;
		else
			goto rx_out_nack;
	}

	/* catch unknown commands */
	if ((cmd != 't') && (cmd != 'T') &&
	    (cmd != 'r') && (cmd != 'R')) {
		ptr = nbytes-1;
		goto rx_out_nack;
	}

	if (cmd & 0x20) /* tiny chars 'r' 't' => SFF */
		ptr = 4; /* dlc position tiiid */
	else
		ptr = 9; /* dlc position Tiiiiiiiid */

	*(unsigned long long *) (&frame.data) = 0ULL; /* clear data[] */

	if ((cmd | 0x20) == 'r' && buf[ptr] != '0') {

		/* 
		 * RTR frame without dlc information!
		 * This is against the SLCAN spec but sent
		 * by a commercial CAN tool ... so we are
		 * robust against this protocol violation.
		 */

		frame.can_dlc = buf[ptr]; /* save following byte */

		buf[ptr] = 0; /* terminate can_id string */

		frame.can_id = strtoul(buf+1, NULL, 16);
		frame.can_id |= CAN_RTR_FLAG;

		if (!(cmd & 0x20)) /* NO tiny chars => EFF */
			frame.can_id |= CAN_EFF_FLAG;

		buf[ptr]  = frame.can_dlc; /* restore following byte */
		frame.can_dlc = 0;
		ptr--; /* we have no dlc component in the violation case */

	} else {

		if (!(buf[ptr] >= '0' && buf[ptr] < '9'))
			goto rx_out_nack;

		frame.can_dlc = buf[ptr] - '0'; /* get dlc from ASCII val */

		buf[ptr] = 0; /* terminate can_id string */

		frame.can_id = strtoul(buf+1, NULL, 16);

		if (!(cmd & 0x20)) /* NO tiny chars => EFF */
			frame.can_id |= CAN_EFF_FLAG;

		if ((cmd | 0x20) == 'r') /* RTR frame */
			frame.can_id |= CAN_RTR_FLAG;

		for (i = 0, ptr++; i < frame.can_dlc; i++) {

			tmp = asc2nibble(buf[ptr++]);
			if (tmp > 0x0F)
				goto rx_out_nack;
			frame.data[i] = (tmp << 4);
			tmp = asc2nibble(buf[ptr++]);
			if (tmp > 0x0F)
				goto rx_out_nack;
			frame.data[i] |= tmp;
		}
		/* point to last real data */
		if (frame.can_dlc)
			ptr--;
	}

	nbytes = write(socket, &frame, sizeof(frame));
	if (nbytes != sizeof(frame)) {
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
	tmp = write(pty, replybuf, tmp);
	if (tmp < 0) {
		perror("write pty replybuf");
		return 1;
	}

	/* check if there is another command in this buffer */
	if (nbytes > ptr+1) {
		for (tmp = 0, ptr++; ptr+tmp < nbytes; tmp++)
			buf[tmp] = buf[ptr+tmp];
		nbytes = tmp;
		goto rx_restart;
	}

	return 0;
}

/* read CAN frames from CAN interface and write it to the pty */
int can2pty(int pty, int socket, int *tstamp)
{
	int nbytes;
	char cmd;
	char buf[SLC_MTU];
	int ptr;
	struct can_frame frame;
	int i;

	nbytes = read(socket, &frame, sizeof(frame));
	if (nbytes != sizeof(frame)) {
		perror("read socket");
		return 1;
	}

	/* convert to slcan ASCII frame */
	if (frame.can_id & CAN_RTR_FLAG)
		cmd = 'R'; /* becomes 'r' in SFF format */
	else
		cmd = 'T'; /* becomes 't' in SFF format */

	if (frame.can_id & CAN_EFF_FLAG)
		sprintf(buf, "%c%08X%d", cmd,
			frame.can_id & CAN_EFF_MASK,
			frame.can_dlc);
	else
		sprintf(buf, "%c%03X%d", cmd | 0x20,
			frame.can_id & CAN_SFF_MASK,
			frame.can_dlc);

	ptr = strlen(buf);

	for (i = 0; i < frame.can_dlc; i++)
		sprintf(&buf[ptr + 2*i], "%02X",
			frame.data[i]);

	if (*tstamp) {
		struct timeval tv;

		if (ioctl(socket, SIOCGSTAMP, &tv) < 0)
			perror("SIOCGSTAMP");

		sprintf(&buf[ptr + 2*frame.can_dlc], "%04lX",
			(tv.tv_sec%60)*1000 + tv.tv_usec/1000);
	}

	strcat(buf, "\r"); /* add terminating character */
	nbytes = write(pty, buf, strlen(buf));
	if (nbytes < 0) {
		perror("write pty");
		return 1;
	}
	fflush(NULL);

	return 0;
}


int main(int argc, char **argv)
{
	fd_set rdfs;
	int p; /* pty master file */ 
	int s; /* can raw socket */ 
	struct sockaddr_can addr;
	struct termios topts;
	struct ifreq ifr;
	int running = 1;
	int tstamp = 0;
	int is_open = 0;
	struct can_filter fi;

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

		if (FD_ISSET(p, &rdfs))
			if (pty2can(p, s, &fi, &is_open, &tstamp)) {
			running = 0;
			continue;
		}

		if (FD_ISSET(s, &rdfs))
			if (can2pty(p, s, &tstamp)) {
			running = 0;
			continue;
		}
	}

	close(p);
	close(s);

	return 0;
}
