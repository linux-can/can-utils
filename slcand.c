/*
 *  $Id$
 */

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
 * Send feedback to <socketcan-users@lists.berlios.de>
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

/* default slcan line discipline since Kernel 2.6.25 */
#define LDISC_N_SLCAN 17

/* Change this to whatever your daemon is called */
#define DAEMON_NAME "slcand"

/* Change this to the user under which to run */
#define RUN_AS_USER "root"

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void print_usage(char *prg)
{
	fprintf(stderr, "\nUsage: %s tty\n\n", prg);
	fprintf(stderr, "Example:\n");
	fprintf(stderr, "%s ttyUSB1\n", prg);
	fprintf(stderr, "%s ttyS0\n", prg);
	fprintf(stderr, "\n");
	exit(EXIT_FAILURE);
}

static void child_handler (int signum)
{
	switch (signum)
	{
	case SIGALRM:
		exit (EXIT_FAILURE);
		break;
	case SIGUSR1:
		exit (EXIT_SUCCESS);
		break;
	case SIGCHLD:
		exit (EXIT_FAILURE);
		break;
	}
}

static void daemonize (const char *lockfile, char *tty)
{
	pid_t pid, sid, parent;
	int lfp = -1;
	FILE * pFile;
	char const *pidprefix = "/var/run/";
	char const *pidsuffix = ".pid";
	char *pidfile;
	pidfile = malloc((strlen(pidprefix) +strlen(DAEMON_NAME) + strlen(tty) + strlen(pidsuffix) + 1) * sizeof(char));
	sprintf(pidfile, "%s%s-%s%s", pidprefix, DAEMON_NAME, tty, pidsuffix);

	/* already a daemon */
	if (getppid () == 1)
		return;

	/* Create the lock file as the current user */
	if (lockfile && lockfile[0])
	{
		lfp = open (lockfile, O_RDWR | O_CREAT, 0640);
		if (lfp < 0)
		{
			syslog (LOG_ERR, "unable to create lock file %s, code=%d (%s)",
				lockfile, errno, strerror (errno));
			exit (EXIT_FAILURE);
		}
	}

	/* Drop user if there is one, and we were run as root */
	if (getuid () == 0 || geteuid () == 0)
	{
		struct passwd *pw = getpwnam (RUN_AS_USER);
		if (pw)
		{
			syslog (LOG_NOTICE, "setting user to " RUN_AS_USER);
			setuid (pw->pw_uid);
		}
	}

	/* Trap signals that we expect to receive */
	signal (SIGCHLD, child_handler);
	signal (SIGUSR1, child_handler);
	signal (SIGALRM, child_handler);

	/* Fork off the parent process */
	pid = fork ();
	if (pid < 0)
	{
		syslog (LOG_ERR, "unable to fork daemon, code=%d (%s)",
			errno, strerror (errno));
		exit (EXIT_FAILURE);
	}
	/* If we got a good PID, then we can exit the parent process. */
	if (pid > 0)
	{

		/* Wait for confirmation from the child via SIGTERM or SIGCHLD, or
		   for two seconds to elapse (SIGALRM).  pause() should not return. */
		alarm (2);
		pause ();

		exit (EXIT_FAILURE);
	}

	/* At this point we are executing as the child process */
	parent = getppid ();

	/* Cancel certain signals */
	signal (SIGCHLD, SIG_DFL);	/* A child process dies */
	signal (SIGTSTP, SIG_IGN);	/* Various TTY signals */
	signal (SIGTTOU, SIG_IGN);
	signal (SIGTTIN, SIG_IGN);
	signal (SIGHUP, SIG_IGN);	/* Ignore hangup signal */
	signal (SIGTERM, SIG_DFL);	/* Die on SIGTERM */

	/* Change the file mode mask */
	umask (0);

	/* Create a new SID for the child process */
	sid = setsid ();
	if (sid < 0)
	{
		syslog (LOG_ERR, "unable to create a new session, code %d (%s)",
			errno, strerror (errno));
		exit (EXIT_FAILURE);
	}

	pFile = fopen (pidfile,"w");
	if (pFile < 0)
	{
		syslog (LOG_ERR, "unable to create pid file %s, code=%d (%s)",
			pidfile, errno, strerror (errno));
		exit (EXIT_FAILURE);
	}
	fprintf (pFile, "%d\n", sid);
	fclose (pFile);

	/* Change the current working directory.  This prevents the current
	   directory from being locked; hence not being able to remove it. */
	if ((chdir ("/")) < 0)
	{
		syslog (LOG_ERR, "unable to change directory to %s, code %d (%s)",
			"/", errno, strerror (errno));
		exit (EXIT_FAILURE);
	}

	/* Redirect standard files to /dev/null */
	freopen ("/dev/null", "r", stdin);
	freopen ("/dev/null", "w", stdout);
	freopen ("/dev/null", "w", stderr);

	/* Tell the parent process that we are A-okay */
	kill (parent, SIGUSR1);
}

int main (int argc, char *argv[])
{
	char *tty;
	char *ttypath;
	char const *devprefix = "/dev/";

	/* Initialize the logging interface */
	openlog (DAEMON_NAME, LOG_PID, LOG_LOCAL5);

	/* See how we're called */
	if (argc != 2)
		print_usage(argv[0]);
	tty = argv[1];

	/* Prepare the tty device name string */
	char * pch;
	pch = strstr (tty, devprefix);
	if (pch == tty) {
		print_usage(argv[0]);
	}
	ttypath = malloc((strlen(devprefix) + strlen(tty)) * sizeof(char));
	sprintf (ttypath, "%s%s", devprefix, tty);
	syslog (LOG_INFO, "starting on TTY device %s", ttypath);

	/* Daemonize */
	daemonize ("/var/lock/" DAEMON_NAME, tty);

	/* Now we are a daemon -- do the work for which we were paid */
	int fd;
	int ldisc = LDISC_N_SLCAN;

	if ((fd = open (ttypath, O_WRONLY | O_NOCTTY )) < 0) {
		perror(ttypath);
		exit(1);
	}

	if (ioctl (fd, TIOCSETD, &ldisc) < 0) {
		perror("ioctl");
		exit(1);
	}

	/* The Big Loop */
	while (1) {
		sleep(60); /* wait 60 seconds */
	}

	/* Finish up */
	syslog (LOG_NOTICE, "terminated on %s", ttypath);
	closelog ();
	return 0;
}
