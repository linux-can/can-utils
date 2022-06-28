/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * canfdtest.c - Full-duplex test program (DUT and host part)
 *
 * (C) 2009 by Vladislav Gribov, IXXAT Automation GmbH, <gribov@ixxat.de>
 * (C) 2009 Wolfgang Grandegger <wg@grandegger.com>
 * (C) 2021 Jean Gressmann, IAV GmbH, <jean.steven.gressmann@iav.de>
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

#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#define CAN_MSG_ID_PING	0x77
#define CAN_MSG_ID_PONG	0x78
#define CAN_MSG_LEN	8
#define CAN_MSG_COUNT	50
#define CAN_MSG_WAIT	27

static int running = 1;
static int verbose;
static int sockfd;
static int test_loops;
static int exit_sig;
static int inflight_count = CAN_MSG_COUNT;
static canid_t can_id_ping = CAN_MSG_ID_PING;
static canid_t can_id_pong = CAN_MSG_ID_PONG;
static int has_pong_id = 0;
static int is_can_fd = 0;
static int bit_rate_switch = 0;
static int msg_len = CAN_MSG_LEN;

static void print_usage(char *prg)
{
	fprintf(stderr,
		"%s - Full-duplex test program (DUT and host part).\n"
		"Usage: %s [options] <can-interface>\n"
		"\n"
		"Options:\n"
		"         -b       (enable CAN FD Bit Rate Switch)\n"
		"         -d       (use CAN FD frames instead of classic CAN)\n"
		"         -f COUNT (number of frames in flight, default: %d)\n"
		"         -g       (generate messages)\n"
		"         -i ID    (CAN ID to use for frames to DUT (ping), default %x)\n"
		"         -l COUNT (test loop count)\n"
		"         -o ID    (CAN ID to use for frames to host (pong), default %x)\n"
		"         -s SIZE  (frame payload size in bytes)\n"
		"         -v       (low verbosity)\n"
		"         -vv      (high verbosity)\n"
		"         -x       (ignore other frames on bus)\n"
		"\n"
		"With the option '-g' CAN messages are generated and checked\n"
		"on <can-interface>, otherwise all messages received on the\n"
                "<can-interface> are sent back incrementing the CAN id and\n"
		"all data bytes. The program can be aborted with ^C.\n"
		"\n"
		"Examples:\n"
		"\ton DUT:\n"
		"%s -v can0\n"
		"\ton Host:\n"
		"%s -g -v can2\n",
		prg, prg, CAN_MSG_COUNT, CAN_MSG_ID_PING, CAN_MSG_ID_PONG, prg, prg);

	exit(1);
}

static void print_frame(canid_t id, const uint8_t *data, int dlc, int inc_data)
{
	int i;

	printf("%04x: ", id);
	if (id & CAN_RTR_FLAG) {
		printf("remote request");
	} else {
		printf("[%d]", dlc);
		for (i = 0; i < dlc; i++)
			printf(" %02x", (uint8_t)(data[i] + inc_data));
	}
	printf("\n");
}

static void print_compare(
	canid_t exp_id,
	const uint8_t *exp_data,
	uint8_t exp_dlc,
	canid_t rec_id,
	const uint8_t *rec_data,
	uint8_t rec_dlc,
	int inc)
{
	printf("expected: ");
	print_frame(exp_id, exp_data, exp_dlc, inc);
	printf("received: ");
	print_frame(rec_id, rec_data, rec_dlc, 0);
}

static int compare_frame(const struct canfd_frame *exp, const struct canfd_frame *rec, int inc)
{
	int i, err = 0;
	const canid_t expected_can_id = inc ? can_id_pong : can_id_ping;

	if (rec->can_id != expected_can_id) {
		printf("Message ID mismatch!\n");
		print_compare(expected_can_id, exp->data, exp->len,
		              rec->can_id, rec->data, rec->len, inc);
		running = 0;
		err = -1;
	} else if (rec->len != exp->len) {
		printf("Message length mismatch!\n");
		print_compare(expected_can_id, exp->data, exp->len,
		              rec->can_id, rec->data, rec->len, inc);
		running = 0;
		err = -1;
	} else {
		for (i = 0; i < rec->len; i++) {
			if (rec->data[i] != (uint8_t)(exp->data[i] + inc)) {
				printf("Databyte %x mismatch!\n", i);
				print_compare(expected_can_id, exp->data, exp->len,
				              rec->can_id, rec->data, rec->len, inc);
				running = 0;
				err = -1;
			}
		}
	}
	return err;
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

static int recv_frame(struct canfd_frame *frame)
{
	ssize_t ret, len;

	if (is_can_fd)
		len = sizeof(struct canfd_frame);
	else
		len = sizeof(struct can_frame);

	ret = recv(sockfd, frame, len, 0);
	if (ret != len) {
		if (ret < 0)
			perror("recv failed");
		else
			fprintf(stderr, "recv returned %zd", ret);
		return -1;
	}
	return 0;
}

static int send_frame(struct canfd_frame *frame)
{
	ssize_t ret, len;

	if (is_can_fd)
		len = sizeof(struct canfd_frame);
	else
		len = sizeof(struct can_frame);

	if (bit_rate_switch)
		frame->flags |= CANFD_BRS;

	while ((ret = send(sockfd, frame, len, 0)) != len) {
		if (ret >= 0) {
			fprintf(stderr, "send returned %zd", ret);
			return -1;
		}
		if (errno != ENOBUFS) {
			perror("send failed");
			return -1;
		}
		if (verbose) {
			printf("N");
			fflush(stdout);
		}
	}
	return 0;
}

static int check_frame(const struct canfd_frame *frame)
{
	int err = 0;
	int i;

	if (frame->can_id != can_id_ping) {
		printf("Unexpected Message ID 0x%04x!\n", frame->can_id);
		err = -1;
	}

	if (frame->len != msg_len) {
		printf("Unexpected Message length %d!\n", frame->len);
		err = -1;
	}

	for (i = 1; i < frame->len; i++) {
		if (frame->data[i] != (uint8_t)(frame->data[i-1] + 1)) {
			printf("Frame inconsistent!\n");
			print_frame(frame->can_id, frame->data, frame->len, 0);
			err = -1;
			goto out;
		}
	}

 out:
	return err;
}

static void inc_frame(struct canfd_frame *frame)
{
	int i;

	if (has_pong_id)
		frame->can_id = can_id_pong;
	else
		frame->can_id++;

	for (i = 0; i < frame->len; i++)
		frame->data[i]++;
}

static int can_echo_dut(void)
{
	unsigned int frame_count = 0;
	struct canfd_frame frame;
	int err = 0;

	while (running) {
		if (recv_frame(&frame))
			return -1;
		frame_count++;
		if (verbose == 1) {
			echo_progress(frame.data[0]);
		} else if (verbose > 1) {
			print_frame(frame.can_id, frame.data, frame.len, 0);
		}

		err = check_frame(&frame);
		inc_frame(&frame);
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

	return err;
}

static int can_echo_gen(void)
{
	struct canfd_frame *tx_frames;
	int *recv_tx;
	struct canfd_frame rx_frame;
	unsigned char counter = 0;
	int send_pos = 0, recv_rx_pos = 0, recv_tx_pos = 0, unprocessed = 0, loops = 0;
	int err = 0;
	int i;

	tx_frames = calloc(inflight_count, sizeof(* tx_frames));
	if (!tx_frames)
		return -1;

	recv_tx = calloc(inflight_count, sizeof(* recv_tx));
	if (!recv_tx) {
		err = -1;
		goto out_free_tx_frames;
	}

	while (running) {
		if (unprocessed < inflight_count) {
			/* still send messages */
			tx_frames[send_pos].len = msg_len;
			tx_frames[send_pos].can_id = can_id_ping;
			recv_tx[send_pos] = 0;

			for (i = 0; i < msg_len; i++)
				tx_frames[send_pos].data[i] = counter + i;
			if (send_frame(&tx_frames[send_pos])) {
				err = -1;
				goto out_free;
			}

			send_pos++;
			if (send_pos == inflight_count)
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
			if (recv_frame(&rx_frame)) {
				err = -1;
				goto out_free;
			}

			if (verbose > 1)
				print_frame(rx_frame.can_id, rx_frame.data, rx_frame.len, 0);

			/* own frame */
			if (rx_frame.can_id == can_id_ping) {
				err = compare_frame(&tx_frames[recv_tx_pos], &rx_frame, 0);
				recv_tx[recv_tx_pos] = 1;
				recv_tx_pos++;
				if (recv_tx_pos == inflight_count)
					recv_tx_pos = 0;
				continue;
			}

			if (!recv_tx[recv_rx_pos]) {
				printf("RX before TX!\n");
				print_frame(rx_frame.can_id, rx_frame.data, rx_frame.len, 0);
				running = 0;
			}
			/* compare with expected */
			err = compare_frame(&tx_frames[recv_rx_pos], &rx_frame, 1);
			recv_rx_pos++;
			if (recv_rx_pos == inflight_count)
				recv_rx_pos = 0;

			loops++;
			if (test_loops && loops >= test_loops)
				break;

			unprocessed--;
		}
	}

	printf("\nTest messages sent and received: %d\n", loops);

 out_free:
	free(recv_tx);
 out_free_tx_frames:
	free(tx_frames);

	return err;
}

int main(int argc, char *argv[])
{
	struct sockaddr_can addr;
	char *intf_name;
	int family = PF_CAN, type = SOCK_RAW, proto = CAN_RAW;
	int echo_gen = 0;
	int opt, err;
	int enable_socket_option = 1;
	int filter = 0;

	signal(SIGTERM, signal_handler);
	signal(SIGHUP, signal_handler);
	signal(SIGINT, signal_handler);

	while ((opt = getopt(argc, argv, "bdf:gi:l:o:s:vx?")) != -1) {
		switch (opt) {
		case 'b':
			bit_rate_switch = 1;
			break;

		case 'd':
			is_can_fd = 1;
			break;

		case 'f':
			inflight_count = atoi(optarg);
			break;

		case 'g':
			echo_gen = 1;
			break;

		case 'i':
			can_id_ping = strtoul(optarg, NULL, 16) & CAN_SFF_MASK;
			break;

		case 'l':
			test_loops = atoi(optarg);
			break;

		case 'o':
			can_id_pong = strtoul(optarg, NULL, 16) & CAN_SFF_MASK;
			has_pong_id = 1;
			break;

		case 's':
			msg_len = atoi(optarg);
			break;

		case 'v':
			verbose++;
			break;

		case 'x':
			filter = 1;
			break;

		case '?':
		default:
			print_usage(basename(argv[0]));
			break;
		}
	}

	/* BRS can be enabled only if CAN FD is enabled */
	if (bit_rate_switch && !is_can_fd) {
		printf("Bit rate switch (-b) needs CAN FD (-d) to be enabled\n");
		return 1;
	}

	/* Make sure the message length is valid */
	if (msg_len <= 0) {
		printf("Message length must > 0\n");
		return 1;
	}
	if (is_can_fd) {
		if (msg_len > CANFD_MAX_DLEN) {
			printf("Message length must be <= %d bytes for CAN FD\n", CANFD_MAX_DLEN);
			return 1;
		}
	} else {
		if (msg_len > CAN_MAX_DLEN) {
			printf("Message length must be <= %d bytes for CAN 2.0B\n", CAN_MAX_DLEN);
			return 1;
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
			   &enable_socket_option, sizeof(enable_socket_option)) == -1) {
			perror("setsockopt CAN_RAW_RECV_OWN_MSGS");
			return 1;
		}
	}

	if (is_can_fd) {
		if (setsockopt(sockfd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
			   &enable_socket_option, sizeof(enable_socket_option)) == -1) {
			perror("setsockopt CAN_RAW_FD_FRAMES");
			return 1;
		}
	}

	addr.can_family = family;
	addr.can_ifindex = if_nametoindex(intf_name);
	if (!addr.can_ifindex) {
		perror("if_nametoindex");
		close(sockfd);
		return 1;
	}

	if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(sockfd);
		return 1;
	}

	if (!has_pong_id)
		can_id_pong = can_id_ping + 1;

	if (filter) {
		const struct can_filter filters[] = {
			{
				.can_id = can_id_ping,
				.can_mask = CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_SFF_MASK,
			},
			{
				.can_id = can_id_pong,
				.can_mask = CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_SFF_MASK,
			},
		};

		if (setsockopt(sockfd, SOL_CAN_RAW, CAN_RAW_FILTER, filters,
					   sizeof(struct can_filter) * (1 + echo_gen))) {
			perror("setsockopt()");
			close(sockfd);
			return 1;
		}
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
