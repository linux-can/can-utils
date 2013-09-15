/*
 * slcand.c - userspace daemon for serial line CAN interface driver SLCAN
 *
 * Copyright (c) 2009 Robert Haddon <robert.haddon@verari.com>
 * Copyright (c) 2009 Verari Systems Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
 * This code is derived from an example daemon code from
 *
 * http://en.wikipedia.org/wiki/Daemon_(computer_software)#Sample_Program_in_C_on_Linux
 * (accessed 2009-05-05)
 *
 * So it is additionally licensed under the GNU Free Documentation License:
 *
 * Permission is granted to copy, distribute and/or modify this document
 * under the terms of the GNU Free Documentation License, Version 1.2
 * or any later version published by the Free Software Foundation;
 * with no Invariant Sections, no Front-Cover Texts, and no Back-Cover Texts.
 * A copy of the license is included in the section entitled "GNU
 * Free Documentation License".
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
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <termios.h>

/* default slcan line discipline since Kernel 2.6.25 */
#define LDISC_N_SLCAN 17

/* Change this to whatever your daemon is called */
#define DAEMON_NAME "slcand"

/* Change this to the user under which to run */
#define RUN_AS_USER "root"

/* The length of ttypath buffer */
#define TTYPATH_LENGTH	64

/* The length of pidfile name buffer */
#define PIDFILE_LENGTH	64

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s [options] <tty> [canif-name]\n\n", prg);
	fprintf(stderr, "Options: -o         (send open command 'O\\r')\n");
	fprintf(stderr, "         -c         (send close command 'C\\r')\n");
	fprintf(stderr, "         -f         (read status flags with 'F\\r' to reset error states)\n");
	fprintf(stderr, "         -s <speed> (set CAN speed 0..8)\n");
	fprintf(stderr, "         -S <speed> (set UART speed in baud)\n");
	fprintf(stderr, "         -b <btr>   (set bit time register value)\n");
	fprintf(stderr, "         -F         (stay in foreground; no daemonize)\n");
	fprintf(stderr, "         -h         (show this help page)\n");
	fprintf(stderr, "\nExamples:\n");
	fprintf(stderr, "slcand -o -c -f -s6 ttyslcan0\n");
	fprintf(stderr, "slcand -o -c -f -s6 ttyslcan0 can0\n");
	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}

static int slcand_running;
static int exit_code;
static char ttypath[TTYPATH_LENGTH];
static char pidfile[PIDFILE_LENGTH];

static void child_handler(int signum)
{
	switch (signum) {

	case SIGUSR1:
		/* exit parent */
		exit(EXIT_SUCCESS);
		break;
	case SIGALRM:
	case SIGCHLD:
		syslog(LOG_NOTICE, "received signal %i on %s", signum, ttypath);
		exit_code = EXIT_FAILURE;
		slcand_running = 0;
		break;
	case SIGINT:
	case SIGTERM:
		syslog(LOG_NOTICE, "received signal %i on %s", signum, ttypath);
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

static pid_t daemonize(const char *lockfile, char *tty, char *name)
{
	pid_t pid, sid, parent;
	int lfp = -1;
	FILE *pFile;
	FILE *dummyFile;
	char const *pidprefix = "/var/run/";
	char const *pidsuffix = ".pid";

	snprintf(pidfile, PIDFILE_LENGTH, "%s%s-%s%s", pidprefix, DAEMON_NAME, tty, pidsuffix);

	/* already a daemon */
	if (getppid() == 1)
		return 0;

	/* Create the lock file as the current user */
	if (lockfile && lockfile[0]) {

		lfp = open(lockfile, O_RDWR | O_CREAT, 0640);
		if (lfp < 0)
		{
			syslog(LOG_ERR, "unable to create lock file %s, code=%d (%s)",
			       lockfile, errno, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	/* Drop user if there is one, and we were run as root */
	if (getuid() == 0 || geteuid() == 0) {
		struct passwd *pw = getpwnam(RUN_AS_USER);

		if (pw)
		{
			/* syslog(LOG_NOTICE, "setting user to " RUN_AS_USER); */
			setuid(pw->pw_uid);
		}
	}

	/* Trap signals that we expect to receive */
	signal(SIGINT, child_handler);
	signal(SIGTERM, child_handler);
	signal(SIGCHLD, child_handler);
	signal(SIGUSR1, child_handler);
	signal(SIGALRM, child_handler);

	/* Fork off the parent process */
	pid = fork();
	if (pid < 0) {
		syslog(LOG_ERR, "unable to fork daemon, code=%d (%s)",
		       errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* If we got a good PID, then we can exit the parent process. */
	if (pid > 0) {
		/* Wait for confirmation from the child via SIGTERM or SIGCHLD, or
		   for five seconds to elapse (SIGALRM).  pause() should not return. */
		alarm(5);
		pause();
		exit(EXIT_FAILURE);
	}

	/* At this point we are executing as the child process */
	parent = getppid();

	/* Cancel certain signals */
	signal(SIGCHLD, SIG_DFL);	/* A child process dies */
	signal(SIGTSTP, SIG_IGN);	/* Various TTY signals */
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	signal(SIGHUP, SIG_IGN);	/* Ignore hangup signal */
	signal(SIGINT, child_handler);
	signal(SIGTERM, child_handler);

	/* Change the file mode mask */
	umask(0);

	/* Create a new SID for the child process */
	sid = setsid();
	if (sid < 0) {
		syslog(LOG_ERR, "unable to create a new session, code %d (%s)",
		       errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	pFile = fopen(pidfile, "w");
	if (NULL == pFile) {
		syslog(LOG_ERR, "unable to create pid file %s, code=%d (%s)",
		       pidfile, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	fprintf(pFile, "%d\n", sid);
	fclose(pFile);

	/* Change the current working directory.  This prevents the current
	   directory from being locked; hence not being able to remove it. */
	if (chdir("/") < 0) {
		syslog(LOG_ERR, "unable to change directory to %s, code %d (%s)",
		       "/", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Redirect standard files to /dev/null */
	dummyFile = freopen("/dev/null", "r", stdin);
	dummyFile = freopen("/dev/null", "w", stdout);
	dummyFile = freopen("/dev/null", "w", stderr);

	/* Tell the parent process that we are A-okay */
	/* kill(parent, SIGUSR1); */
	return parent;
}

int main(int argc, char *argv[])
{
	char *tty = NULL;
	char const *devprefix = "/dev/";
	char *name = NULL;
	char buf[IFNAMSIZ+1];
	struct termios tios;
	speed_t old_ispeed;
	speed_t old_ospeed;

	int opt;
	int send_open = 0;
	int send_close = 0;
	int send_read_status_flags = 0;
	char *speed = NULL;
	char *uart_speed_str = NULL;
	long int uart_speed = 0;
	char *btr = NULL;
	int run_as_daemon = 1;
	pid_t parent_pid = 0;
	char *pch;
	int ldisc = LDISC_N_SLCAN;
	int fd;

	ttypath[0] = '\0';

	while ((opt = getopt(argc, argv, "ocfs:S:b:?hF")) != -1) {
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

	/* Initialize the logging interface */
	openlog(DAEMON_NAME, LOG_PID, LOG_LOCAL5);

	/* Parse serial device name and optional can interface name */
	tty = argv[optind];
	if (NULL == tty)
		print_usage(argv[0]);

	name = argv[optind + 1];

	/* Prepare the tty device name string */
	pch = strstr(tty, devprefix);
	if (pch == tty)
		print_usage(argv[0]);

	snprintf(ttypath, TTYPATH_LENGTH, "%s%s", devprefix, tty);
	syslog(LOG_INFO, "starting on TTY device %s", ttypath);

	/* Daemonize */
	if (run_as_daemon)
		parent_pid = daemonize("/var/lock/" DAEMON_NAME, tty, name);
	else {
		/* Trap signals that we expect to receive */
		signal(SIGINT, child_handler);
		signal(SIGTERM, child_handler);
	}

	/* */
	slcand_running = 1;

	/* Now we are a daemon -- do the work for which we were paid */
	fd = open(ttypath, O_RDWR | O_NONBLOCK | O_NOCTTY);
	if (fd < 0) {
		syslog(LOG_NOTICE, "failed to open TTY device %s\n", ttypath);
		perror(ttypath);
		exit(EXIT_FAILURE);
	}

	/* Configure baud rate */
	memset(&tios, 0, sizeof(struct termios));
	if (tcgetattr(fd, &tios) < 0) {
		syslog(LOG_NOTICE, "failed to get attributes for TTY device %s: %s\n", ttypath, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* Get old values for later restore */
	old_ispeed = cfgetispeed(&tios);
	old_ospeed = cfgetospeed(&tios);

	/* Baud Rate */
	cfsetispeed(&tios, look_up_uart_speed(uart_speed));
	cfsetospeed(&tios, look_up_uart_speed(uart_speed));

	/* apply changes */
	if (tcsetattr(fd, TCSADRAIN, &tios) < 0)
		syslog(LOG_NOTICE, "Cannot set attributes for device \"%s\": %s!\n", ttypath, strerror(errno));

	if (speed) {
		sprintf(buf, "C\rS%s\r", speed);
		write(fd, buf, strlen(buf));
	}

	if (btr) {
		sprintf(buf, "C\rs%s\r", btr);
		write(fd, buf, strlen(buf));
	}

	if (send_read_status_flags) {
		sprintf(buf, "F\r");
		write(fd, buf, strlen(buf));
	}

	if (send_open) {
		sprintf(buf, "O\r");
		write(fd, buf, strlen(buf));
	}

	/* set slcan like discipline on given tty */
	if (ioctl(fd, TIOCSETD, &ldisc) < 0) {
		perror("ioctl TIOCSETD");
		exit(1);
	}
	
	/* retrieve the name of the created CAN netdevice */
	if (ioctl(fd, SIOCGIFNAME, buf) < 0) {
		perror("ioctl SIOCGIFNAME");
		exit(1);
	}

	syslog(LOG_NOTICE, "attached TTY %s to netdevice %s\n", ttypath, buf);
	
	/* try to rename the created netdevice */
	if (name) {
		struct ifreq ifr;
		int s = socket(PF_INET, SOCK_DGRAM, 0);

		if (s < 0)
			perror("socket for interface rename");
		else {
			strncpy(ifr.ifr_name, buf, IFNAMSIZ);
			strncpy(ifr.ifr_newname, name, IFNAMSIZ);

			if (ioctl(s, SIOCSIFNAME, &ifr) < 0) {
				syslog(LOG_NOTICE, "netdevice %s rename to %s failed\n", buf, name);
				perror("ioctl SIOCSIFNAME rename");
				exit(1);
			} else
				syslog(LOG_NOTICE, "netdevice %s renamed to %s\n", buf, name);

			close(s);
		}	
	}
	if (parent_pid > 0)
		kill(parent_pid, SIGUSR1);

	/* The Big Loop */
	while (slcand_running)
		sleep(1); /* wait 1 second */

	/* Reset line discipline */
	syslog(LOG_INFO, "stopping on TTY device %s", ttypath);
	ldisc = N_TTY;
	if (ioctl(fd, TIOCSETD, &ldisc) < 0) {
		perror("ioctl TIOCSETD");
		exit(EXIT_FAILURE);
	}

	if (send_close) {
		sprintf(buf, "C\r");
		write(fd, buf, strlen(buf));
	}

	/* Reset old rates */
	cfsetispeed(&tios, old_ispeed);
	cfsetospeed(&tios, old_ospeed);

	/* apply changes */
	if (tcsetattr(fd, TCSADRAIN, &tios) < 0)
		syslog(LOG_NOTICE, "Cannot set attributes for device \"%s\": %s!\n", ttypath, strerror(errno));

	/* Remove pidfile */
	if (run_as_daemon)
		unlink(pidfile);

	/* Finish up */
	syslog(LOG_NOTICE, "terminated on %s", ttypath);
	closelog();
	return exit_code;
}
