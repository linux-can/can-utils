/*
 * a real quick'n'dirty hack to add/remove vcan interfaces.
 * (also to have something to test the new RTNL API in vcan.)
 * this will be added to ip(8) of the iproute package, making
 * this hack obsolete.
 * 
 * we don't check the return value of sendto() and don't wait for
 * a reply using recvmsg().  We just hope everything works fine,
 * otherwise use strace, or feel free to add the code before this
 * whole thing is dumped to the bit bucket.
 *
 * Parts of this code were taken from the iproute source.
 *
 * urs
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <net/if.h>

#include <asm/types.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <linux/if_link.h>

#ifndef IFLA_LINKINFO
#error Your kernel includes do not provide the needed netlink interface function.
#error This is a normal behaviour for Kernel versions below v2.6.24 .
#error You do not need this tool for Kernel versions below v2.6.24 anyway as
#error the number of vcan driver instances can be defined as a vcan.ko module
#error commandline parameter (default = 4) in older Kernels.
#endif

#define NLMSG_TAIL(nmsg)						\
        ((struct rtattr *)(((void *) (nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

int addattr_l(struct nlmsghdr *n, int maxlen, int type, const void *data,
	      int alen);

void usage()
{
	fprintf(stderr, "Usage: vcan create\n"
		"       vcan delete iface\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int s;
	char *cmd, *dev;
	struct {
		struct nlmsghdr  n;
		struct ifinfomsg i;
		char             buf[1024];
	} req;
	struct sockaddr_nl nladdr;
	struct rtattr *linkinfo;

#ifdef OBSOLETE
	fprintf(stderr, "This program is a temporary hack and is now obsolete.\n"
		"Please use ip(8) instead, i.e.\n"
		"    ip link add type vcan       or\n"
		"    ip link delete iface\n");
	exit(1);
#endif
	if (argc < 2)
		usage();
	cmd = argv[1];

	s = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE);

	memset(&req, 0, sizeof(req));

	if (strcmp(cmd, "create") == 0) {
		req.n.nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));
		req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL;
		req.n.nlmsg_type  = RTM_NEWLINK;
		req.n.nlmsg_seq   = 0;
		req.i.ifi_family  = AF_UNSPEC;

		linkinfo = NLMSG_TAIL(&req.n);
		addattr_l(&req.n, sizeof(req), IFLA_LINKINFO, NULL, 0);
		addattr_l(&req.n, sizeof(req), IFLA_INFO_KIND, "vcan", strlen("vcan"));
		linkinfo->rta_len = (void*)NLMSG_TAIL(&req.n) - (void*)linkinfo;

	} else if (strcmp(cmd, "delete") == 0) {
		if (argc < 3)
			usage();
		dev = argv[2];
		req.n.nlmsg_len   = NLMSG_LENGTH(sizeof(struct ifinfomsg));
		req.n.nlmsg_flags = NLM_F_REQUEST;
		req.n.nlmsg_type  = RTM_DELLINK;
		req.i.ifi_family  = AF_UNSPEC;
		req.i.ifi_index   = if_nametoindex(dev);
	} else
		usage();

	memset(&nladdr, 0, sizeof(nladdr));
	nladdr.nl_family = AF_NETLINK;
	nladdr.nl_pid    = 0;
	nladdr.nl_groups = 0;
#if 1
	sendto(s, &req, req.n.nlmsg_len, 0,
	       (struct sockaddr*)&nladdr, sizeof(nladdr));
#else
	{
		int i;

		for (i = 0; i < req.n.nlmsg_len; i++) {
			printf(" %02x", ((unsigned char*)&req)[i]);
			if (i % 16 == 15)
				putchar('\n');
		}
		putchar('\n');
	}
#endif
	close(s);

	return 0;
}

int addattr_l(struct nlmsghdr *n, int maxlen, int type, const void *data,
	      int alen)
{
	int len = RTA_LENGTH(alen);
	struct rtattr *rta;

	if (NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len) > maxlen) {
		fprintf(stderr, "addattr_l ERROR: message exceeded bound of %d\n",
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
