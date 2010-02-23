/*
 *  $Id$
 */

/*
 * cangw.c - manage PF_CAN netlink gateway
 *
 * Copyright (c) 2010 Volkswagen Group Electronic Research
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
 * Send feedback to <socketcan-users@lists.berlios.de>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/can/gw.h>

enum {
	UNSPEC,
	ADD,
	DEL,
	FLUSH,
	LIST
};

struct modattr {
	struct can_frame cf;
	__u8 modtype;
	__u8 instruction;
} __attribute__((packed));


/* some netlink helpers stolen from iproute2 package */
#define NLMSG_TAIL(nmsg) \
        ((struct rtattr *)(((void *) (nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

int addattr_l(struct nlmsghdr *n, int maxlen, int type, const void *data,
	      int alen)
{
	int len = RTA_LENGTH(alen);
	struct rtattr *rta;

	if (NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len) > maxlen) {
		fprintf(stderr, "addattr_l: message exceeded bound of %d\n",
			maxlen);
		return -1;
	}
	rta = NLMSG_TAIL(n);
	rta->rta_type = type;
	rta->rta_len = len;
	memcpy(RTA_DATA(rta), data, alen);
	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);
	return 0;
}

void printfilter(const void *data)
{
	struct can_filter *filter = (struct can_filter *)data;

	printf("-f %03X:%X ", filter->can_id, filter->can_mask);
}

void printmod(const char *type, const void *data)
{
	struct modattr mod;
	int i;

	memcpy (&mod, data, CGW_MODATTR_LEN);

	printf("-m %s:", type);

	if (mod.modtype & CGW_MOD_ID)
		printf("I");

	if (mod.modtype & CGW_MOD_DLC)
		printf("L");

	if (mod.modtype & CGW_MOD_DATA)
		printf("D");

	printf(":%03X.%X.", mod.cf.can_id, mod.cf.can_dlc);

	for (i = 0; i < 8; i++)
		printf("%02X", mod.cf.data[i]);

	printf(" ");
}


void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s [options]\n\n", prg);
	fprintf(stderr, "Commands:  -A (add a new rule)\n");
	fprintf(stderr, "           -D (delete a rule)\n");
	fprintf(stderr, "           -F (flush - delete all rules)\n");
	fprintf(stderr, "           -L (list all rules)\n");
	fprintf(stderr, "Mandatory: -s <src_dev>  (source netdevice)\n");
	fprintf(stderr, "           -d <dst_dev>  (destination netdevice)\n");
	fprintf(stderr, "Options:   -t (preserve src_dev rx timestamp)\n");
	fprintf(stderr, "           -e (echo sent frames - recommended on vcanx)\n");
	fprintf(stderr, "           -f <filter> (set CAN filter)\n");
	fprintf(stderr, "           -m <mod> (set frame modifications)\n");
	fprintf(stderr, "\nValues are given and expected in hexadecimal values. Leading 0s can be omitted.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "<filter> is a <value>:<mask> CAN identifier filter\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "<mod> is a CAN frame modification instruction consisting of\n");
	fprintf(stderr, "<instruction>:<can_frame-elements>:<can_id>.<can_dlc>.<can_data>\n");
	fprintf(stderr, " - <instruction> is one of 'AND' 'OR' 'XOR' 'SET'\n");
	fprintf(stderr, " - <can_frame-elements> is _one_ or _more_ of 'I'dentifier 'L'ength 'D'ata\n");
	fprintf(stderr, " - <can_id> is an u32 value containing the CAN Identifier\n");
	fprintf(stderr, " - <can_dlc> is an u8 value containing the data length code (0 .. 8)\n");
	fprintf(stderr, " - <can_data> is always eight(!) u8 values containing the CAN frames data\n");
	fprintf(stderr, "The instructions are performed in the order 'AND' -> 'OR' -> 'XOR' -> 'SET'\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Example:\n");
	fprintf(stderr, "%s -A -s can0 -d vcan3 -f 123:C00007FF -m SET:IL:80000333.4.1122334455667788\n", prg);
	fprintf(stderr, "\n");
}

int parse_mod(char *optarg, struct modattr *modmsg)
{
	char *ptr, *nptr;
	int i;

	char hexdata[17] = {0};

	ptr = optarg;
	nptr = strchr(ptr, ':');

	if ((nptr - ptr > 3) || (nptr - ptr == 0))
		return 1;

	if (!strncmp(ptr, "AND", 3))
		modmsg->instruction = CGW_MOD_AND;
	else if (!strncmp(ptr, "OR", 2))
		modmsg->instruction = CGW_MOD_OR;
	else if (!strncmp(ptr, "XOR", 3))
		modmsg->instruction = CGW_MOD_XOR;
	else if (!strncmp(ptr, "SET", 3))
		modmsg->instruction = CGW_MOD_SET;
	else
		return 2;

	ptr = nptr+1;
	nptr = strchr(ptr, ':');

	if ((nptr - ptr > 3) || (nptr - ptr == 0))
		return 3;

	modmsg->modtype = 0;

	while (*ptr != ':') {

		switch (*ptr) {

		case 'I':
			modmsg->modtype |= CGW_MOD_ID;
			break;

		case 'L':
			modmsg->modtype |= CGW_MOD_DLC;
			break;

		case 'D':
			modmsg->modtype |= CGW_MOD_DATA;
			break;

		default:
			return 4;
		}
		ptr++;
	}

	if ((sscanf(++ptr, "%lx.%hhd.%16s",
		    (long unsigned int *)&modmsg->cf.can_id,
		    (unsigned char *)&modmsg->cf.can_dlc, hexdata) != 3) ||
	    (modmsg->cf.can_dlc > 8))
		return 5;

	if (strlen(hexdata) != 16)
		return 6;

	for (i = 0; i < 8; i++) {
		if (!sscanf(&hexdata[i*2], "%2hhx", &modmsg->cf.data[i]))
			return 7;	
	}

	return 0; /* ok */
}

int main(int argc, char **argv)
{
	int s;
	int err = 0;

	int opt;
	extern int optind, opterr, optopt;

	int cmd = UNSPEC;
	int have_filter = 0;

	struct {
		struct nlmsghdr nh;
		struct rtcanmsg rtcan;
		char buf[200];

	} req;

	char rxbuf[8192]; /* netlink receive buffer */
	char ifname[IF_NAMESIZE]; /* internface name for if_indextoname() */
	struct nlmsghdr *nlh;
	struct nlmsgerr *rte;
	struct rtcanmsg *rtc;
	struct rtattr *rta;
	__u32 handled = 0;
	__u32 dropped = 0;
	int rtlen;
	int len;

	struct can_filter filter;
	struct sockaddr_nl nladdr;

	struct modattr modmsg[CGW_MOD_FUNCS];
	int modidx = 0;
	int i;

	memset(&req, 0, sizeof(req));

	while ((opt = getopt(argc, argv, "ADFLs:d:tef:m:?")) != -1) {
		switch (opt) {

		case 'A':
			if (cmd == UNSPEC)
				cmd = ADD;
			break;

		case 'D':
			if (cmd == UNSPEC)
				cmd = DEL;
			break;

		case 'F':
			if (cmd == UNSPEC)
				cmd = FLUSH;
			break;

		case 'L':
			if (cmd == UNSPEC)
				cmd = LIST;
			break;

		case 's':
			req.rtcan.src_ifindex = if_nametoindex(optarg);
			break;

		case 'd':
			req.rtcan.dst_ifindex = if_nametoindex(optarg);
			break;

		case 't':
			req.rtcan.can_txflags |= CAN_GW_TXFLAGS_SRC_TSTAMP;
			break;

		case 'e':
			req.rtcan.can_txflags |= CAN_GW_TXFLAGS_ECHO;
			break;

		case 'f':
			if (sscanf(optarg, "%lx:%lx",
				   (long unsigned int *)&filter.can_id, 
				   (long unsigned int *)&filter.can_mask) == 2) {
				have_filter = 1;
			} else {
				printf("Bad filter definition '%s'.\n", optarg);
				exit(1);
			}
			break;

		case 'm':
			/* may be triggered by each of the CGW_MOD_FUNCS functions */
			if ((modidx < CGW_MOD_FUNCS) && (err = parse_mod(optarg, &modmsg[modidx++]))) {
				printf("Problem %d with modification definition '%s'.\n", err, optarg);
				exit(1);
			}
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

	if ((argc - optind != 0) || (cmd == UNSPEC)) {
		print_usage(basename(argv[0]));
		exit(1);
	}

	if ((cmd == ADD || cmd == DEL) &&
	    ((!req.rtcan.src_ifindex) || (!req.rtcan.dst_ifindex))) {
		print_usage(basename(argv[0]));
		exit(1);
	}

	s = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

	switch (cmd) {

	case ADD:
		req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
		req.nh.nlmsg_type  = RTM_NEWROUTE;
		break;

	case DEL:
		req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
		req.nh.nlmsg_type  = RTM_DELROUTE;
		break;

	case FLUSH:
		req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
		req.nh.nlmsg_type  = RTM_DELROUTE;
		/* if_index set to 0 => remove all entries */
		req.rtcan.src_ifindex  = 0;
		req.rtcan.dst_ifindex  = 0;
		break;

	case LIST:
		req.nh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
		req.nh.nlmsg_type  = RTM_GETROUTE;
		break;

	default:
		printf("This function is not yet implemented.\n");
		exit(1);
		break;
	}

	req.nh.nlmsg_len   = NLMSG_LENGTH(sizeof(struct rtcanmsg));
	req.nh.nlmsg_seq   = 0;

	req.rtcan.can_family  = AF_CAN;

	/* add new attributes here */

	if (have_filter)
		addattr_l(&req.nh, sizeof(req), CGW_FILTER, &filter, sizeof(filter));

	/*
	 * a better example code
	 * modmsg.modtype = CGW_MOD_ID;
	 * addattr_l(&req.n, sizeof(req), CGW_MOD_SET, &modmsg, CGW_MODATTR_LEN);
	 */

	/* add up to CGW_MOD_FUNCS modification definitions */
	for (i = 0; i < modidx; i++)
		addattr_l(&req.nh, sizeof(req), modmsg[i].instruction, &modmsg[i], CGW_MODATTR_LEN);

	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;
	nladdr.nl_pid    = 0;
	nladdr.nl_groups = 0;

	err = sendto(s, &req, req.nh.nlmsg_len, 0,
		     (struct sockaddr*)&nladdr, sizeof(nladdr));
	if (err < 0) {
		perror("netlink sendto");
		return err;
	}

	/* clean netlink receive buffer */
	bzero(rxbuf, sizeof(rxbuf));

	if (cmd != LIST) {

		/*
		 * cmd == ADD || cmd == DEL || cmd == FLUSH
		 *
		 * Parse the requested netlink acknowledge return values.
		 */

		err = recv(s, &rxbuf, sizeof(rxbuf), 0);
		if (err < 0) {
			perror("netlink recv");
			return err;
		}
		nlh = (struct nlmsghdr *)rxbuf;
		if (nlh->nlmsg_type != NLMSG_ERROR) {
			fprintf(stderr, "unexpected netlink answer of type %d\n", nlh->nlmsg_type);
			return -EINVAL;
		}
		rte = (struct nlmsgerr *)NLMSG_DATA(nlh);
		err = rte->error;
		if (err < 0)
			fprintf(stderr, "netlink error %d (%s)\n", err, strerror(abs(err)));

	} else {

		/* cmd == LIST */

		while (1) {
			len = recv(s, &rxbuf, sizeof(rxbuf), 0);
			if (len < 0) {
				perror("netlink recv");
				return len;
			}
			nlh = (struct nlmsghdr *)rxbuf;
			if (nlh->nlmsg_type == NLMSG_DONE) 
				break;

			rtc = (struct rtcanmsg *)NLMSG_DATA(nlh);
			if (rtc->can_family != AF_CAN) {
				printf("received msg from unknown family %d\n", rtc->can_family);
				return -EINVAL;
			}

			/*
			 * print list in a representation that
			 * can be used directly for start scripts
			 */

			printf("%s -A ", basename(argv[0]));
			printf("-s %s ", if_indextoname(rtc->src_ifindex, ifname));
			printf("-d %s ", if_indextoname(rtc->dst_ifindex, ifname));

			if (rtc->can_txflags & CAN_GW_TXFLAGS_ECHO)
				printf("-e ");

			if (rtc->can_txflags & CAN_GW_TXFLAGS_SRC_TSTAMP)
				printf("-t ");

			/* check for attributes */
			rta = (struct rtattr *) RTM_RTA(rtc);
			rtlen = RTM_PAYLOAD(nlh);
			for(;RTA_OK(rta, rtlen);rta=RTA_NEXT(rta,rtlen))
			{
				switch(rta->rta_type) {

				case CGW_FILTER:
					printfilter(RTA_DATA(rta));
					break;

				case CGW_MOD_AND:
					printmod("AND", RTA_DATA(rta));
					break;

				case CGW_MOD_OR:
					printmod("OR", RTA_DATA(rta));
					break;

				case CGW_MOD_XOR:
					printmod("XOR", RTA_DATA(rta));
					break;

				case CGW_MOD_SET:
					printmod("SET", RTA_DATA(rta));
					break;

				case CGW_HANDLED:
					handled = *(__u32 *)RTA_DATA(rta);
					break;

				case CGW_DROPPED:
					dropped = *(__u32 *)RTA_DATA(rta);
					break;

				default:
					printf("Unknown attribute %d!", rta->rta_type);
					return -EINVAL;
					break;
				}
			}

			printf("# %d handled %d dropped\n", handled, dropped); /* end of entry */
		}
	}

	close(s);

	return err;
}

