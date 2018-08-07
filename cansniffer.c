/*
 * cansniffer.c
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

#define STARTLINESTR "XX delta   ID  data ... "

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
} sniftab[MAX_SLOTS];

extern int optind, opterr, optopt;

static int idx;
static int running = 1;
static int clearscreen = 1;
static int notch;
static long timeout = TIMEOUT;
static long hold = HOLD;
static long loop = LOOP;
static unsigned char binary;
static unsigned char binary_gap;
static unsigned char color;
static char *interface;

void print_snifline(int slot);
int handle_keyb(int fd);
int handle_frame(int fd, long currcms);
int handle_timeo(int fd, long currcms);
void writesettings(char* name);
void readsettings(char* name, int sockfd);
static int sniftab_index(canid_t id);

int comp (const void * elem1, const void * elem2) 
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
		"\n"
		"q<ENTER>         - quit\n"
		"b<ENTER>         - toggle binary / HEX-ASCII output\n"
		"B<ENTER>         - toggle binary with gap / HEX-ASCII output (exceeds 80 chars!)\n"
		"c<ENTER>         - toggle color mode\n"
		"#<ENTER>         - notch currently marked/changed bits (can be used repeatedly)\n"
		"*<ENTER>         - clear notched marked\n"
		"rMYNAME<ENTER>   - read settings file (filter/notch)\n"
		"wMYNAME<ENTER>   - write settings file (filter/notch)\n"
		"+FILTER<ENTER>   - add CAN-IDs to sniff\n"
		"-FILTER<ENTER>   - remove CAN-IDs to sniff\n"
		"\n"
		"FILTER must be a single CAN-ID:\n"/* "or a CAN-ID/Bitmask:\n"*/
		"+1F5<ENTER>      - add CAN-ID 0x1F5\n"
		"-42E<ENTER>      - remove CAN-ID 0x42E\n"
		"\n"
		"29 bit IDs:\n"
		"+18FEDF55<ENTER> - add CAN-ID 0x18FEDF55\n"
		"-00000090<ENTER> - remove CAN-ID 0x00000090\n"
		"\n"
	};

	fprintf(stderr, "\nUsage: %s [can-interface]\n", prg);
	fprintf(stderr, "Options: -q         (quiet - all slots are deactivated)\n");
	fprintf(stderr, "         -r <name>  (read %sname from file)\n", SETFNAME);
	fprintf(stderr, "         -b         (start with binary mode)\n");
	fprintf(stderr, "         -B         (start with binary mode with gap - exceeds 80 chars!)\n");
	fprintf(stderr, "         -c         (color changes)\n");
	fprintf(stderr, "         -t <time>  (timeout for ID display [x10ms] default: %d, 0 = OFF)\n", TIMEOUT);
	fprintf(stderr, "         -h <time>  (hold marker on changes [x10ms] default: %d)\n", HOLD);
	fprintf(stderr, "         -l <time>  (loop time (display) [x10ms] default: %d)\n", LOOP);
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

	for (i=0; i < MAX_SLOTS ;i++) /* default: enable all slots */
		do_set(i, ENABLE);

	while ((opt = getopt(argc, argv, "m:v:r:t:h:l:qbBcf?")) != -1) {
		switch (opt) {
		case 'r':
			readsettings(optarg, 0);
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
	
	if (quiet)
		for (i = 0; i < MAX_SLOTS; i++)
			do_clr(i, ENABLE);

	if (strlen(argv[optind]) >= IFNAMSIZ) {
		printf("name of CAN device '%s' is too long!\n", argv[optind]);
		return 1;
	}

	interface = argv[optind];

	if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
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
			running &= handle_keyb(s);

		if (FD_ISSET(s, &rdfs))
			running &= handle_frame(s, currcms);

		if (currcms - lastcms >= loop) {
			running &= handle_timeo(s, currcms);
			lastcms = currcms;
		}
	}

	printf("%s%s%s", CSR_SHOW, CLR_SCREEN, CSR_HOME); /* show cursor & clear screen */

	close(s);
	return 0;
}

int handle_keyb(int fd) {

	char cmd [20] = {0};
	int i;
	unsigned int value;

	if (read(0, cmd, 19) > strlen("+12345678\n"))
		return 1; /* ignore */

	if (strlen(cmd) > 0)
		cmd[strlen(cmd)-1] = 0; /* chop off trailing newline */

	switch (cmd[0]) {

	case '+':
	case '-':
		sscanf(&cmd[1], "%x", &value);
		if (strlen(&cmd[1]) > 3)
			value = value | 0x80000000;

		i = sniftab_index(value);

		if (i < 0)
			break; /* No Match */

		if (cmd[0] == '+') {
			do_set(i, ENABLE);
		}
		else { /* '-' */
			do_clr(i, ENABLE);
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
		for (i=0; i < idx; i++)
			memset(&sniftab[i].notch.data, 0, 8);
		break;

	default:
		break;
	}

	clearscreen = 1;

	return 1; /* ok */
};

int handle_frame(int fd, long currcms) {

	bool match = false;
	bool rx_changed = false;
	int nbytes, i, pos = 0;

	canid_t id;
	struct can_frame frame;

	if ((nbytes = read(fd, &frame, sizeof(frame))) < 0) {
		perror("frame read");
		return 0; /* quit */
	}

	id = frame.can_id;

	pos = sniftab_index(id);

	if (pos >= 0)
		match = true;

	if (!match) {
		if (idx < MAX_SLOTS) {
			pos = idx++;
			rx_changed = true;
		}
	}
	else {
		if (frame.can_dlc == sniftab[pos].current.can_dlc)
			for (i=0; i<frame.can_dlc; i++) {
				if (frame.data[i] != sniftab[pos].current.data[i] ) {
					rx_changed = true;
					break;
				}
			}
		else
			rx_changed = true;
	}

	if (nbytes != sizeof(frame)) {
		printf("received strange data length %d!\n", nbytes);
		return 0; /* quit */
	}

	if (rx_changed == true) {
		ioctl(fd, SIOCGSTAMP, &sniftab[pos].currstamp);

		sniftab[pos].current = frame;
		for (i=0; i < 8; i++)
			sniftab[pos].marker.data[i] |= sniftab[pos].current.data[i] ^ sniftab[pos].last.data[i];

		sniftab[pos].timeout = (timeout)?(currcms + timeout):0;

		if (is_clr(pos, DISPLAY))
			clearscreen = 1; /* new entry -> new drawing */

		do_set(pos, DISPLAY);
		do_set(pos, UPDATE);

		qsort (sniftab, idx, sizeof(sniftab[0]), comp);
	}

	return 1; /* ok */
};

int handle_timeo(int fd, long currcms) {

	int i, j;
	int force_redraw = 0;
	static unsigned int frame_count;

	if (clearscreen) {
		char startline[80];
		printf("%s%s", CLR_SCREEN, CSR_HOME);
		snprintf(startline, 79, "< cansniffer %s # l=%ld h=%ld t=%ld s=%d>", interface, loop, hold, timeout, idx);
		printf("%s%*s",STARTLINESTR, 79-(int)strlen(STARTLINESTR), startline);
		force_redraw = 1;
		clearscreen = 0;
	}

	if (notch) {
		for (i=0; i < MAX_SLOTS; i++) {
			for (j=0; j < 8; j++)
				sniftab[i].notch.data[j] |= sniftab[i].marker.data[j];
		}
		notch = 0;
	}

	printf("%s", CSR_HOME);
	printf("%02d\n", frame_count++); /* rolling display update counter */
	frame_count %= 100;

	for (i=0; i < MAX_SLOTS; i++) {

		if is_set(i, ENABLE) {

				if is_set(i, DISPLAY) {

						if (is_set(i, UPDATE) || (force_redraw)) {
							print_snifline(i);
							sniftab[i].hold = currcms + hold;
							do_clr(i, UPDATE);
						}
						else
							if ((sniftab[i].hold) && (sniftab[i].hold < currcms)) {
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
				sniftab[i].laststamp = sniftab[i].currstamp;
			}
	}

	return 1; /* ok */

};

void print_snifline(int slot) {

	long diffsec  = sniftab[slot].currstamp.tv_sec  - sniftab[slot].laststamp.tv_sec;
	long diffusec = sniftab[slot].currstamp.tv_usec - sniftab[slot].laststamp.tv_usec;
	int dlc_diff  = sniftab[slot].last.can_dlc - sniftab[slot].current.can_dlc;
	unsigned long id = sniftab[slot].current.can_id;
	int i,j;

	if (diffusec < 0)
		diffsec--, diffusec += 1000000;

	if (diffsec < 0)
		diffsec = diffusec = 0;

	if (diffsec > 10)
		diffsec = 9, diffusec = 999999;

	if (id & CAN_EFF_FLAG)
		printf("%ld.%06ld EXT 0x%08lX  ", diffsec, diffusec, (id & CAN_EFF_MASK));
	else
		printf("%ld.%06ld STD 0x%03lX       ", diffsec, diffusec, id);

	if (binary) {

		for (i=0; i<sniftab[slot].current.can_dlc; i++) {
			for (j=7; j>=0; j--) {
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
		for (i=0; i<dlc_diff; i++) {
			printf("        ");
			if (binary_gap)
				putchar(' ');
		}
	}
	else {

		for (i=0; i<sniftab[slot].current.can_dlc; i++)
			if ((color) && (sniftab[slot].marker.data[i]) && (!(sniftab[slot].notch.data[i])))
				printf("%s%02X%s ", ATTCOLOR, sniftab[slot].current.data[i], ATTRESET);
			else
				printf("%02X ", sniftab[slot].current.data[i]);

		if (sniftab[slot].current.can_dlc < 8)
			printf("%*s", (8 - sniftab[slot].current.can_dlc) * 3, "");

		for (i=0; i<sniftab[slot].current.can_dlc; i++)
			if ((sniftab[slot].current.data[i] > 0x1F) && 
			    (sniftab[slot].current.data[i] < 0x7F))
				if ((color) && (sniftab[slot].marker.data[i]) && (!(sniftab[slot].notch.data[i])))
					printf("%s%c%s", ATTCOLOR, sniftab[slot].current.data[i], ATTRESET);
				else
					putchar(sniftab[slot].current.data[i]);
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

	memset(&sniftab[slot].marker.data, 0, 8);
};

void writesettings(char* name) {

	int fd;
	char fname[30] = SETFNAME;
	int i,j;
	char buf[8]= {0};

	strncat(fname, name, 29 - strlen(fname)); 
	fd = open(fname,  O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    
	if (fd > 0) {
		for (i=0; i < idx; i++) {
			sprintf(buf, "<%08X>%c.", sniftab[i].current.can_id, (is_set(i, ENABLE))?'1':'0');
			write(fd, buf, 12);
			for (j=0; j<8 ; j++) {
				sprintf(buf, "%02X", sniftab[i].notch.data[j]);
				write(fd, buf, 2);
			}
			write(fd, "\n", 1);
			/* 12 + 16 + 1 = 29 bytes per entry */ 
		}
		close(fd);
	}
	else
		printf("unable to write setting file '%s'!\n", fname);
};

void readsettings(char* name, int sockfd) {

	int fd;
	char fname[30] = SETFNAME;
	char buf[25] = {0};
	int pos,j;
	bool done = false;

	strncat(fname, name, 29 - strlen(fname)); 
	fd = open(fname, O_RDONLY);
    
	if (fd > 0) {
		if (!sockfd)
			printf("reading setting file '%s' ... ", fname);

		while (!done) {
			if (read(fd, &buf, 29) == 29) {
				unsigned long id = strtoul(&buf[1], (char **)NULL, 16);
				pos = sniftab_index(id);

				if (pos < 0) {
					if (idx < MAX_SLOTS) {
						pos = idx++;
						sniftab[pos].current.can_id = id;
					}
					else
						break;
				}
				if (buf[10] & 1) {
					if (is_clr(pos, ENABLE)) {
						do_set(pos, ENABLE);
					}
				}
				else
					if (is_set(pos, ENABLE)) {
						do_clr(pos, ENABLE);
					}
				for (j=7; j>=0 ; j--) {
					sniftab[pos].notch.data[j] =
						(__u8) strtoul(&buf[2*j+12], (char **)NULL, 16) & 0xFF;
					buf[2*j+12] = 0; /* cut off each time */
				}
			}
			else {
				if (!sockfd)
					printf("was only able to read unti from setting file '%s'!\n", fname);
				
				done = true;
			}
		}
    
		if (!sockfd)
			printf("done\n");

		close(fd);
	}
	else
		printf("unable to read setting file '%s'!\n", fname);
};

static int sniftab_index(canid_t id) {

	int i;

	for (i = 0; i <= idx; i++)
		if (id == sniftab[i].current.can_id)
			return i;

	return -1; /* No match */
}
