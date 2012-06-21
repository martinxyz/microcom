/******************************************************************
** File: serial.c
** Description: the serial part for microcom project
**
** Copyright (C)1999 Anca and Lucian Jurubita <ljurubita@hotmail.com>.
** All rights reserved.
****************************************************************************
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details at www.gnu.org
****************************************************************************
** Rev. 1.0 - Feb. 2000
** Rev. 1.01 - March 2000
** Rev. 1.02 - June 2000
****************************************************************************/

#include <limits.h>
#include <arpa/telnet.h>

#include "microcom.h"

int crnl_mapping;		//0 - no mapping, 1 mapping
struct termios pots;		/* old port termios settings to restore */
char *lockfile;

static void init_comm(struct termios *pts)
{
	/* some things we want to set arbitrarily */
	pts->c_lflag &= ~ICANON;
	pts->c_lflag &= ~(ECHO | ECHOCTL | ECHONL);
	pts->c_cflag |= HUPCL;
	pts->c_iflag |= IGNBRK;
	pts->c_cc[VMIN] = 1;
	pts->c_cc[VTIME] = 0;

	/* Standard CR/LF handling: this is a dumb terminal.
	 * Do no translation:
	 *  no NL -> CR/NL mapping on output, and
	 *  no CR -> NL mapping on input.
	 */
	pts->c_oflag &= ~ONLCR;
	crnl_mapping = 0;
	pts->c_iflag &= ~ICRNL;
}

static int serial_set_speed(struct ios_ops *ios, speed_t speed)
{
	struct termios pts;	/* termios settings on port */

	tcgetattr(ios->fd, &pts);
	cfsetospeed(&pts, speed);
	cfsetispeed(&pts, speed);
	tcsetattr(ios->fd, TCSANOW, &pts);

	return 0;
}

static int serial_set_flow(struct ios_ops *ios, int flow)
{
	struct termios pts;	/* termios settings on port */
	tcgetattr(ios->fd, &pts);

	switch (flow) {
	case FLOW_NONE:
		/* no flow control */
		pts.c_cflag &= ~CRTSCTS;
		pts.c_iflag &= ~(IXON | IXOFF | IXANY);
		break;
	case FLOW_HARD:
		/* hardware flow control */
		pts.c_cflag |= CRTSCTS;
		pts.c_iflag &= ~(IXON | IXOFF | IXANY);
		break;
	case FLOW_SOFT:
		/* software flow control */
		pts.c_cflag &= ~CRTSCTS;
		pts.c_iflag |= IXON | IXOFF | IXANY;
		break;
	}

	tcsetattr(ios->fd, TCSANOW, &pts);

	return 0;
}

static int serial_send_break(struct ios_ops *ios)
{
	tcsendbreak(ios->fd, 0);

	return 0;
}

/* unlink the lockfile */
static void serial_unlock()
{
	if (lockfile)
		unlink(lockfile);
}

/* restore original terminal settings on exit */
static void serial_exit(struct ios_ops *ios)
{
	tcsetattr(ios->fd, TCSANOW, &pots);
	close(ios->fd);
	free(ios);
	serial_unlock();
}

struct ios_ops * serial_init(char *device)
{
	struct termios pts;	/* termios settings on port */
	struct ios_ops *ops;
	int fd;
	char *substring;
	long pid;
	int res;

	ops = malloc(sizeof(*ops));
	if (!ops)
		return NULL;
	lockfile = malloc(PATH_MAX);
	if (!lockfile)
		return NULL;

	ops->set_speed = serial_set_speed;
	ops->set_flow = serial_set_flow;
	ops->send_break = serial_send_break;
	ops->exit = serial_exit;

	/* check lockfile */
	substring = strrchr(device, '/');
	if (substring)
		substring++;
	else
		substring = device;

	sprintf(lockfile, "/var/lock/LCK..%s", substring);

	fd = open(lockfile, O_RDONLY);
	if (fd >= 0 && !opt_force) {
		res = read(fd, &pid, sizeof(long));
		if (res != sizeof(long)) pid = 0;
		close(fd);
		fprintf(stderr, "%s is already in use by PID %d!\n", device, pid);
		fprintf(stderr, "(use -f to continue anyway)\n");
		exit(3);
	}

	if (fd >= 0 && opt_force) {
		close(fd);
		printf("lockfile for port exists, ignoring\n");
		serial_unlock();
	}

	fd = open(lockfile, O_RDWR | O_CREAT, 0444);
	if (fd < 0 && opt_force) {
		printf("cannot create lockfile. ignoring\n");
		lockfile = NULL;
		goto force;
	}
	if (fd < 0)
		main_usage(3, "cannot create lockfile", device);
	/* Kermit wants binary pid */
	pid = getpid();
	write(fd, &pid, sizeof(long));
	close(fd);
force:
	/* open the device */
	fd = open(device, O_RDWR);
	ops->fd = fd;

	if (fd < 0) {
		serial_unlock();
		main_usage(2, "cannot open device", device);
	}

	/* modify the port configuration */
	tcgetattr(fd, &pts);
	memcpy(&pots, &pts, sizeof (pots));
	init_comm(&pts);
	tcsetattr(fd, TCSANOW, &pts);
	printf("connected to %s\n", device);

	return ops;
}

