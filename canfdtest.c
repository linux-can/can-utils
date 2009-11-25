/*
 * canfdtest.c - Full-duplex test program (DUT and host part)
 *
 * (C) 2009 by Vladislav Gribov, IXXAT Automation GmbH, <gribov@ixxat.de>
 * (C) 2009 Wolfgang Grandegger <wg@grandegger.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Send feedback to <socketcan-users@lists.berlios.de>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <libgen.h>
#include <getopt.h>
#include <time.h>
#include <sched.h>
#include <limits.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#define CAN_MSG_ID	0x77
#define CAN_MSG_LEN	8
#define CAN_MSG_COUNT	50
#define CAN_MSG_WAIT	27

static int running = 1;
static int verbose;
static int sockfd;
static int test_loops;

static void print_usage(char *prg)
{
	fprintf(stderr,
		"Usage: %s [options] <can-interface>\n"
		"\n"
		"Options: -v       (low verbosity)\n"
		"         -vv      (high verbosity)\n"
		"         -g       (generate messages)\n"
		"         -l COUNT (test loop count)\n"
		"\n"
		"With the option '-g' CAN messages are generated and checked\n"
		"on <can-interface>, otherwise all messages received on the\n"
                "<can-interface> are sent back incrementing the CAN id and\n"
		"all data bytes. The program can be aborted with ^C.\n"
		"\n"
		"Example:\n"
		"\ton DUT : %s -v can0\n"
		"\ton Host: %s -g -v can2\n",
		prg, prg, prg);

	exit(1);
}

static void print_frame(struct can_frame *frame)
{
	int i;

	printf("%04x: ", frame->can_id);
	if (frame->can_id & CAN_RTR_FLAG) {
		printf("remote request");
	} else {
		printf("[%d]", frame->can_dlc);
		for (i = 0; i < frame->can_dlc; i++)
			printf(" %02x", frame->data[i]);
	}
	printf("\n");
}

static void print_compare(struct can_frame *exp, struct can_frame *rec)
{
	printf("expected: ");
	print_frame(exp);
	printf("received: ");
	print_frame(rec);
}

static void compare_frame(struct can_frame *exp, struct can_frame *rec)
{
	int i;

	if (rec->can_id != exp->can_id) {
		printf("Message ID mismatch!\n");
		print_compare(exp, rec);
		running = 0;
	} else if (rec->can_dlc != exp->can_dlc) {
		printf("Message length mismatch!\n");
		print_compare(exp, rec);
		running = 0;
	} else {
		for (i = 0; i < rec->can_dlc; i++) {
			if (rec->data[i] != exp->data[i]) {
				printf("Databyte %x mismatch !\n", i);
				print_compare(exp,
					      rec);
				running = 0;
			}
		}
	}
}

static void millisleep(int msecs)
{
	if (msecs <= 0) {
		sched_yield();
	} else {
		struct timespec rqtp, rmtp;

		/* sleep in ms */
		rqtp.tv_sec = msecs / 1000;
		rqtp.tv_nsec = (msecs % 1000) * 1000000;
		while (nanosleep(&rqtp, &rmtp)) {
			if (errno != EINTR) {
				printf("t\n");
				break;
			}
			rqtp = rmtp;
		}
	}
}

static void echo_progress(unsigned char data)
{
	if (data == 0xff) {
		printf(".");
		fflush(stdout);
	}
}

static void signal_handler(int signo)
{
	close(sockfd);
	running = 0;
}

static int recv_frame(struct can_frame *frame)
{
	int ret;

	ret = recv(sockfd, frame, sizeof(*frame), 0);
	if (ret != sizeof(*frame)) {
		if (ret < 0)
			perror("recv failed");
		else
			fprintf(stderr, "recv returned %d", ret);
		return -1;
	}
	return 0;
}

static int send_frame(struct can_frame *frame)
{
	int ret;

	while ((ret = send(sockfd, frame, sizeof(*frame), 0))
	       != sizeof(*frame)) {
		if (ret < 0) {
			if (errno != ENOBUFS) {
				perror("send failed");
				return -1;
			} else {
				if (verbose) {
					printf("N");
					fflush(stdout);
				}
			}
		} else {
			fprintf(stderr, "send returned %d", ret);
			return -1;
		}
	}
	return 0;
}

static int can_echo_dut(void)
{
	unsigned int frame_count = 0;
	struct timeval tvn, tv_stop;
	struct can_frame frame;
	int i;

	while (running) {
		if (recv_frame(&frame))
			return -1;
		frame_count++;
		if (verbose == 1) {
			echo_progress(frame.data[0]);
		} else if (verbose > 1) {
			printf("%04x: ", frame.can_id);
			if (frame.can_id & CAN_RTR_FLAG) {
				printf("remote request");
			} else {
				printf("[%d]", frame.can_dlc);
				for (i = 0; i < frame.can_dlc; i++)
					printf(" %02x", frame.data[i]);
			}
			printf("\n");
		}
		frame.can_id++;
		for (i = 0; i < frame.can_dlc; i++)
			frame.data[i]++;
		if (send_frame(&frame))
			return -1;

		/*
		 * to force a interlacing of the frames send by DUT and PC
		 * test tool a waiting time is injected
		 */
		if (frame_count == CAN_MSG_WAIT) {
			frame_count = 0;
			if (gettimeofday(&tv_stop, NULL)) {
				perror("gettimeofday failed\n");
				return -1;
			} else {
				tv_stop.tv_usec += 3000;
				if (tv_stop.tv_usec > 999999) {
					tv_stop.tv_sec++;
					tv_stop.tv_usec =
						tv_stop.tv_usec % 1000000;
				}
				gettimeofday(&tvn, NULL);
				while ((tv_stop.tv_sec > tvn.tv_sec) ||
				       ((tv_stop.tv_sec = tvn.tv_sec) &&
					(tv_stop.tv_usec >= tvn.tv_usec)))
					gettimeofday(&tvn, NULL);
			}
		}
	}

	return 0;
}

static int can_echo_gen(void)
{
	struct can_frame tx_frames[CAN_MSG_COUNT];
	struct can_frame rx_frame;
	unsigned char counter = 0;
	int send_pos = 0, recv_pos = 0, unprocessed = 0, loops = 0;
	int i;

	while (running) {
		if (unprocessed < CAN_MSG_COUNT) {
			/* still send messages */
			tx_frames[send_pos].can_dlc = CAN_MSG_LEN;
			tx_frames[send_pos].can_id = CAN_MSG_ID;
			for (i = 0; i < CAN_MSG_LEN; i++)
				tx_frames[send_pos].data[i] = counter + i;
			if (send_frame(&tx_frames[send_pos]))
				return -1;

			/* increment to be equal to expected */
			tx_frames[send_pos].can_id++;
			for (i = 0; i < CAN_MSG_LEN; i++)
				tx_frames[send_pos].data[i]++;

			send_pos++;
			if (send_pos == CAN_MSG_COUNT)
				send_pos = 0;
			unprocessed++;
			if (verbose == 1)
				echo_progress(counter);
			counter++;

			if ((counter % 33) == 0)
				millisleep(3);
			else
				millisleep(1);
		} else {
			if (recv_frame(&rx_frame))
				return -1;

			if (verbose > 1)
				print_frame(&rx_frame);

			/* compare with expected */
			compare_frame(&tx_frames[recv_pos], &rx_frame);

			loops++;
			if (test_loops && loops >= test_loops)
				break;

			recv_pos++;
			if (recv_pos == CAN_MSG_COUNT)
				recv_pos = 0;
			unprocessed--;
		}
	}

	printf("\nTest messages sent and received: %d\n", loops);

	return 0;
}

int main(int argc, char *argv[])
{
	struct ifreq ifr;
	struct sockaddr_can addr;
	char *intf_name;
	int family = PF_CAN, type = SOCK_RAW, proto = CAN_RAW;
	int echo_gen = 0;
	int opt, err;

	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

	while ((opt = getopt(argc, argv, "gl:v")) != -1) {
		switch (opt) {
		case 'v':
			verbose++;
			break;

		case 'l':
			test_loops = atoi(optarg);;
			break;

		case 'g':
			echo_gen = 1;
			break;

		default:
			print_usage(basename(argv[0]));
			break;
		}
	}

	if ((argc - optind) != 1)
		print_usage(basename(argv[0]));
	intf_name = argv[optind];

	printf("interface = %s, family = %d, type = %d, proto = %d\n",
	       intf_name, family, type, proto);

	if ((sockfd = socket(family, type, proto)) < 0) {
		perror("socket");
		return 1;
	}

	addr.can_family = family;
	strcpy(ifr.ifr_name, intf_name);
	ioctl(sockfd, SIOCGIFINDEX, &ifr);
	addr.can_ifindex = ifr.ifr_ifindex;

	if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(sockfd);
		return 1;
	}

	if (echo_gen)
		err = can_echo_gen();
	else
		err = can_echo_dut();

	if (verbose)
		printf("Exiting...\n");

	close(sockfd);
	return err;
}
