/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * slcanpty: adapter for applications using the slcan ASCII protocol
 *
 * slcanpty.c - creates a pty for applications using the slcan ASCII protocol
 * and converts the ASCII data to a CAN network interface (and vice versa)
 *
 * Copyright (c)2009 Oliver Hartkopp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
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

#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/sockios.h>

/* maximum rx buffer len: extended CAN frame with timestamp */
#define SLC_MTU (sizeof("T1111222281122334455667788EA5F\r")+1)
#define DEVICE_NAME_PTMX "/dev/ptmx"

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
	static char buf[200];
	char replybuf[10]; /* for answers to received commands */
	int ptr;
	struct can_frame frame;
	int tmp, i;
	static int rxoffset = 0; /* points to the end of an received incomplete SLCAN message */

	nbytes = read(pty, &buf[rxoffset], sizeof(buf)-rxoffset-1);
	if (nbytes <= 0) {
		/* nbytes == 0 : no error but pty descriptor has been closed */
		if (nbytes < 0)
			perror("read pty");

		return 1;
	}

	/* reset incomplete message offset */
	nbytes += rxoffset;
	rxoffset = 0;

rx_restart:
	/* remove trailing '\r' characters to be robust against some apps */
	while (buf[0] == '\r' && nbytes > 0) {
		for (tmp = 0; tmp < nbytes; tmp++)
			buf[tmp] = buf[tmp+1];
		nbytes--;
	}

	if (!nbytes)
		return 0;

	/* check if we can detect a complete SLCAN message including '\r' */
	for (tmp = 0; tmp < nbytes; tmp++) {
		if (buf[tmp] == '\r')
			break;
	}

	/* no '\r' found in the message buffer? */
	if (tmp == nbytes) {
		/* save incomplete message */
		rxoffset = nbytes;

		/* leave here and read from pty again */
		return 0;
	}

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
	/* check for 'v'ersion command */
	if (cmd == 'v') {
		sprintf(replybuf, "v1014\r");
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

	memset(&frame.data, 0, 8); /* clear data[] */

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

	tmp = write(socket, &frame, sizeof(frame));
	if (tmp != sizeof(frame)) {
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

int check_select_stdin(void)
{
	fd_set rdfs;
	struct timeval timeout;
	int ret;

	FD_ZERO(&rdfs);
	FD_SET(0, &rdfs);
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	ret = select(1, &rdfs, NULL, NULL, &timeout);

	if (ret < 0)
		return 0; /* not selectable */

	if (ret > 0 && getchar() == EOF)
		return 0; /* EOF, eg. /dev/null */

	return 1;
}

int main(int argc, char **argv)
{
	fd_set rdfs;
	int p; /* pty master file */ 
	int s; /* can raw socket */ 
	struct sockaddr_can addr;
	struct termios topts;
	int select_stdin = 0;
	int running = 1;
	int tstamp = 0;
	int is_open = 0;
	struct can_filter fi;

	/* check command line options */
	if (argc != 3) {
		fprintf(stderr, "%s: adapter for applications using"
			" the slcan ASCII protocol.\n", basename(argv[0]));
		fprintf(stderr, "\n%s creates a pty for applications using"
			" the slcan ASCII protocol and\n", basename(argv[0]));
		fprintf(stderr, "converts the ASCII data to a CAN network"
			" interface (and vice versa)\n\n");
		fprintf(stderr, "Usage: %s <pty> <can interface>\n", basename(argv[0]));
		fprintf(stderr, "\nExamples:\n");
		fprintf(stderr, "%s /dev/ptyc0 can0  - creates /dev/ttyc0 for the slcan application\n\n",
			basename(argv[0]));
		fprintf(stderr, "e.g. for pseudo-terminal '%s %s can0' creates"
			" /dev/pts/N\n", basename(argv[0]), DEVICE_NAME_PTMX);
		fprintf(stderr, "\n");
		return 1;
	}

	select_stdin = check_select_stdin();

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
			   ECHONL | ECHOPRT | ECHOKE);
	topts.c_iflag &= ~(ICRNL);
	topts.c_iflag |= INLCR;
	tcsetattr(p, TCSANOW, &topts);

	/* Support for the Unix 98 pseudo-terminal interface /dev/ptmx /dev/pts/N */
	if  (strcmp(argv[1], DEVICE_NAME_PTMX) == 0) {

		char *name_pts = NULL;	/* slave pseudo-terminal device name */

		if (grantpt(p) < 0) {
			perror("grantpt");
			return 1;
		}

		if (unlockpt(p) < 0) {
			perror("unlockpt");
			return 1;
		}

		name_pts = ptsname(p);
		if (name_pts == NULL) {
			perror("ptsname");
			return 1;
		}
		printf("open: %s: slave pseudo-terminal is %s\n", argv[1], name_pts);
	}

	/* open socket */
	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) {
		perror("socket");
		return 1;
	}

	addr.can_family = AF_CAN;
	addr.can_ifindex = if_nametoindex(argv[2]);
	if (!addr.can_ifindex) {
		perror("if_nametoindex");
		return 1;
	}

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

		if (select_stdin)
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
