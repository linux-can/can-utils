/*
 *  $Id$
 */

/*
 * can-sniffer.c
 *
 * Copyright (c) 2002-2007 Volkswagen Group Electronic Research
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
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include <libgen.h>
#include <time.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <net/if.h>

#include <linux/can.h>
#include <linux/can/bcm.h>

#include "terminal.h"

#define U64_DATA(p) (*(unsigned long long*)(p)->data)

#define SETFNAME "sniffset."
#define ANYDEV   "any"

/* flags */

#define ENABLE  1 /* by filter or user */
#define DISPLAY 2 /* is on the screen */
#define UPDATE  4 /* needs to be printed on the screen */
#define CLRSCR  8 /* clear screen in next loop */

/* flags testing & setting */

#define is_set(id, flag) (sniftab[id].flags & flag)
#define is_clr(id, flag) (!(sniftab[id].flags & flag))

#define do_set(id, flag) (sniftab[id].flags |= flag)
#define do_clr(id, flag) (sniftab[id].flags &= ~flag)

/* time defaults */

#define TIMEOUT 50 /* in 100ms */
#define HOLD    10 /* in 100ms */
#define LOOP     2 /* in 100ms */

#define MAXANI 8
const char anichar[MAXANI] = {'|', '/', '-', '\\', '|', '/', '-', '\\'};

#define ATTCOLOR ATTBOLD FGRED

#define STARTLINESTR "X  time    ID  data ... "

struct snif {
	int flags;
	long hold;
	long timeout;
	struct timeval laststamp;
	struct timeval currstamp;
	struct can_frame last;
	struct can_frame current;
	struct can_frame marker;
	struct can_frame notch;
} sniftab[2048];


extern int optind, opterr, optopt;

static int running = 1;
static int clearscreen = 1;
static int notch;
static int filter_id_only;
static long timeout = TIMEOUT;
static long hold = HOLD;
static long loop = LOOP;
static unsigned char binary;
static unsigned char binary_gap;
static unsigned char color;
static char *interface;

void rx_setup (int fd, int id);
void rx_delete (int fd, int id);
void print_snifline(int id);
int handle_keyb(int fd);
int handle_bcm(int fd, long currcms);
int handle_timeo(int fd, long currcms);
void writesettings(char* name);
void readsettings(char* name, int sockfd);

void print_usage(char *prg)
{
	const char manual [] = {
		"commands that can be entered at runtime:\n"
		"\n"
		"q<ENTER>       - quit\n"
		"b<ENTER>       - toggle binary / HEX-ASCII output\n"
		"B<ENTER>       - toggle binary with gap / HEX-ASCII output (exceeds 80 chars!)\n"
		"c<ENTER>       - toggle color mode\n"
		"#<ENTER>       - notch currently marked/changed bits (can be used repeatedly)\n"
		"*<ENTER>       - clear notched marked\n"
		"rMYNAME<ENTER> - read settings file (filter/notch)\n"
		"wMYNAME<ENTER> - write settings file (filter/notch)\n"
		"+FILTER<ENTER> - add CAN-IDs to sniff\n"
		"-FILTER<ENTER> - remove CAN-IDs to sniff\n"
		"\n"
		"FILTER can be a single CAN-ID or a CAN-ID/Bitmask:\n"
		"+1F5<ENTER>    - add CAN-ID 0x1F5\n"
		"-42E<ENTER>    - remove CAN-ID 0x42E\n"
		"-42E7FF<ENTER> - remove CAN-ID 0x42E (using Bitmask)\n"
		"-500700<ENTER> - remove CAN-IDs 0x500 - 0x5FF\n"
		"+400600<ENTER> - add CAN-IDs 0x400 - 0x5FF\n"
		"+000000<ENTER> - add all CAN-IDs\n"
		"-000000<ENTER> - remove all CAN-IDs\n"
		"\n"
		"if (id & filter) == (sniff-id & filter) the action (+/-) is performed,\n"
		"which is quite easy when the filter is 000\n"
		"\n"
	};

	fprintf(stderr, "\nUsage: %s [can-interface]\n", prg);
	fprintf(stderr, "Options: -m <mask>  (initial FILTER default 0x00000000)\n");
	fprintf(stderr, "         -v <value> (initial FILTER default 0x00000000)\n");
	fprintf(stderr, "         -q         (quiet - all IDs deactivated)\n");
	fprintf(stderr, "         -r <name>  (read %sname from file)\n", SETFNAME);
	fprintf(stderr, "         -b         (start with binary mode)\n");
	fprintf(stderr, "         -B         (start with binary mode with gap - exceeds 80 chars!)\n");
	fprintf(stderr, "         -c         (color changes)\n");
	fprintf(stderr, "         -f         (filter on CAN-ID only)\n");
	fprintf(stderr, "         -t <time>  (timeout for ID display [x100ms] default: %d, 0 = OFF)\n", TIMEOUT);
	fprintf(stderr, "         -h <time>  (hold marker on changes [x100ms] default: %d)\n", HOLD);
	fprintf(stderr, "         -l <time>  (loop time (display) [x100ms] default: %d)\n", LOOP);
	fprintf(stderr, "Use interface name '%s' to receive from all can-interfaces\n", ANYDEV);
	fprintf(stderr, "\n");
	fprintf(stderr, "%s", manual);
}

void sigterm(int signo)
{
	running = 0;
}

int main(int argc, char **argv)
{
	fd_set rdfs;
	int s;
	canid_t mask = 0;
	canid_t value = 0;
	long currcms = 0;
	long lastcms = 0;
	unsigned char quiet = 0;
	int opt, ret;
	struct timeval timeo, start_tv, tv;
	struct sockaddr_can addr;
	struct ifreq ifr;
	int i;


	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);
	signal(SIGINT, sigterm);

	for (i=0; i < 2048 ;i++) /* default: check all CAN-IDs */
		do_set(i, ENABLE);

	while ((opt = getopt(argc, argv, "m:v:r:t:h:l:qbBcf?")) != -1) {
		switch (opt) {
		case 'm':
			sscanf(optarg, "%x", &mask);
			break;

		case 'v':
			sscanf(optarg, "%x", &value);
			break;

		case 'r':
			readsettings(optarg, 0); /* no BCM-setting here */
			break;

		case 't':
			sscanf(optarg, "%ld", &timeout);
			break;

		case 'h':
			sscanf(optarg, "%ld", &hold);
			break;

		case 'l':
			sscanf(optarg, "%ld", &loop);
			break;

		case 'q':
			quiet = 1;
			break;

		case 'b':
			binary = 1;
			binary_gap = 0;
			break;

		case 'B':
			binary = 1;
			binary_gap = 1;
			break;

		case 'c':
			color = 1;
			break;

		case 'f':
			filter_id_only = 1;
			break;

		case '?':
			break;

		default:
			fprintf(stderr, "Unknown option %c\n", opt);
			break;
		}
	}

	if (optind == argc) {
		print_usage(basename(argv[0]));
		exit(0);
	}
	
	if (mask || value) {
		for (i=0; i < 2048 ;i++) {
			if ((i & mask) ==  (value & mask))
				do_set(i, ENABLE);
			else
				do_clr(i, ENABLE);
		}
	}

	if (quiet)
		for (i=0; i < 2048 ;i++)
			do_clr(i, ENABLE);

	if (strlen(argv[optind]) >= IFNAMSIZ) {
		printf("name of CAN device '%s' is too long!\n", argv[optind]);
		return 1;
	}

	interface = argv[optind];

	if ((s = socket(PF_CAN, SOCK_DGRAM, CAN_BCM)) < 0) {
		perror("socket");
		return 1;
	}

	addr.can_family = AF_CAN;

	if (strcmp(ANYDEV, argv[optind])) {
		strcpy(ifr.ifr_name, argv[optind]);
		if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
			perror("SIOCGIFINDEX");
			exit(1);
		}
		addr.can_ifindex = ifr.ifr_ifindex;
	}
	else
		addr.can_ifindex = 0; /* any can interface */

	if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("connect");
		return 1;
	}

	for (i=0; i < 2048 ;i++) /* initial BCM setup */
		if (is_set(i, ENABLE))
			rx_setup(s, i);

	gettimeofday(&start_tv, NULL);
	tv.tv_sec = tv.tv_usec = 0;

	printf("%s", CSR_HIDE); /* hide cursor */

	while (running) {

		FD_ZERO(&rdfs);
		FD_SET(0, &rdfs);
		FD_SET(s, &rdfs);

		timeo.tv_sec  = 0;
		timeo.tv_usec = 100000 * loop;

		if ((ret = select(s+1, &rdfs, NULL, NULL, &timeo)) < 0) {
			//perror("select");
			running = 0;
			continue;
		}

		gettimeofday(&tv, NULL);
		currcms = (tv.tv_sec - start_tv.tv_sec) * 10 + (tv.tv_usec / 100000);

		if (FD_ISSET(0, &rdfs))
			running &= handle_keyb(s);

		if (FD_ISSET(s, &rdfs))
			running &= handle_bcm(s, currcms);

		if (currcms - lastcms >= loop) {
			running &= handle_timeo(s, currcms);
			lastcms = currcms;
		}
	}

	printf("%s", CSR_SHOW); /* show cursor */

	close(s);
	return 0;
}

void rx_setup (int fd, int id){

	struct {
		struct bcm_msg_head msg_head;
		struct can_frame frame;
	} txmsg;

	txmsg.msg_head.opcode  = RX_SETUP;
	txmsg.msg_head.can_id  = id;
	txmsg.msg_head.flags   = RX_CHECK_DLC;
	txmsg.msg_head.ival1.tv_sec  = 0;
	txmsg.msg_head.ival1.tv_usec = 0;
	txmsg.msg_head.ival2.tv_sec  = 0;
	txmsg.msg_head.ival2.tv_usec = 0;
	txmsg.msg_head.nframes = 1;
	U64_DATA(&txmsg.frame) = (__u64) 0xFFFFFFFFFFFFFFFFULL;

	if (filter_id_only)
		txmsg.msg_head.flags |= RX_FILTER_ID;

	if (write(fd, &txmsg, sizeof(txmsg)) < 0)
		perror("write");
};

void rx_delete (int fd, int id){

	struct bcm_msg_head msg_head;

	msg_head.opcode  = RX_DELETE;
	msg_head.can_id  = id;
	msg_head.nframes = 0;

	if (write(fd, &msg_head, sizeof(msg_head)) < 0)
		perror("write");
}

int handle_keyb(int fd){

	char cmd [20] = {0};
	int i;
	unsigned int mask;
	unsigned int value;

	if (read(0, cmd, 19) > strlen("+123456\n"))
		return 1; /* ignore */

	if (strlen(cmd) > 0)
		cmd[strlen(cmd)-1] = 0; /* chop off trailing newline */

	switch (cmd[0]) {

	case '+':
	case '-':
		sscanf(&cmd[1], "%x", &value);
		if (strlen(&cmd[1]) > 3) {
			mask = value & 0xFFF;
			value >>= 12;
		}
		else
			mask = 0x7FF;

		if (cmd[0] == '+') {
			for (i=0; i < 2048 ;i++) {
				if (((i & mask) == (value & mask)) && (is_clr(i, ENABLE))) {
					do_set(i, ENABLE);
					rx_setup(fd, i);
				}
			}
		}
		else { /* '-' */
			for (i=0; i < 2048 ;i++) {
				if (((i & mask) == (value & mask)) && (is_set(i, ENABLE))) {
					do_clr(i, ENABLE);
					rx_delete(fd, i);
				}
			}
		}
		break;

	case 'w' :
		writesettings(&cmd[1]);
		break;

	case 'r' :
		readsettings(&cmd[1], fd);
		break;

	case 'q' :
		running = 0;
		break;

	case 'B' :
		binary_gap = 1;
		if (binary)
			binary = 0;
		else
			binary = 1;

		break;

	case 'b' :
		binary_gap = 0;
		if (binary)
			binary = 0;
		else
			binary = 1;

		break;

	case 'c' :
		if (color)
			color = 0;
		else
			color = 1;

		break;

	case '#' :
		notch = 1;
		break;

	case '*' :
		for (i=0; i < 2048; i++)
			U64_DATA(&sniftab[i].notch) = (__u64) 0;
		break;

	default:
		break;
	}

	clearscreen = 1;

	return 1; /* ok */
};

int handle_bcm(int fd, long currcms){

	int nbytes, id;

	struct {
		struct bcm_msg_head msg_head;
		struct can_frame frame;
	} bmsg;

	if ((nbytes = read(fd, &bmsg, sizeof(bmsg))) < 0) {
		perror("bcm read");
		return 0; /* quit */
	}

	id = bmsg.msg_head.can_id;
	ioctl(fd, SIOCGSTAMP, &sniftab[id].currstamp);

	if (bmsg.msg_head.opcode != RX_CHANGED) {
		printf("received strange BCM opcode %d!\n", bmsg.msg_head.opcode);
		return 0; /* quit */
	}

	if (nbytes != sizeof(bmsg)) {
		printf("received strange BCM data length %d!\n", nbytes);
		return 0; /* quit */
	}

	sniftab[id].current = bmsg.frame;
	U64_DATA(&sniftab[id].marker) |= 
		U64_DATA(&sniftab[id].current) ^ U64_DATA(&sniftab[id].last);
	sniftab[id].timeout = (timeout)?(currcms + timeout):0;

	if (is_clr(id, DISPLAY))
		clearscreen = 1; /* new entry -> new drawing */

	do_set(id, DISPLAY);
	do_set(id, UPDATE);
	
	return 1; /* ok */
};

int handle_timeo(int fd, long currcms){

	int i;
	int force_redraw = 0;

	if (clearscreen) {
		char startline[80];
		printf("%s%s", CLR_SCREEN, CSR_HOME);
		snprintf(startline, 79, "< cansniffer %s # l=%ld h=%ld t=%ld >", interface, loop, hold, timeout);
		printf("%s%*s",STARTLINESTR, 79-(int)strlen(STARTLINESTR), startline);
		force_redraw = 1;
		clearscreen = 0;
	}

	if (notch) {
		for (i=0; i < 2048; i++)
			U64_DATA(&sniftab[i].notch) |= U64_DATA(&sniftab[i].marker);
		notch = 0;
	}

	printf("%s", CSR_HOME);
	printf("%c\n", anichar[currcms % MAXANI]); /* funny animation */

	for (i=0; i < 2048; i++) {

		if is_set(i, ENABLE) {

				if is_set(i, DISPLAY) {

						if (is_set(i, UPDATE) || (force_redraw)){
							print_snifline(i);
							sniftab[i].hold = currcms + hold;
							do_clr(i, UPDATE);
						}
						else
							if ((sniftab[i].hold) && (sniftab[i].hold < currcms)) {
								U64_DATA(&sniftab[i].marker) = (__u64) 0;
								print_snifline(i);
								sniftab[i].hold = 0; /* disable update by hold */
							}
							else
								printf("%s", CSR_DOWN); /* skip my line */

						if (sniftab[i].timeout && sniftab[i].timeout < currcms) {
							do_clr(i, DISPLAY);
							do_clr(i, UPDATE);
							clearscreen = 1; /* removed entry -> new drawing next time */
						}
					}
				sniftab[i].last      = sniftab[i].current;
				sniftab[i].laststamp = sniftab[i].currstamp;
			}
	}

	return 1; /* ok */

};

void print_snifline(int id){

	long diffsec  = sniftab[id].currstamp.tv_sec  - sniftab[id].laststamp.tv_sec;
	long diffusec = sniftab[id].currstamp.tv_usec - sniftab[id].laststamp.tv_usec;
	int dlc_diff  = sniftab[id].last.can_dlc - sniftab[id].current.can_dlc;
	int i,j;

	if (diffusec < 0)
		diffsec--, diffusec += 1000000;

	if (diffsec < 0)
		diffsec = diffusec = 0;

	if (diffsec > 10)
		diffsec = 9, diffusec = 999999;

	printf("%ld.%06ld  %3x  ", diffsec, diffusec, id);

	if (binary) {

		for (i=0; i<sniftab[id].current.can_dlc; i++) {
			for (j=7; j>=0; j--) {
				if ((color) && (sniftab[id].marker.data[i] & 1<<j) &&
				    (!(sniftab[id].notch.data[i] & 1<<j)))
					if (sniftab[id].current.data[i] & 1<<j)
						printf("%s1%s", ATTCOLOR, ATTRESET);
					else
						printf("%s0%s", ATTCOLOR, ATTRESET);
				else
					if (sniftab[id].current.data[i] & 1<<j)
						putchar('1');
					else
						putchar('0');
			}
			if (binary_gap)
				putchar(' ');
		}

		/*
		 * when the can_dlc decreased (dlc_diff > 0),
		 * we need to blank the former data printout
		 */
		for (i=0; i<dlc_diff; i++) {
			printf("        ");
			if (binary_gap)
				putchar(' ');
		}
	}
	else {

		for (i=0; i<sniftab[id].current.can_dlc; i++)
			if ((color) && (sniftab[id].marker.data[i]) && (!(sniftab[id].notch.data[i])))
				printf("%s%02X%s ", ATTCOLOR, sniftab[id].current.data[i], ATTRESET);
			else
				printf("%02X ", sniftab[id].current.data[i]);

		if (sniftab[id].current.can_dlc < 8)
			printf("%*s", (8 - sniftab[id].current.can_dlc) * 3, "");

		for (i=0; i<sniftab[id].current.can_dlc; i++)
			if ((sniftab[id].current.data[i] > 0x1F) && 
			    (sniftab[id].current.data[i] < 0x7F))
				if ((color) && (sniftab[id].marker.data[i]) && (!(sniftab[id].notch.data[i])))
					printf("%s%c%s", ATTCOLOR, sniftab[id].current.data[i], ATTRESET);
				else
					putchar(sniftab[id].current.data[i]);
			else
				putchar('.');

		/*
		 * when the can_dlc decreased (dlc_diff > 0),
		 * we need to blank the former data printout
		 */
		for (i=0; i<dlc_diff; i++)
			putchar(' ');
	}

	putchar('\n');

	U64_DATA(&sniftab[id].marker) = (__u64) 0;

};


void writesettings(char* name){

	int fd;
	char fname[30] = SETFNAME;
	int i,j;
	char buf[8]= {0};

	strncat(fname, name, 29 - strlen(fname)); 
	fd = open(fname,  O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    
	if (fd > 0) {

		for (i=0; i < 2048 ;i++) {
			sprintf(buf, "<%03X>%c.", i, (is_set(i, ENABLE))?'1':'0');
			write(fd, buf, 7);
			for (j=0; j<8 ; j++){
				sprintf(buf, "%02X", sniftab[i].notch.data[j]);
				write(fd, buf, 2);
			}
			write(fd, "\n", 1);
			/* 7 + 16 + 1 = 24 bytes per entry */ 
		}
		close(fd);
	}
	else
		printf("unable to write setting file '%s'!\n", fname);
};

void readsettings(char* name, int sockfd){

	int fd;
	char fname[30] = SETFNAME;
	char buf[25] = {0};
	int i,j;

	strncat(fname, name, 29 - strlen(fname)); 
	fd = open(fname, O_RDONLY);
    
	if (fd > 0) {
		if (!sockfd)
			printf("reading setting file '%s' ... ", fname);

		for (i=0; i < 2048 ;i++) {
			if (read(fd, &buf, 24) == 24) {
				if (buf[5] & 1) {
					if (is_clr(i, ENABLE)) {
						do_set(i, ENABLE);
						if (sockfd)
							rx_setup(sockfd, i);
					}
				}
				else
					if (is_set(i, ENABLE)) {
						do_clr(i, ENABLE);
						if (sockfd)
							rx_delete(sockfd, i);
					}
				for (j=7; j>=0 ; j--){
					sniftab[i].notch.data[j] =
						(__u8) strtoul(&buf[2*j+7], (char **)NULL, 16) & 0xFF;
					buf[2*j+7] = 0; /* cut off each time */
				}
			}
			else {
				if (!sockfd)
					printf("was only able to read until index %d from setting file '%s'!\n",
					       i, fname);
			}
		}
    
		if (!sockfd)
			printf("done\n");

		close(fd);
	}
	else
		printf("unable to read setting file '%s'!\n", fname);
};
