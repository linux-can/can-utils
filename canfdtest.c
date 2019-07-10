/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * canfdtest.c - Full-duplex test program (DUT and host part)
 *
 * (C) 2009 by Vladislav Gribov, IXXAT Automation GmbH, <gribov@ixxat.de>
 * (C) 2009 Wolfgang Grandegger <wg@grandegger.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Send feedback to <linux-can@vger.kernel.org>
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
static int exit_sig;

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

static void print_frame(struct can_frame *frame, int inc)
{
	int i;

	printf("%04x: ", frame->can_id + inc);
	if (frame->can_id & CAN_RTR_FLAG) {
		printf("remote request");
	} else {
		printf("[%d]", frame->can_dlc);
		for (i = 0; i < frame->can_dlc; i++)
			printf(" %02x", frame->data[i] + inc);
	}
	printf("\n");
}

static void print_compare(struct can_frame *exp, struct can_frame *rec, int inc)
{
	printf("expected: ");
	print_frame(exp, inc);
	printf("received: ");
	print_frame(rec, 0);
}

static void compare_frame(struct can_frame *exp, struct can_frame *rec, int inc)
{
	int i;

	if (rec->can_id != exp->can_id + inc) {
		printf("Message ID mismatch!\n");
		print_compare(exp, rec, inc);
		running = 0;
	} else if (rec->can_dlc != exp->can_dlc) {
		printf("Message length mismatch!\n");
		print_compare(exp, rec, inc);
		running = 0;
	} else {
		for (i = 0; i < rec->can_dlc; i++) {
			if (rec->data[i] != ((exp->data[i] + inc) & 0xff)) {
				printf("Databyte %x mismatch !\n", i);
				print_compare(exp,
					      rec, inc);
				running = 0;
			}
		}
	}
}

static void millisleep(int msecs)
{
	struct timespec rqtp, rmtp;
	int err;

	/* sleep in ms */
	rqtp.tv_sec = msecs / 1000;
	rqtp.tv_nsec = msecs % 1000 * 1000000;

	do {
		err = clock_nanosleep(CLOCK_MONOTONIC, 0, &rqtp, &rmtp);
		if (err != 0 && err != EINTR) {
			printf("t\n");
			break;
		}
		rqtp = rmtp;
	} while (err != 0);
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
	exit_sig = signo;
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
			millisleep(3);
		}
	}

	return 0;
}

static int can_echo_gen(void)
{
	struct can_frame tx_frames[CAN_MSG_COUNT];
	int recv_tx[CAN_MSG_COUNT];
	struct can_frame rx_frame;
	unsigned char counter = 0;
	int send_pos = 0, recv_rx_pos = 0, recv_tx_pos = 0, unprocessed = 0, loops = 0;
	int i;

	while (running) {
		if (unprocessed < CAN_MSG_COUNT) {
			/* still send messages */
			tx_frames[send_pos].can_dlc = CAN_MSG_LEN;
			tx_frames[send_pos].can_id = CAN_MSG_ID;
			recv_tx[send_pos] = 0;

			for (i = 0; i < CAN_MSG_LEN; i++)
				tx_frames[send_pos].data[i] = counter + i;
			if (send_frame(&tx_frames[send_pos]))
				return -1;

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
				print_frame(&rx_frame, 0);

			/* own frame */
			if (rx_frame.can_id == CAN_MSG_ID) {
				compare_frame(&tx_frames[recv_tx_pos], &rx_frame, 0);
				recv_tx[recv_tx_pos] = 1;
				recv_tx_pos++;
				if (recv_tx_pos == CAN_MSG_COUNT)
					recv_tx_pos = 0;
				continue;
			} else {
				if (!recv_tx[recv_rx_pos]) {
					printf("RX before TX!\n");
					running = 0;
				}
				/* compare with expected */
				compare_frame(&tx_frames[recv_rx_pos], &rx_frame, 1);
				recv_rx_pos++;
				if (recv_rx_pos == CAN_MSG_COUNT)
					recv_rx_pos = 0;
			}

			loops++;
			if (test_loops && loops >= test_loops)
				break;

			unprocessed--;
		}
	}

	printf("\nTest messages sent and received: %d\n", loops);

	return 0;
}

int main(int argc, char *argv[])
{
	struct sockaddr_can addr;
	char *intf_name;
	int family = PF_CAN, type = SOCK_RAW, proto = CAN_RAW;
	int echo_gen = 0;
	int opt, err;
	int recv_own_msgs = 1;

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

	if (echo_gen) {
		if (setsockopt(sockfd, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS,
			   &recv_own_msgs, sizeof(recv_own_msgs)) == -1) {
			perror("setsockopt");
			return 1;
		}
	}

	addr.can_family = family;
	addr.can_ifindex = if_nametoindex(intf_name);

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

	if (exit_sig) {
		signal(exit_sig, SIG_DFL);
		kill(getpid(), exit_sig);
	}

	return err;
}
