/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * cansniffer.c - volatile CAN content visualizer
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
#include <stdint.h>
#include <stdbool.h>
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
#include <linux/can/raw.h>
#include <linux/sockios.h>

#include "terminal.h"

#define SETFNAME "sniffset."
#define ANYDEV   "any"
#define MAX_SLOTS 2048

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

#define TIMEOUT 500 /* in 10ms */
#define HOLD    100 /* in 10ms */
#define LOOP     20 /* in 10ms */

#define ATTCOLOR ATTBOLD FGRED

#define LDL " | "	/* long delimiter */
#define SDL "|"		/* short delimiter for binary on 80 chars terminal */

static struct snif {
	int flags;
	long hold;
	long timeout;
	struct timeval laststamp;
	struct timeval currstamp;
	struct can_frame last;
	struct can_frame current;
	struct can_frame marker;
	struct can_frame notch;
} sniftab[MAX_SLOTS];

extern int optind, opterr, optopt;

static int idx;
static int running = 1;
static int clearscreen = 1;
static int print_eff;
static int notch;
static long timeout = TIMEOUT;
static long hold = HOLD;
static long loop = LOOP;
static unsigned char binary;
static unsigned char binary8;
static unsigned char binary_gap;
static unsigned char color;
static char *interface;
static char *vdl = LDL; /* variable delimiter */
static char *ldl = LDL; /* long delimiter */

void print_snifline(int slot);
int handle_keyb(void);
int handle_frame(int fd, long currcms);
int handle_timeo(long currcms);
void writesettings(char* name);
int readsettings(char* name);
int sniftab_index(canid_t id);

void switchvdl(char *delim)
{
	/* reduce delimiter size for EFF IDs in binary display of up
	   to 8 data bytes payload to fit into 80 chars per line */
	if (binary8)
		vdl = delim;
}

int comp(const void *elem1, const void *elem2)
{
    unsigned long f = ((struct snif*)elem1)->current.can_id;
    unsigned long s = ((struct snif*)elem2)->current.can_id;

    if (f > s)
	    return  1;
    if (f < s)
	    return -1;

    return 0;
}

void print_usage(char *prg)
{
	const char manual [] = {
		"commands that can be entered at runtime:\n"
		" q<ENTER>        - quit\n"
		" b<ENTER>        - toggle binary / HEX-ASCII output\n"
		" 8<ENTER>        - toggle binary / HEX-ASCII output (small for EFF on 80 chars)\n"
		" B<ENTER>        - toggle binary with gap / HEX-ASCII output (exceeds 80 chars!)\n"
		" c<ENTER>        - toggle color mode\n"
		" <SPACE><ENTER>  - force a clear screen\n"
		" #<ENTER>        - notch currently marked/changed bits (can be used repeatedly)\n"
		" *<ENTER>        - clear notched marked\n"
		" rMYNAME<ENTER>  - read settings file (filter/notch)\n"
		" wMYNAME<ENTER>  - write settings file (filter/notch)\n"
		" a<ENTER>        - enable 'a'll SFF CAN-IDs to sniff\n"
		" n<ENTER>        - enable 'n'one SFF CAN-IDs to sniff\n"
		" A<ENTER>        - enable 'A'll EFF CAN-IDs to sniff\n"
		" N<ENTER>        - enable 'N'one EFF CAN-IDs to sniff\n"
		" +FILTER<ENTER>  - add CAN-IDs to sniff\n"
		" -FILTER<ENTER>  - remove CAN-IDs to sniff\n"
		"\n"
		"FILTER can be a single CAN-ID or a CAN-ID/Bitmask:\n"
		"\n"
		" single SFF 11 bit IDs:\n"
		"  +1F5<ENTER>               - add SFF CAN-ID 0x1F5\n"
		"  -42E<ENTER>               - remove SFF CAN-ID 0x42E\n"
		"\n"
		" single EFF 29 bit IDs:\n"
		"  +18FEDF55<ENTER>          - add EFF CAN-ID 0x18FEDF55\n"
		"  -00000090<ENTER>          - remove EFF CAN-ID 0x00000090\n"
		"\n"
		" CAN-ID/Bitmask SFF:\n"
		"  -42E7FF<ENTER>            - remove SFF CAN-ID 0x42E (using Bitmask)\n"
		"  -500700<ENTER>            - remove SFF CAN-IDs 0x500 - 0x5FF\n"
		"  +400600<ENTER>            - add SFF CAN-IDs 0x400 - 0x5FF\n"
		"  +000000<ENTER>            - add all SFF CAN-IDs\n"
		"  -000000<ENTER>            - remove all SFF CAN-IDs\n"
		"\n"
		" CAN-ID/Bitmask EFF:\n"
		"  -0000000000000000<ENTER>  - remove all EFF CAN-IDs\n"
		"  +12345678000000FF<ENTER>  - add EFF CAN IDs xxxxxx78\n"
		"  +0000000000000000<ENTER>  - add all EFF CAN-IDs\n"
		"\n"
		"if (id & filter) == (sniff-id & filter) the action (+/-) is performed,\n"
		"which is quite easy when the filter is 000 resp. 00000000 for EFF.\n"
		"\n"
	};

	fprintf(stderr, "%s - volatile CAN content visualizer.\n", prg);
	fprintf(stderr, "\nUsage: %s [can-interface]\n", prg);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "         -q          (quiet - all IDs deactivated)\n");
	fprintf(stderr, "         -r <name>   (read %sname from file)\n", SETFNAME);
	fprintf(stderr, "         -e          (fix extended frame format output - no auto detect)\n");
	fprintf(stderr, "         -b          (start with binary mode)\n");
	fprintf(stderr, "         -8          (start with binary mode - for EFF on 80 chars)\n");
	fprintf(stderr, "         -B          (start with binary mode with gap - exceeds 80 chars!)\n");
	fprintf(stderr, "         -c          (color changes)\n");
	fprintf(stderr, "         -t <time>   (timeout for ID display [x10ms] default: %d, 0 = OFF)\n", TIMEOUT);
	fprintf(stderr, "         -h <time>   (hold marker on changes [x10ms] default: %d)\n", HOLD);
	fprintf(stderr, "         -l <time>   (loop time (display) [x10ms] default: %d)\n", LOOP);
	fprintf(stderr, "         -?          (print this help text)\n");
	fprintf(stderr, "Use interface name '%s' to receive from all can-interfaces.\n", ANYDEV);
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
	long currcms = 0;
	long lastcms = 0;
	unsigned char quiet = 0;
	int opt, ret;
	struct timeval timeo, start_tv, tv;
	struct sockaddr_can addr;
	int i;

	signal(SIGTERM, sigterm);
	signal(SIGHUP, sigterm);
	signal(SIGINT, sigterm);

	for (i = 0; i < MAX_SLOTS ;i++) /* default: enable all slots */
		do_set(i, ENABLE);

	while ((opt = getopt(argc, argv, "r:t:h:l:qeb8Bc?")) != -1) {
		switch (opt) {
		case 'r':
			if (readsettings(optarg) < 0) {
				fprintf(stderr, "Unable to read setting file '%s%s'!\n", SETFNAME, optarg);
				exit(1);
			}
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

		case 'e':
			print_eff = 1;
			break;

		case 'b':
			binary = 1;
			binary_gap = 0;
			break;

		case '8':
			binary = 1;
			binary8 = 1; /* enable variable delimiter for EFF */
			switchvdl(SDL); /* switch directly to short delimiter */
			binary_gap = 0;
			break;

		case 'B':
			binary = 1;
			binary_gap = 1;
			break;

		case 'c':
			color = 1;
			break;

		case '?':
			print_usage(basename(argv[0]));
			exit(0);
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
	
	if (quiet)
		for (i = 0; i < MAX_SLOTS; i++)
			do_clr(i, ENABLE);

	if (strlen(argv[optind]) >= IFNAMSIZ) {
		printf("name of CAN device '%s' is too long!\n", argv[optind]);
		return 1;
	}

	interface = argv[optind];

	s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) {
		perror("socket");
		return 1;
	}

	addr.can_family = AF_CAN;

	if (strcmp(ANYDEV, argv[optind]))
		addr.can_ifindex = if_nametoindex(argv[optind]);
	else
		addr.can_ifindex = 0; /* any can interface */

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("connect");
		return 1;
	}

	gettimeofday(&start_tv, NULL);
	tv.tv_sec = tv.tv_usec = 0;

	printf("%s", CSR_HIDE); /* hide cursor */

	while (running) {

		FD_ZERO(&rdfs);
		FD_SET(0, &rdfs);
		FD_SET(s, &rdfs);

		timeo.tv_sec  = 0;
		timeo.tv_usec = 10000 * loop;

		if ((ret = select(s+1, &rdfs, NULL, NULL, &timeo)) < 0) {
			//perror("select");
			running = 0;
			continue;
		}

		gettimeofday(&tv, NULL);
		currcms = (tv.tv_sec - start_tv.tv_sec) * 100 + (tv.tv_usec / 10000);

		if (FD_ISSET(0, &rdfs))
			running &= handle_keyb();

		if (FD_ISSET(s, &rdfs))
			running &= handle_frame(s, currcms);

		if (currcms - lastcms >= loop) {
			running &= handle_timeo(currcms);
			lastcms = currcms;
		}
	}

	printf("%s", CSR_SHOW); /* show cursor */

	close(s);
	return 0;
}

void do_modify_sniftab(unsigned int value, unsigned int mask, char cmd)
{
	int i;

	for (i = 0; i < idx ;i++) {
		if ((sniftab[i].current.can_id & mask) == (value & mask)) {
			if (cmd == '+')
				do_set(i, ENABLE);
			else
				do_clr(i, ENABLE);
		}
	}
}

int handle_keyb(void)
{
	char cmd [25] = {0};
	int i, clen;
	unsigned int mask;
	unsigned int value;

	if (read(0, cmd, 24) > (long)strlen("+1234567812345678\n"))
		return 1; /* ignore */

	if (strlen(cmd) > 0)
		cmd[strlen(cmd)-1] = 0; /* chop off trailing newline */

	clen = strlen(&cmd[1]); /* content length behind command */

	switch (cmd[0]) {

	case '+':
	case '-':
		if (clen == 6) {
			/* masking strict SFF ID content vvvmmm */
			sscanf(&cmd[1], "%x", &value);
			mask = value | 0xFFFF800; /* cleared flags! */
			value >>= 12;
			value &= 0x7FF;
			do_modify_sniftab(value, mask, cmd[0]);
			break;
		} else if (clen == 16) {
			sscanf(&cmd[9], "%x", &mask);
			cmd[9] = 0; /* terminate 'value' */
			sscanf(&cmd[1], "%x", &value);
			mask |= CAN_EFF_FLAG;
			value |= CAN_EFF_FLAG;
			do_modify_sniftab(value, mask, cmd[0]);
			break;
		}

		/* check for single SFF/EFF CAN ID length */
		if (!((clen == 3) || (clen == 8)))
			break;

		/* enable/disable single SFF/EFF CAN ID */
		sscanf(&cmd[1], "%x", &value);
		if (clen == 8)
			value |= CAN_EFF_FLAG;

		i = sniftab_index(value);
		if (i < 0)
			break; /* No Match */

		if (cmd[0] == '+')
			do_set(i, ENABLE);
		else
			do_clr(i, ENABLE);

		break;

	case 'a' : /* all SFF CAN IDs */
		value = 0;
		mask = 0xFFFF800; /* cleared flags! */
		do_modify_sniftab(value, mask, '+');
		break;

	case 'n' : /* none SFF CAN IDs */
		value = 0;
		mask = 0xFFFF800; /* cleared flags! */
		do_modify_sniftab(value, mask, '-');
		break;

	case 'A' : /* all EFF CAN IDs */
		value = CAN_EFF_FLAG;
		mask = CAN_EFF_FLAG;
		do_modify_sniftab(value, mask, '+');
		break;

	case 'N' : /* none EFF CAN IDs */
		value = CAN_EFF_FLAG;
		mask = CAN_EFF_FLAG;
		do_modify_sniftab(value, mask, '-');
		break;

	case 'w' :
		writesettings(&cmd[1]);
		break;

	case 'r' :
		readsettings(&cmd[1]); /* don't care about success */
		break;

	case 'q' :
		running = 0;
		break;

	case 'B' :
		binary_gap = 1;
		switchvdl(LDL);
		if (binary)
			binary = 0;
		else
			binary = 1;

		break;

	case '8' :
		binary8 = 1;
		/* fallthrough */

	case 'b' :
		binary_gap = 0;
		if (binary) {
			binary = 0;
			switchvdl(LDL);
		} else {
			binary = 1;
			switchvdl(SDL);
		}
		break;

	case 'c' :
		if (color)
			color = 0;
		else
			color = 1;

		break;

	case ' ' :
		clearscreen = 1;
		break;

	case '#' :
		notch = 1;
		break;

	case '*' :
		for (i = 0; i < idx; i++)
			memset(&sniftab[i].notch.data, 0, 8);
		break;

	default:
		break;
	}

	clearscreen = 1;

	return 1; /* ok */
};

int handle_frame(int fd, long currcms)
{
	bool rx_changed = false;
	bool run_qsort = false;
	int nbytes, i, pos;
	struct can_frame cf;

	if ((nbytes = read(fd, &cf, sizeof(cf))) < 0) {
		perror("raw read");
		return 0; /* quit */
	}

	if (nbytes != CAN_MTU) {
		printf("received strange frame data length %d!\n", nbytes);
		return 0; /* quit */
	}

	if (!print_eff && (cf.can_id & CAN_EFF_FLAG)) {
		print_eff = 1;
		clearscreen = 1;
	}

	pos = sniftab_index(cf.can_id);
	if (pos < 0) {
		/* CAN ID not existing */
		if (idx < MAX_SLOTS) {
			/* assign new slot */
			pos = idx++;
			rx_changed = true;
			run_qsort = true;
		} else {
			/* informative exit */
			perror("number of different CAN IDs exceeded MAX_SLOTS");
			return 0; /* quit */
		}
	}
	else {
		if (cf.can_dlc == sniftab[pos].current.can_dlc)
			for (i = 0; i < cf.can_dlc; i++) {
				if (cf.data[i] != sniftab[pos].current.data[i] ) {
					rx_changed = true;
					break;
				}
			}
		else
			rx_changed = true;
	}

	/* print received frame even if the data didn't change to get a gap time */
	if ((sniftab[pos].laststamp.tv_sec == 0) && (sniftab[pos].laststamp.tv_usec == 0))
		rx_changed = true;

	if (rx_changed == true) {
		sniftab[pos].laststamp = sniftab[pos].currstamp;
		ioctl(fd, SIOCGSTAMP, &sniftab[pos].currstamp);

		sniftab[pos].current = cf;
		for (i = 0; i < 8; i++)
			sniftab[pos].marker.data[i] |= sniftab[pos].current.data[i] ^ sniftab[pos].last.data[i];

		sniftab[pos].timeout = (timeout)?(currcms + timeout):0;

		if (is_clr(pos, DISPLAY))
			clearscreen = 1; /* new entry -> new drawing */

		do_set(pos, DISPLAY);
		do_set(pos, UPDATE);
	}

	if (run_qsort == true)
		qsort(sniftab, idx, sizeof(sniftab[0]), comp);

	return 1; /* ok */
};

int handle_timeo(long currcms)
{
	int i, j;
	int force_redraw = 0;
	static unsigned int frame_count;

	if (clearscreen) {
		if (print_eff)
			printf("%s%sXX|ms%s-- ID --%sdata ...     < %s # l=%ld h=%ld t=%ld slots=%d >",
			       CLR_SCREEN, CSR_HOME, vdl, vdl, interface, loop, hold, timeout, idx);
		else
			printf("%s%sXX|ms%sID %sdata ...     < %s # l=%ld h=%ld t=%ld slots=%d >",
			       CLR_SCREEN, CSR_HOME, ldl, ldl, interface, loop, hold, timeout, idx);

		force_redraw = 1;
		clearscreen = 0;
	}

	if (notch) {
		for (i = 0; i < idx; i++) {
			for (j = 0; j < 8; j++)
				sniftab[i].notch.data[j] |= sniftab[i].marker.data[j];
		}
		notch = 0;
	}

	printf("%s", CSR_HOME);
	printf("%02d\n", frame_count++); /* rolling display update counter */
	frame_count %= 100;

	for (i = 0; i < idx; i++) {
		if is_set(i, ENABLE) {
				if is_set(i, DISPLAY) {
						if (is_set(i, UPDATE) || (force_redraw)) {
							print_snifline(i);
							sniftab[i].hold = currcms + hold;
							do_clr(i, UPDATE);
						}
						else  if ((sniftab[i].hold) && (sniftab[i].hold < currcms)) {
								memset(&sniftab[i].marker.data, 0, 8);
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
			}
	}

	return 1; /* ok */
};

void print_snifline(int slot)
{
	long diffsec  = sniftab[slot].currstamp.tv_sec  - sniftab[slot].laststamp.tv_sec;
	long diffusec = sniftab[slot].currstamp.tv_usec - sniftab[slot].laststamp.tv_usec;
	int dlc_diff  = sniftab[slot].last.can_dlc - sniftab[slot].current.can_dlc;
	canid_t cid = sniftab[slot].current.can_id;
	int i,j;

	if (diffusec < 0)
		diffsec--, diffusec += 1000000;

	if (diffsec < 0)
		diffsec = diffusec = 0;

	if (diffsec >= 100)
		diffsec = 99, diffusec = 999999;

	if (cid & CAN_EFF_FLAG)
		printf("%02ld%03ld%s%08X%s", diffsec, diffusec/1000, vdl, cid & CAN_EFF_MASK, vdl);
	else if (print_eff)
		printf("%02ld%03ld%s---- %03X%s", diffsec, diffusec/1000, vdl, cid & CAN_SFF_MASK, vdl);
	else
		printf("%02ld%03ld%s%03X%s", diffsec, diffusec/1000, ldl, cid & CAN_SFF_MASK, ldl);

	if (binary) {
		for (i = 0; i < sniftab[slot].current.can_dlc; i++) {
			for (j=7; j >= 0; j--) {
				if ((color) && (sniftab[slot].marker.data[i] & 1<<j) &&
				    (!(sniftab[slot].notch.data[i] & 1<<j)))
					if (sniftab[slot].current.data[i] & 1<<j)
						printf("%s1%s", ATTCOLOR, ATTRESET);
					else
						printf("%s0%s", ATTCOLOR, ATTRESET);
				else
					if (sniftab[slot].current.data[i] & 1<<j)
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
		for (i = 0; i < dlc_diff; i++) {
			printf("        ");
			if (binary_gap)
				putchar(' ');
		}
	}
	else {
		for (i = 0; i < sniftab[slot].current.can_dlc; i++)
			if ((color) && (sniftab[slot].marker.data[i] & ~sniftab[slot].notch.data[i]))
				printf("%s%02X%s ", ATTCOLOR, sniftab[slot].current.data[i], ATTRESET);
			else
				printf("%02X ", sniftab[slot].current.data[i]);

		if (sniftab[slot].current.can_dlc < 8)
			printf("%*s", (8 - sniftab[slot].current.can_dlc) * 3, "");

		for (i = 0; i<sniftab[slot].current.can_dlc; i++)
			if ((sniftab[slot].current.data[i] > 0x1F) &&
			    (sniftab[slot].current.data[i] < 0x7F))
				if ((color) && (sniftab[slot].marker.data[i] & ~sniftab[slot].notch.data[i]))
					printf("%s%c%s", ATTCOLOR, sniftab[slot].current.data[i], ATTRESET);
				else
					putchar(sniftab[slot].current.data[i]);
			else
				putchar('.');

		/*
		 * when the can_dlc decreased (dlc_diff > 0),
		 * we need to blank the former data printout
		 */
		for (i = 0; i < dlc_diff; i++)
			putchar(' ');
	}

	putchar('\n');

	memset(&sniftab[slot].marker.data, 0, 8);
};

void writesettings(char* name)
{
	int fd;
	char fname[30] = SETFNAME;
	int i,j;
	char buf[13]= {0};

	strncat(fname, name, 29 - strlen(fname)); 
	fd = open(fname, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    
	if (fd > 0) {
		for (i = 0; i < idx ;i++) {
			sprintf(buf, "<%08X>%c.", sniftab[i].current.can_id, (is_set(i, ENABLE))?'1':'0');
			if (write(fd, buf, 12) < 0)
				perror("write");
			for (j = 0; j < 8 ; j++) {
				sprintf(buf, "%02X", sniftab[i].notch.data[j]);
				if (write(fd, buf, 2) < 0)
					perror("write");
			}
			if (write(fd, "\n", 1) < 0)
				perror("write");
			/* 12 + 16 + 1 = 29 bytes per entry */
		}
		close(fd);
	}
	else
		printf("unable to write setting file '%s'!\n", fname);
};

int readsettings(char* name)
{
	int fd;
	char fname[30] = SETFNAME;
	char buf[30] = {0};
	int j;
	bool done = false;

	strncat(fname, name, 29 - strlen(fname)); 
	fd = open(fname, O_RDONLY);
    
	if (fd > 0) {
		idx = 0;
		while (!done) {
			if (read(fd, &buf, 29) == 29) {
				unsigned long id = strtoul(&buf[1], (char **)NULL, 16);

				sniftab[idx].current.can_id = id;

				if (buf[10] & 1)
					do_set(idx, ENABLE);
				else
					do_clr(idx, ENABLE);

				for (j = 7; j >= 0 ; j--) {
					sniftab[idx].notch.data[j] =
						(__u8) strtoul(&buf[2*j+12], (char **)NULL, 16) & 0xFF;
					buf[2*j+12] = 0; /* cut off each time */
				}

				if (++idx >= MAX_SLOTS)
					break;
			}
			else
				done = true;
		}
		close(fd);
	}
	else
		return -1;

	return idx;
};

int sniftab_index(canid_t id)
{
	int i;

	for (i = 0; i <= idx; i++)
		if (id == sniftab[i].current.can_id)
			return i;

	return -1; /* No match */
}
