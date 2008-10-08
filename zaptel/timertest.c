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

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/time.h>
#include <errno.h>

#ifdef STANDALONE_ZAPATA
#include "kernel/zaptel.h"
#else
#include <zaptel/zaptel.h>
#endif

int main(int argc, char *argv[])
{
	int fd;
	int x = 8000;
	int res;
	fd_set fds;
	struct timeval orig, now;
	fd = open("/dev/zap/timer", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Unable to open timer: %s\n", strerror(errno));
		exit(1);
	}
	printf("Opened timer...\n");
	if (ioctl(fd, ZT_TIMERCONFIG, &x)) {
		fprintf(stderr, "Unable to set timer: %s\n", strerror(errno));
		exit(1);
	}
	printf("Set timer duration to %d samples (%d ms)\n", x, x/8);
	printf("Waiting...\n");
	gettimeofday(&orig, NULL);
	for(;;) {
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		res = select(fd + 1, NULL, NULL, &fds, NULL);
		if (res != 1) {
			fprintf(stderr, "Unexpected result %d: %s\n", res, strerror(errno));
			exit(1);
		}
		x = -1;
		if (ioctl(fd, ZT_TIMERACK, &x)) {
			fprintf(stderr, "Unable to ack timer: %s\n", strerror(errno));
			exit(1);
		}
		gettimeofday(&now, NULL);
		printf("Timer Expired (%ld ms)!\n", (now.tv_sec - orig.tv_sec) * 1000 + (now.tv_usec - orig.tv_usec) / 1000);
	}
	exit(0);
}
