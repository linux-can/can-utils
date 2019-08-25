/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * slcand.c - userspace daemon for serial line CAN interface driver SLCAN
 *
 * Copyright (c) 2009 Robert Haddon <robert.haddon@verari.com>
 * Copyright (c) 2009 Verari Systems Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Send feedback to <linux-can@vger.kernel.org>
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <termios.h>
#include <linux/tty.h>
#include <linux/sockios.h>
#include <linux/serial.h>
#include <stdarg.h>

/* Change this to whatever your daemon is called */
#define DAEMON_NAME "slcand"

/* Change this to the user under which to run */
#define RUN_AS_USER "root"

/* The length of ttypath buffer */
#define TTYPATH_LENGTH	256

/* UART flow control types */
#define FLOW_NONE 0
#define FLOW_HW 1
#define FLOW_SW 2

static void fake_syslog(int priority, const char *format, ...)
{
	va_list ap;

	printf("[%d] ", priority);
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
	printf("\n");
}

typedef void (*syslog_t)(int priority, const char *format, ...);
static syslog_t syslogger = syslog;

void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s [options] <tty> [canif-name]\n\n", prg);
	fprintf(stderr, "Options: -o         (send open command 'O\\r')\n");
	fprintf(stderr, "         -c         (send close command 'C\\r')\n");
	fprintf(stderr, "         -f         (read status flags with 'F\\r' to reset error states)\n");
	fprintf(stderr, "         -l         (send listen only command 'L\\r', overrides -o)\n");
	fprintf(stderr, "         -s <speed> (set CAN speed 0..8)\n");
	fprintf(stderr, "         -S <speed> (set UART speed in baud)\n");
	fprintf(stderr, "         -t <type>  (set UART flow control type 'hw' or 'sw')\n");
	fprintf(stderr, "         -b <btr>   (set bit time register value)\n");
	fprintf(stderr, "         -F         (stay in foreground; no daemonize)\n");
	fprintf(stderr, "         -h         (show this help page)\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "slcand -o -c -f -s6 ttyUSB0\n");
	fprintf(stderr, "slcand -o -c -f -s6 ttyUSB0 can0\n");
	fprintf(stderr, "slcand -o -c -f -s6 /dev/ttyUSB0\n");
	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}

static int slcand_running;
static int exit_code;
static char ttypath[TTYPATH_LENGTH];

static void child_handler(int signum)
{
	switch (signum) {

	case SIGUSR1:
		/* exit parent */
		exit(EXIT_SUCCESS);
		break;
	case SIGALRM:
	case SIGCHLD:
		syslogger(LOG_NOTICE, "received signal %i on %s", signum, ttypath);
		exit_code = EXIT_FAILURE;
		slcand_running = 0;
		break;
	case SIGINT:
	case SIGTERM:
		syslogger(LOG_NOTICE, "received signal %i on %s", signum, ttypath);
		exit_code = EXIT_SUCCESS;
		slcand_running = 0;
		break;
	}
}

static int look_up_uart_speed(long int s)
{
	switch (s) {

	case 9600:
		return B9600;
	case 19200:
		return B19200;
	case 38400:
		return B38400;
	case 57600:
		return B57600;
	case 115200:
		return B115200;
	case 230400:
		return B230400;
	case 460800:
		return B460800;
	case 500000:
		return B500000;
	case 576000:
		return B576000;
	case 921600:
		return B921600;
	case 1000000:
		return B1000000;
	case 1152000:
		return B1152000;
	case 1500000:
		return B1500000;
	case 2000000:
		return B2000000;
#ifdef B2500000
	case 2500000:
		return B2500000;
#endif
#ifdef B3000000
	case 3000000:
		return B3000000;
#endif
#ifdef B3500000
	case 3500000:
		return B3500000;
#endif
#ifdef B3710000
	case 3710000
		return B3710000;
#endif
#ifdef B4000000
	case 4000000:
		return B4000000;
#endif
	default:
		return -1;
	}
}

int main(int argc, char *argv[])
{
	char *tty = NULL;
	char const *devprefix = "/dev/";
	char *name = NULL;
	char buf[20];
	static struct ifreq ifr;
	struct termios tios;
	speed_t old_ispeed;
	speed_t old_ospeed;

	int opt;
	int send_open = 0;
	int send_close = 0;
	int send_listen = 0;
	int send_read_status_flags = 0;
	char *speed = NULL;
	char *uart_speed_str = NULL;
	long int uart_speed = 0;
	int flow_type = FLOW_NONE;
	char *btr = NULL;
	int run_as_daemon = 1;
	char *pch;
	int ldisc = N_SLCAN;
	int fd;

	ttypath[0] = '\0';

	while ((opt = getopt(argc, argv, "ocfls:S:t:b:?hF")) != -1) {
		switch (opt) {
		case 'o':
			send_open = 1;
			break;
		case 'c':
			send_close = 1;
			break;
		case 'f':
			send_read_status_flags = 1;
			break;
		case 'l':
			send_listen = 1;
			break;
		case 's':
			speed = optarg;
			if (strlen(speed) > 1)
				print_usage(argv[0]);
			break;
		case 'S':
			uart_speed_str = optarg;
			errno = 0;
			uart_speed = strtol(uart_speed_str, NULL, 10);
			if (errno)
				print_usage(argv[0]);
			if (look_up_uart_speed(uart_speed) == -1) {
				fprintf(stderr, "Unsupported UART speed (%lu)\n", uart_speed);
				exit(EXIT_FAILURE);
			}
			break;
		case 't':
			if (!strcmp(optarg, "hw")) {
				flow_type = FLOW_HW;
			} else if (!strcmp(optarg, "sw")) {
				flow_type = FLOW_SW;
			} else {
				fprintf(stderr, "Unsupported flow type (%s)\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;
		case 'b':
			btr = optarg;
			if (strlen(btr) > 6)
				print_usage(argv[0]);
			break;
		case 'F':
			run_as_daemon = 0;
			break;
		case 'h':
		case '?':
		default:
			print_usage(argv[0]);
			break;
		}
	}

	if (!run_as_daemon)
		syslogger = fake_syslog;

	/* Initialize the logging interface */
	openlog(DAEMON_NAME, LOG_PID, LOG_LOCAL5);

	/* Parse serial device name and optional can interface name */
	tty = argv[optind];
	if (NULL == tty)
		print_usage(argv[0]);

	name = argv[optind + 1];
	if (name && (strlen(name) > sizeof(ifr.ifr_newname) - 1))
		print_usage(argv[0]);

	/* Prepare the tty device name string */
	pch = strstr(tty, devprefix);
	if (pch != tty)
		snprintf(ttypath, TTYPATH_LENGTH, "%s%s", devprefix, tty);
	else
		snprintf(ttypath, TTYPATH_LENGTH, "%s", tty);

	syslogger(LOG_INFO, "starting on TTY device %s", ttypath);

	fd = open(ttypath, O_RDWR | O_NONBLOCK | O_NOCTTY);
	if (fd < 0) {
		syslogger(LOG_NOTICE, "failed to open TTY device %s\n", ttypath);
		perror(ttypath);
		exit(EXIT_FAILURE);
	}

	/* Configure baud rate */
	memset(&tios, 0, sizeof(tios));
	if (tcgetattr(fd, &tios) < 0) {
		syslogger(LOG_NOTICE, "failed to get attributes for TTY device %s: %s\n", ttypath, strerror(errno));
		exit(EXIT_FAILURE);
	}

	// Because of a recent change in linux - https://patchwork.kernel.org/patch/9589541/
	// we need to set low latency flag to get proper receive latency
	struct serial_struct snew;
	ioctl (fd, TIOCGSERIAL, &snew);
	snew.flags |= ASYNC_LOW_LATENCY;
	ioctl (fd, TIOCSSERIAL, &snew);

	/* Get old values for later restore */
	old_ispeed = cfgetispeed(&tios);
	old_ospeed = cfgetospeed(&tios);

	/* Reset UART settings */
	cfmakeraw(&tios);
	tios.c_iflag &= ~IXOFF;
	tios.c_cflag &= ~CRTSCTS;

	/* Baud Rate */
	cfsetispeed(&tios, look_up_uart_speed(uart_speed));
	cfsetospeed(&tios, look_up_uart_speed(uart_speed));

	/* Flow control */
	if (flow_type == FLOW_HW)
		tios.c_cflag |= CRTSCTS;
	else if (flow_type == FLOW_SW)
		tios.c_iflag |= (IXON | IXOFF);

	/* apply changes */
	if (tcsetattr(fd, TCSADRAIN, &tios) < 0)
		syslogger(LOG_NOTICE, "Cannot set attributes for device \"%s\": %s!\n", ttypath, strerror(errno));

	if (speed) {
		sprintf(buf, "C\rS%s\r", speed);
		if (write(fd, buf, strlen(buf)) <= 0) {
			perror("write");
			exit(EXIT_FAILURE);
		}
	}

	if (btr) {
		sprintf(buf, "C\rs%s\r", btr);
		if (write(fd, buf, strlen(buf)) <= 0) {
			perror("write");
			exit(EXIT_FAILURE);
		}
	}

	if (send_read_status_flags) {
		sprintf(buf, "F\r");
		if (write(fd, buf, strlen(buf)) <= 0) {
			perror("write");
			exit(EXIT_FAILURE);
		}
	}

	if (send_listen) {
		sprintf(buf, "L\r");
		if (write(fd, buf, strlen(buf)) <= 0) {
			perror("write");
			exit(EXIT_FAILURE);
		}
	} else if (send_open) {
		sprintf(buf, "O\r");
		if (write(fd, buf, strlen(buf)) <= 0) {
			perror("write");
			exit(EXIT_FAILURE);
		}
	}

	/* set slcan like discipline on given tty */
	if (ioctl(fd, TIOCSETD, &ldisc) < 0) {
		perror("ioctl TIOCSETD");
		exit(EXIT_FAILURE);
	}
	
	/* retrieve the name of the created CAN netdevice */
	if (ioctl(fd, SIOCGIFNAME, ifr.ifr_name) < 0) {
		perror("ioctl SIOCGIFNAME");
		exit(EXIT_FAILURE);
	}

	syslogger(LOG_NOTICE, "attached TTY %s to netdevice %s\n", ttypath, ifr.ifr_name);
	
	/* try to rename the created netdevice */
	if (name) {
		int s = socket(PF_INET, SOCK_DGRAM, 0);

		if (s < 0)
			perror("socket for interface rename");
		else {
			/* current slcan%d name is still in ifr.ifr_name */
			memset (ifr.ifr_newname, 0, sizeof(ifr.ifr_newname));
			strncpy (ifr.ifr_newname, name, sizeof(ifr.ifr_newname) - 1);

			if (ioctl(s, SIOCSIFNAME, &ifr) < 0) {
				syslogger(LOG_NOTICE, "netdevice %s rename to %s failed\n", buf, name);
				perror("ioctl SIOCSIFNAME rename");
				exit(EXIT_FAILURE);
			} else
				syslogger(LOG_NOTICE, "netdevice %s renamed to %s\n", buf, name);

			close(s);
		}	
	}

	/* Daemonize */
	if (run_as_daemon) {
		if (daemon(0, 0)) {
			syslogger(LOG_ERR, "failed to daemonize");
			exit(EXIT_FAILURE);
		}
	}
	else {
		/* Trap signals that we expect to receive */
		signal(SIGINT, child_handler);
		signal(SIGTERM, child_handler);
	}

	slcand_running = 1;

	/* The Big Loop */
	while (slcand_running)
		sleep(1); /* wait 1 second */

	/* Reset line discipline */
	syslogger(LOG_INFO, "stopping on TTY device %s", ttypath);
	ldisc = N_TTY;
	if (ioctl(fd, TIOCSETD, &ldisc) < 0) {
		perror("ioctl TIOCSETD");
		exit(EXIT_FAILURE);
	}

	if (send_close) {
		sprintf(buf, "C\r");
		if (write(fd, buf, strlen(buf)) <= 0) {
			perror("write");
			exit(EXIT_FAILURE);
		}
	}

	/* Reset old rates */
	cfsetispeed(&tios, old_ispeed);
	cfsetospeed(&tios, old_ospeed);

	/* apply changes */
	if (tcsetattr(fd, TCSADRAIN, &tios) < 0)
		syslogger(LOG_NOTICE, "Cannot set attributes for device \"%s\": %s!\n", ttypath, strerror(errno));

	/* Finish up */
	syslogger(LOG_NOTICE, "terminated on %s", ttypath);
	closelog();
	return exit_code;
}
