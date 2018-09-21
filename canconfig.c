/*
 * canutils/canconfig.c
 *
 * Copyright (C) 2005, 2008 Marc Kleine-Budde <mkl@pengutronix.de>, Pengutronix
 * Copyright (C) 2007 Juergen Beisert <jbe@pengutronix.de>, Pengutronix
 * Copyright (C) 2009 Luotao Fu <l.fu@pengutronix.de>, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libsocketcan.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

const char *can_states[CAN_STATE_MAX] = {
	"ERROR-ACTIVE",
	"ERROR-WARNING",
	"ERROR-PASSIVE",
	"BUS-OFF",
	"STOPPED",
	"SLEEPING"
};

const char *config_keywords[] = {
		"baudrate", "bitrate", "bittiming", "ctrlmode", "restart",
		"start", "stop", "restart-ms", "state", "clockfreq",
		"bittiming-const", "berr-counter"};

/* this is shamelessly stolen from iproute and slightly modified */
#define NEXT_ARG() \
	do { \
		argv++; \
		if (--argc < 0) { \
			fprintf(stderr, "missing parameter for %s\n", *argv); \
			exit(EXIT_FAILURE);\
		}\
	} while(0)

static inline int find_str(const char** haystack, unsigned int stack_size,
		const char* needle)
{
	int i, found = 0;

	for (i = 0; i < stack_size; i++) {
		if (!strcmp(needle, haystack[i])) {
			found = 1;
			break;
		}
	}

	return found;
}

static void help(void)
{
	fprintf(stderr, "usage:\n\t"
		"canconfig <dev> bitrate { BR } [sample-point { SP }]\n\t\t"
		"BR := <bitrate in Hz>\n\t\t"
		"SP := <sample-point {0...0.999}> (optional)\n\t"
		"canconfig <dev> bittiming [ VALs ]\n\t\t"
		"VALs := <tq | prop-seg | phase-seg1 | phase-seg2 | sjw>\n\t\t"
		"tq <time quantum in ns>\n\t\t"
		"prop-seg <no. in tq>\n\t\t"
		"phase-seg1 <no. in tq>\n\t\t"
		"phase-seg2 <no. in tq\n\t\t"
		"sjw <no. in tq> (optional)\n\t"
		"canconfig <dev> restart-ms { RESTART-MS }\n\t\t"
		"RESTART-MS := <autorestart interval in ms>\n\t"
		"canconfig <dev> ctrlmode { CTRLMODE }\n\t\t"
		"CTRLMODE := <[loopback | listen-only | triple-sampling | berr-reporting] [on|off]>\n\t"
		"canconfig <dev> {ACTION}\n\t\t"
		"ACTION := <[start|stop|restart]>\n\t"
		"canconfig <dev> clockfreq\n\t"
		"canconfig <dev> bittiming-constants\n\t"
		"canconfig <dev> berr-counter\n"
		);

	exit(EXIT_FAILURE);
}

static void do_show_bitrate(const char *name)
{
	struct can_bittiming bt;

	if (can_get_bittiming(name, &bt) < 0) {
		fprintf(stderr, "%s: failed to get bitrate\n", name);
		exit(EXIT_FAILURE);
	} else
		fprintf(stdout,
			"%s bitrate: %u, sample-point: %0.3f\n",
			name, bt.bitrate,
			(float)((float)bt.sample_point / 1000));
}

static void do_set_bitrate(int argc, char *argv[], const char *name)
{
	__u32 bitrate = 0;
	__u32 sample_point = 0;
	int err;

	while (argc > 0) {
		if (!strcmp(*argv, "bitrate")) {
			NEXT_ARG();
			bitrate =  (__u32)strtoul(*argv, NULL, 0);
		} else if (!strcmp(*argv, "sample-point")) {
			NEXT_ARG();
			sample_point = (__u32)(strtod(*argv, NULL) * 1000);
		}
		argc--, argv++;
	}

	if (sample_point)
		err = can_set_bitrate_samplepoint(name, bitrate, sample_point);
	else
		err = can_set_bitrate(name, bitrate);

	if (err < 0) {
		fprintf(stderr, "failed to set bitrate of %s to %u\n",
			name, bitrate);
		exit(EXIT_FAILURE);
	}
}

static void cmd_bitrate(int argc, char *argv[], const char *name)
{
	int show_only = 1;

	if (argc > 0)
		show_only = find_str(config_keywords,
				sizeof(config_keywords) / sizeof(char*),
				argv[1]);

	if (! show_only)
		do_set_bitrate(argc, argv, name);

	do_show_bitrate(name);
}

static void do_set_bittiming(int argc, char *argv[], const char *name)
{
	struct can_bittiming bt;
	int bt_par_count = 0;

	memset(&bt, 0, sizeof(bt));

	while (argc > 0) {
		if (!strcmp(*argv, "tq")) {
			NEXT_ARG();
			bt.tq = (__u32)strtoul(*argv, NULL, 0);
			bt_par_count++;
			continue;
		}
		if (!strcmp(*argv, "prop-seg")) {
			NEXT_ARG();
			bt.prop_seg = (__u32)strtoul(*argv, NULL, 0);
			bt_par_count++;
			continue;
		}
		if (!strcmp(*argv, "phase-seg1")) {
			NEXT_ARG();
			bt.phase_seg1 = (__u32)strtoul(*argv, NULL, 0);
			bt_par_count++;
			continue;
		}
		if (!strcmp(*argv, "phase-seg2")) {
			NEXT_ARG();
			bt.phase_seg2 =
				(__u32)strtoul(*argv, NULL, 0);
			bt_par_count++;
			continue;
		}
		if (!strcmp(*argv, "sjw")) {
			NEXT_ARG();
			bt.sjw =
				(__u32)strtoul(*argv, NULL, 0);
			continue;
		}
		argc--, argv++;
	}
	/* kernel will take a default sjw value if it's zero. all other
	 * parameters have to be set */
	if (bt_par_count < 4) {
		fprintf(stderr, "%s: missing bittiming parameters, "
				"try help to figure out the correct format\n",
				name);
		exit(1);
	}
	if (can_set_bittiming(name, &bt) < 0) {
		fprintf(stderr, "%s: unable to set bittiming\n", name);
		exit(EXIT_FAILURE);
	}
}

static void do_show_bittiming(const char *name)
{
	struct can_bittiming bt;

	if (can_get_bittiming(name, &bt) < 0) {
		fprintf(stderr, "%s: failed to get bittiming\n", name);
		exit(EXIT_FAILURE);
	} else
		fprintf(stdout, "%s bittiming:\n\t"
			"tq: %u, prop-seq: %u phase-seq1: %u phase-seq2: %u "
			"sjw: %u, brp: %u\n",
			name, bt.tq, bt.prop_seg, bt.phase_seg1, bt.phase_seg2,
			bt.sjw, bt.brp);
}

static void cmd_bittiming(int argc, char *argv[], const char *name)
{
	int show_only = 1;

	if (argc > 0)
		show_only = find_str(config_keywords,
				sizeof(config_keywords) / sizeof(char*),
				argv[1]);

	if (! show_only)
		do_set_bittiming(argc, argv, name);

	do_show_bittiming(name);
	do_show_bitrate(name);
}

static void do_show_bittiming_const(const char *name)
{
	struct can_bittiming_const btc;

	if (can_get_bittiming_const(name, &btc) < 0) {
		fprintf(stderr, "%s: failed to get bittiming_const\n", name);
		exit(EXIT_FAILURE);
	} else
		fprintf(stdout, "%s bittiming-constants: name %s,\n\t"
			"tseg1-min: %u, tseg1-max: %u, "
			"tseg2-min: %u, tseg2-max: %u,\n\t"
			"sjw-max %u, brp-min: %u, brp-max: %u, brp-inc: %u,\n",
			name, btc.name, btc.tseg1_min, btc.tseg1_max,
			btc.tseg2_min, btc.tseg2_max, btc.sjw_max,
			btc.brp_min, btc.brp_max, btc.brp_inc);
}

static void cmd_bittiming_const(int argc, char *argv[], const char *name)
{
	do_show_bittiming_const(name);
}

static void do_show_state(const char *name)
{
	int state;

	if (can_get_state(name, &state) < 0) {
		fprintf(stderr, "%s: failed to get state \n", name);
		exit(EXIT_FAILURE);
	}

	if (state >= 0 && state < CAN_STATE_MAX)
		fprintf(stdout, "%s state: %s\n", name, can_states[state]);
	else
		fprintf(stderr, "%s: unknown state\n", name);
}

static void cmd_state(int argc, char *argv[], const char *name)
{
	do_show_state(name);
}

static void do_show_clockfreq(const char *name)
{
	struct can_clock clock;

	memset(&clock, 0, sizeof(struct can_clock));
	if (can_get_clock(name, &clock) < 0) {
		fprintf(stderr, "%s: failed to get clock parameters\n",
				name);
		exit(EXIT_FAILURE);
	}

	fprintf(stdout, "%s clock freq: %u\n", name, clock.freq);
}

static void cmd_clockfreq(int argc, char *argv[], const char *name)
{
	do_show_clockfreq(name);
}

static void do_restart(const char *name)
{
	if (can_do_restart(name) < 0) {
		fprintf(stderr, "%s: failed to restart\n", name);
		exit(EXIT_FAILURE);
	} else {
		fprintf(stdout, "%s restarted\n", name);
	}
}

static void cmd_restart(int argc, char *argv[], const char *name)
{
	do_restart(name);
}

static void do_start(const char *name)
{
	if (can_do_start(name) < 0) {
		fprintf(stderr, "%s: failed to start\n", name);
		exit(EXIT_FAILURE);
	} else {
		do_show_state(name);
	}
}

static void cmd_start(int argc, char *argv[], const char *name)
{
	do_start(name);
}

static void do_stop(const char *name)
{
	if (can_do_stop(name) < 0) {
		fprintf(stderr, "%s: failed to stop\n", name);
		exit(EXIT_FAILURE);
	} else {
		do_show_state(name);
	}
}

static void cmd_stop(int argc, char *argv[], const char *name)
{
	do_stop(name);
}

static inline void print_ctrlmode(__u32 cm_flags)
{
	fprintf(stdout,
		"loopback[%s], listen-only[%s], tripple-sampling[%s],"
		"one-shot[%s], berr-reporting[%s]\n",
		(cm_flags & CAN_CTRLMODE_LOOPBACK) ? "ON" : "OFF",
		(cm_flags & CAN_CTRLMODE_LISTENONLY) ? "ON" : "OFF",
		(cm_flags & CAN_CTRLMODE_3_SAMPLES) ? "ON" : "OFF",
		(cm_flags & CAN_CTRLMODE_ONE_SHOT) ? "ON" : "OFF",
		(cm_flags & CAN_CTRLMODE_BERR_REPORTING) ? "ON" : "OFF");
}

static void do_show_ctrlmode(const char *name)
{
	struct can_ctrlmode cm;

	if (can_get_ctrlmode(name, &cm) < 0) {
		fprintf(stderr, "%s: failed to get controlmode\n", name);
		exit(EXIT_FAILURE);
	} else {
		fprintf(stdout, "%s ctrlmode: ", name);
		print_ctrlmode(cm.flags);
	}
}

/* this is shamelessly stolen from iproute and slightly modified */
static inline void set_ctrlmode(char* name, char *arg,
			 struct can_ctrlmode *cm, __u32 flags)
{
	if (strcmp(arg, "on") == 0) {
		cm->flags |= flags;
	} else if (strcmp(arg, "off") != 0) {
		fprintf(stderr,
			"Error: argument of \"%s\" must be \"on\" or \"off\" %s\n",
			name, arg);
		exit(EXIT_FAILURE);
	}
	cm->mask |= flags;
}

static void do_set_ctrlmode(int argc, char* argv[], const char *name)
{
	struct can_ctrlmode cm;

	memset(&cm, 0, sizeof(cm));

	while (argc > 0) {
		if (!strcmp(*argv, "loopback")) {
			NEXT_ARG();
			set_ctrlmode("loopback", *argv, &cm,
				     CAN_CTRLMODE_LOOPBACK);
		} else if (!strcmp(*argv, "listen-only")) {
			NEXT_ARG();
			set_ctrlmode("listen-only", *argv, &cm,
				     CAN_CTRLMODE_LISTENONLY);
		} else if (!strcmp(*argv, "triple-sampling")) {
			NEXT_ARG();
			set_ctrlmode("triple-sampling", *argv, &cm,
				     CAN_CTRLMODE_3_SAMPLES);
		} else if (!strcmp(*argv, "one-shot")) {
			NEXT_ARG();
			set_ctrlmode("one-shot", *argv, &cm,
				     CAN_CTRLMODE_ONE_SHOT);
		} else if (!strcmp(*argv, "berr-reporting")) {
			NEXT_ARG();
			set_ctrlmode("berr-reporting", *argv, &cm,
				     CAN_CTRLMODE_BERR_REPORTING);
		}

		argc--, argv++;
	}

	if (can_set_ctrlmode(name, &cm) < 0) {
		fprintf(stderr, "%s: failed to set ctrlmode\n", name);
		exit(EXIT_FAILURE);
	}
}

static void cmd_ctrlmode(int argc, char *argv[], const char *name)
{
	int show_only = 1;

	if (argc > 0)
		show_only = find_str(config_keywords,
				sizeof(config_keywords) / sizeof(char*),
				argv[1]);

	if (! show_only)
		do_set_ctrlmode(argc, argv, name);

	do_show_ctrlmode(name);
}

static void do_show_restart_ms(const char *name)
{
	__u32 restart_ms;

	if (can_get_restart_ms(name, &restart_ms) < 0) {
		fprintf(stderr, "%s: failed to get restart_ms\n", name);
		exit(EXIT_FAILURE);
	} else
		fprintf(stdout,
			"%s restart-ms: %u\n", name, restart_ms);
}

static void do_set_restart_ms(int argc, char* argv[], const char *name)
{
	if (can_set_restart_ms(name,
				(__u32)strtoul(argv[1], NULL, 10)) < 0) {
		fprintf(stderr, "failed to set restart_ms of %s to %lu\n",
		       	name, strtoul(argv[1], NULL, 10));
		exit(EXIT_FAILURE);
	}
}

static void cmd_restart_ms(int argc, char *argv[], const char *name)
{
	int show_only = 1;

	if (argc > 0)
		show_only = find_str(config_keywords,
				sizeof(config_keywords) / sizeof(char*),
				argv[1]);

	if (! show_only)
		do_set_restart_ms(argc, argv, name);

	do_show_restart_ms(name);
}

static void do_show_berr_counter(const char *name)
{
	struct can_berr_counter bc;
	struct can_ctrlmode cm;

	if (can_get_ctrlmode(name, &cm) < 0) {
		fprintf(stderr, "%s: failed to get controlmode\n", name);
		exit(EXIT_FAILURE);
	}

	if (cm.flags & CAN_CTRLMODE_BERR_REPORTING) {
		memset(&bc, 0, sizeof(struct can_berr_counter));

		if (can_get_berr_counter(name, &bc) < 0) {
			fprintf(stderr, "%s: failed to get berr counters\n",
					name);
			exit(EXIT_FAILURE);
		}

		fprintf(stdout, "%s txerr: %u rxerr: %u\n",
				name, bc.txerr, bc.rxerr);
	}
}

static void cmd_berr_counter(int argc, char *argv[], const char *name)
{
	do_show_berr_counter(name);
}

static void cmd_baudrate(int argc, char *argv[], const char *name)
{
	fprintf(stderr, "%s: baudrate is deprecated, pleae use bitrate\n",
		name);

	exit(EXIT_FAILURE);
}

static void cmd_show_interface(const char *name)
{
	do_show_bitrate(name);
	do_show_bittiming(name);
	do_show_state(name);
	do_show_restart_ms(name);
	do_show_ctrlmode(name);
	do_show_clockfreq(name);
	do_show_bittiming_const(name);
	do_show_berr_counter(name);

	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	const char* name = argv[1];

	if ((argc < 2) || !strcmp(argv[1], "--help"))
		help();

	if (!strcmp(argv[1], "--version")) {
		printf("Version: %s\n", VERSION);
		exit(EXIT_SUCCESS);
	}

	if (argc < 3)
		cmd_show_interface(name);

	while (argc-- > 0) {
		if (!strcmp(argv[0], "baudrate"))
			cmd_baudrate(argc, argv, name);
		if (!strcmp(argv[0], "bitrate"))
			cmd_bitrate(argc, argv, name);
		if (!strcmp(argv[0], "bittiming"))
			cmd_bittiming(argc, argv, name);
		if (!strcmp(argv[0], "ctrlmode"))
			cmd_ctrlmode(argc, argv, name);
		if (!strcmp(argv[0], "restart"))
			cmd_restart(argc, argv, name);
		if (!strcmp(argv[0], "start"))
			cmd_start(argc, argv, name);
		if (!strcmp(argv[0], "stop"))
			cmd_stop(argc, argv, name);
		if (!strcmp(argv[0], "restart-ms"))
			cmd_restart_ms(argc, argv, name);
		if (!strcmp(argv[0], "state"))
			cmd_state(argc, argv, name);
		if (!strcmp(argv[0], "clockfreq"))
			cmd_clockfreq(argc, argv, name);
		if (!strcmp(argv[0], "bittiming-constants"))
			cmd_bittiming_const(argc, argv, name);
		if (!strcmp(argv[0], "berr-counter"))
			cmd_berr_counter(argc, argv, name);
		argv++;
	}

	exit(EXIT_SUCCESS);
}
