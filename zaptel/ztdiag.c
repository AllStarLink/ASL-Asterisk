/*
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 */

#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef STANDALONE_ZAPATA
#include "kernel/zaptel.h"
#else
#include <zaptel/zaptel.h>
#endif

int main(int argc, char *argv[])
{
	int fd;
	int chan;
	if ((argc < 2) || (sscanf(argv[1], "%d", &chan) != 1)) {
		fprintf(stderr, "Usage: ztdiag <channel>\n");
		exit(1);
	}
	fd = open("/dev/zap/ctl", O_RDWR);
	if (fd < 0) {
		perror("open(/dev/zap/ctl");
		exit(1);
	}
	if (ioctl(fd, ZT_CHANDIAG, &chan)) {
		perror("ioctl(ZT_CHANDIAG)");
		exit(1);
	}
	exit(0);
}
