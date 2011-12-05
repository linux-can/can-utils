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
 * Send feedback to <linux-can@vger.kernel.org>
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


#define RTCAN_RTA(r)  ((struct rtattr*)(((char*)(r)) + NLMSG_ALIGN(sizeof(struct rtcanmsg))))
#define RTCAN_PAYLOAD(n) NLMSG_PAYLOAD(n,sizeof(struct rtcanmsg))

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

void print_cs_xor(struct cgw_csum_xor *cs_xor)
{
	printf("-x %d:%d:%d:%02X ",
	       cs_xor->from_idx, cs_xor->to_idx,
	       cs_xor->result_idx, cs_xor->init_xor_val);
}

void print_cs_crc8_profile(struct cgw_csum_crc8 *cs_crc8)
{
	int i;

	printf("-p %d:", cs_crc8->profile);

	switch (cs_crc8->profile) {

	case  CGW_CRC8PRF_1U8:

		printf("%02X", cs_crc8->profile_data[0]);
		break;

	case  CGW_CRC8PRF_16U8:

		for (i = 0; i < 16; i++)
			printf("%02X", cs_crc8->profile_data[i]);
		break;

	case  CGW_CRC8PRF_SFFID_XOR:
		break;

	default:
		printf("<unknown profile #%d>", cs_crc8->profile);
	}

	printf(" ");
}

void print_cs_crc8(struct cgw_csum_crc8 *cs_crc8)
{
	int i;

	printf("-c %d:%d:%d:%02X:%02X:",
	       cs_crc8->from_idx, cs_crc8->to_idx,
	       cs_crc8->result_idx, cs_crc8->init_crc_val,
	       cs_crc8->final_xor_val);

	for (i = 0; i < 256; i++)
		printf("%02X", cs_crc8->crctab[i]);

	printf(" ");

	if (cs_crc8->profile != CGW_CRC8PRF_UNSPEC)
		print_cs_crc8_profile(cs_crc8);
}

void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s [options]\n\n", prg);
	fprintf(stderr, "Commands:  -A (add a new rule)\n");
	fprintf(stderr, "           -D (delete a rule)\n");
	fprintf(stderr, "           -F (flush / delete all rules)\n");
	fprintf(stderr, "           -L (list all rules)\n");
	fprintf(stderr, "Mandatory: -s <src_dev>  (source netdevice)\n");
	fprintf(stderr, "           -d <dst_dev>  (destination netdevice)\n");
	fprintf(stderr, "Options:   -t (preserve src_dev rx timestamp)\n");
	fprintf(stderr, "           -e (echo sent frames - recommended on vcanx)\n");
	fprintf(stderr, "           -f <filter> (set CAN filter)\n");
	fprintf(stderr, "           -m <mod> (set frame modifications)\n");
	fprintf(stderr, "           -x <from_idx>:<to_idx>:<result_idx>:<init_xor_val> (XOR checksum)\n");
	fprintf(stderr, "           -c <from>:<to>:<result>:<init_val>:<xor_val>:<crctab[256]> (CRC8 cs)\n");
	fprintf(stderr, "           -p <profile>:[<profile_data>] (CRC8 checksum profile & parameters)\n");
	fprintf(stderr, "\nValues are given and expected in hexadecimal values. Leading 0s can be omitted.\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "<filter> is a <value><mask> CAN identifier filter\n");
	fprintf(stderr, "   <can_id>:<can_mask> (matches when <received_can_id> & mask == can_id & mask)\n");
	fprintf(stderr, "   <can_id>~<can_mask> (matches when <received_can_id> & mask != can_id & mask)\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "<mod> is a CAN frame modification instruction consisting of\n");
	fprintf(stderr, "<instruction>:<can_frame-elements>:<can_id>.<can_dlc>.<can_data>\n");
	fprintf(stderr, " - <instruction> is one of 'AND' 'OR' 'XOR' 'SET'\n");
	fprintf(stderr, " - <can_frame-elements> is _one_ or _more_ of 'I'dentifier 'L'ength 'D'ata\n");
	fprintf(stderr, " - <can_id> is an u32 value containing the CAN Identifier\n");
	fprintf(stderr, " - <can_dlc> is an u8 value containing the data length code (0 .. 8)\n");
	fprintf(stderr, " - <can_data> is always eight(!) u8 values containing the CAN frames data\n");
	fprintf(stderr, "The max. four modifications are performed in the order AND -> OR -> XOR -> SET\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Example:\n");
	fprintf(stderr, "%s -A -s can0 -d vcan3 -e -f 123:C00007FF -m SET:IL:333.4.1122334455667788\n", prg);
	fprintf(stderr, "\n");
	fprintf(stderr, "Supported CRC 8 profiles:\n");
	fprintf(stderr, "Profile '%d' (1U8)       - add one additional u8 value\n", CGW_CRC8PRF_1U8);
	fprintf(stderr, "Profile '%d' (16U8)      - add u8 value from table[16] indexed by (data[1] & 0xF)\n", CGW_CRC8PRF_16U8);
	fprintf(stderr, "Profile '%d' (SFFID_XOR) - add u8 value (can_id & 0xFF) ^ (can_id >> 8 & 0xFF)\n", CGW_CRC8PRF_SFFID_XOR);
	fprintf(stderr, "\n");
}

int b64hex(char *asc, unsigned char *bin, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		if (!sscanf(asc+(i*2), "%2hhx", bin+i))
			return 1;	
	}
	return 0;
}

int parse_crc8_profile(char *optarg, struct cgw_csum_crc8 *crc8)
{
	int ret = 1;
	char *ptr;

	if (sscanf(optarg, "%hhd:", &crc8->profile) != 1)
		return ret;

	switch (crc8->profile) {

	case  CGW_CRC8PRF_1U8:

		if (sscanf(optarg, "%hhd:%2hhx", &crc8->profile, &crc8->profile_data[0]) == 2)
			ret = 0;

		break;

	case  CGW_CRC8PRF_16U8:

		ptr = strchr(optarg, ':');

		/* check if length contains 16 base64 hex values */
		if (ptr != NULL &&
		    strlen(ptr) == strlen(":00112233445566778899AABBCCDDEEFF") &&
		    b64hex(ptr+1, (unsigned char *)&crc8->profile_data[0], 16) == 0)
			ret = 0;

		break;

	case  CGW_CRC8PRF_SFFID_XOR:

		/* no additional parameters needed */
		ret = 0;
		break;
	}

	return ret;
}

int parse_mod(char *optarg, struct modattr *modmsg)
{
	char *ptr, *nptr;
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

	if (sscanf(++ptr, "%x.%hhx.%16s", &modmsg->cf.can_id,
		   (unsigned char *)&modmsg->cf.can_dlc, hexdata) != 3)
		return 5;

	/* 4-bit masks can have values from 0 to 0xF */ 
	if (modmsg->cf.can_dlc > 0xF)
		return 6;

	/* but when setting CAN_DLC the value has to be limited to 8 */
	if (modmsg->instruction == CGW_MOD_SET && modmsg->cf.can_dlc > 8)
		return 7;

	if (strlen(hexdata) != 16)
		return 8;

	if (b64hex(hexdata, &modmsg->cf.data[0], 8))
		return 9;

	return 0; /* ok */
}

int parse_rtlist(char *prgname, unsigned char *rxbuf, int len)
{
	char ifname[IF_NAMESIZE]; /* internface name for if_indextoname() */
	struct rtcanmsg *rtc;
	struct rtattr *rta;
	struct nlmsghdr *nlh;
	unsigned int src_ifindex = 0;
	unsigned int dst_ifindex = 0;
	__u32 handled, dropped;
	int rtlen;


	nlh = (struct nlmsghdr *)rxbuf;

	while (1) {
		if (!NLMSG_OK(nlh, len))
			return 0;

		if (nlh->nlmsg_type == NLMSG_ERROR) {
			printf("NLMSG_ERROR\n");
			return 1;
		}

		if (nlh->nlmsg_type == NLMSG_DONE) {
			//printf("NLMSG_DONE\n");
			return 1;
		}

		rtc = (struct rtcanmsg *)NLMSG_DATA(nlh);
		if (rtc->can_family != AF_CAN) {
			printf("received msg from unknown family %d\n", rtc->can_family);
			return -EINVAL;
		}

		if (rtc->gwtype != CGW_TYPE_CAN_CAN) {
			printf("received msg with unknown gwtype %d\n", rtc->gwtype);
			return -EINVAL;
		}

		/*
		 * print list in a representation that
		 * can be used directly for start scripts.
		 *
		 * To order the mandatory and optional parameters in the
		 * output string, the NLMSG is parsed twice.
		 */

		handled = 0;
		dropped = 0;
		src_ifindex = 0;
		dst_ifindex = 0;

		printf("%s -A ", basename(prgname));

		/* first parse for mandatory options */
		rta = (struct rtattr *) RTCAN_RTA(rtc);
		rtlen = RTCAN_PAYLOAD(nlh);
		for(;RTA_OK(rta, rtlen);rta=RTA_NEXT(rta,rtlen))
		{
			//printf("(A-%d)", rta->rta_type);
			switch(rta->rta_type) {

			case CGW_FILTER:
			case CGW_MOD_AND:
			case CGW_MOD_OR:
			case CGW_MOD_XOR:
			case CGW_MOD_SET:
			case CGW_CS_XOR:
			case CGW_CS_CRC8:
				break;

			case CGW_SRC_IF:
				src_ifindex = *(__u32 *)RTA_DATA(rta);
				break;

			case CGW_DST_IF:
				dst_ifindex = *(__u32 *)RTA_DATA(rta);
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


		printf("-s %s ", if_indextoname(src_ifindex, ifname));
		printf("-d %s ", if_indextoname(dst_ifindex, ifname));

		if (rtc->flags & CGW_FLAGS_CAN_ECHO)
			printf("-e ");

		if (rtc->flags & CGW_FLAGS_CAN_SRC_TSTAMP)
			printf("-t ");

		/* second parse for mod attributes */
		rta = (struct rtattr *) RTCAN_RTA(rtc);
		rtlen = RTCAN_PAYLOAD(nlh);
		for(;RTA_OK(rta, rtlen);rta=RTA_NEXT(rta,rtlen))
		{
			//printf("(B-%d)", rta->rta_type);
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

			case CGW_CS_XOR:
				print_cs_xor((struct cgw_csum_xor *)RTA_DATA(rta));
				break;

			case CGW_CS_CRC8:
				print_cs_crc8((struct cgw_csum_crc8 *)RTA_DATA(rta));
				break;

			case CGW_SRC_IF:
			case CGW_DST_IF:
			case CGW_HANDLED:
			case CGW_DROPPED:
				break;

			default:
				printf("Unknown attribute %d!", rta->rta_type);
				return -EINVAL;
				break;
			}
		}

		printf("# %d handled %d dropped\n", handled, dropped); /* end of entry */

		/* jump to next NLMSG in the given buffer */
		nlh = NLMSG_NEXT(nlh, len);
	}
}

int main(int argc, char **argv)
{
	int s;
	int err = 0;

	int opt;
	extern int optind, opterr, optopt;

	int cmd = UNSPEC;
	int have_filter = 0;
	int have_cs_xor = 0;
	int have_cs_crc8 = 0;

	struct {
		struct nlmsghdr nh;
		struct rtcanmsg rtcan;
		char buf[600];

	} req;

	unsigned char rxbuf[8192]; /* netlink receive buffer */
	struct nlmsghdr *nlh;
	struct nlmsgerr *rte;
	unsigned int src_ifindex = 0;
	unsigned int dst_ifindex = 0;
	__u16 flags = 0;
	int len;

	struct can_filter filter;
	struct sockaddr_nl nladdr;

	struct cgw_csum_xor cs_xor;
	struct cgw_csum_crc8 cs_crc8;
	char crc8tab[513] = {0};

	struct modattr modmsg[CGW_MOD_FUNCS];
	int modidx = 0;
	int i;

	memset(&req, 0, sizeof(req));
	memset(&cs_xor, 0, sizeof(cs_xor));
	memset(&cs_crc8, 0, sizeof(cs_crc8));

	while ((opt = getopt(argc, argv, "ADFLs:d:tef:c:p:x:m:?")) != -1) {
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
			src_ifindex = if_nametoindex(optarg);
			break;

		case 'd':
			dst_ifindex = if_nametoindex(optarg);
			break;

		case 't':
			flags |= CGW_FLAGS_CAN_SRC_TSTAMP;
			break;

		case 'e':
			flags |= CGW_FLAGS_CAN_ECHO;
			break;

		case 'f':
			if (sscanf(optarg, "%x:%x", &filter.can_id,
				   &filter.can_mask) == 2) {
				have_filter = 1;
			} else if (sscanf(optarg, "%x~%x", &filter.can_id,
					  &filter.can_mask) == 2) {
				filter.can_id |= CAN_INV_FILTER;
				have_filter = 1;
			} else {
				printf("Bad filter definition '%s'.\n", optarg);
				exit(1);
			}
			break;

		case 'x':
			if (sscanf(optarg, "%hhd:%hhd:%hhd:%hhx",
				   &cs_xor.from_idx, &cs_xor.to_idx,
				   &cs_xor.result_idx, &cs_xor.init_xor_val) == 4) {
				have_cs_xor = 1;
			} else {
				printf("Bad XOR checksum definition '%s'.\n", optarg);
				exit(1);
			}
			break;

		case 'c':
			if ((sscanf(optarg, "%hhd:%hhd:%hhd:%hhx:%hhx:%512s",
				    &cs_crc8.from_idx, &cs_crc8.to_idx,
				    &cs_crc8.result_idx, &cs_crc8.init_crc_val,
				    &cs_crc8.final_xor_val, crc8tab) == 6) &&
			    (strlen(crc8tab) == 512) &&
			    (b64hex(crc8tab, (unsigned char *)&cs_crc8.crctab, 256) == 0)) {
				have_cs_crc8 = 1;
			} else {
				printf("Bad CRC8 checksum definition '%s'.\n", optarg);
				exit(1);
			}
			break;

		case 'p':
			if (parse_crc8_profile(optarg, &cs_crc8)) {
				printf("Bad CRC8 profile definition '%s'.\n", optarg);
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
	    ((!src_ifindex) || (!dst_ifindex))) {
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
		src_ifindex  = 0;
		dst_ifindex  = 0;
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
	req.rtcan.gwtype = CGW_TYPE_CAN_CAN;
	req.rtcan.flags = flags;

	addattr_l(&req.nh, sizeof(req), CGW_SRC_IF, &src_ifindex, sizeof(src_ifindex));
	addattr_l(&req.nh, sizeof(req), CGW_DST_IF, &dst_ifindex, sizeof(dst_ifindex));

	/* add new attributes here */

	if (have_filter)
		addattr_l(&req.nh, sizeof(req), CGW_FILTER, &filter, sizeof(filter));

	if (have_cs_crc8)
		addattr_l(&req.nh, sizeof(req), CGW_CS_CRC8, &cs_crc8, sizeof(cs_crc8));

	if (have_cs_xor)
		addattr_l(&req.nh, sizeof(req), CGW_CS_XOR, &cs_xor, sizeof(cs_xor));

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
	memset(rxbuf, 0x0, sizeof(rxbuf));

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
#if 0
			printf("received msg len %d\n", len);

			for (i = 0; i < len; i++)
				printf("%02X ", rxbuf[i]);

			printf("\n");
#endif
			/* leave on errors or NLMSG_DONE */
			if (parse_rtlist(argv[0], rxbuf, len))
				break;
		}
	}

	close(s);

	return err;
}

