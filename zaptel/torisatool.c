/*
 * BSD Telephony Of Mexico "Tormenta" card LINUX driver, version 1.8 4/8/01
 * 
 * Working with the "Tormenta ISA" Card 
 *
 * Modified from original tor.c by Mark Spencer <markster@digium.com>
 *                     original by Jim Dixon <jim@lambdatel.com>
 */

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

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include "kernel/zaptel.h"

static void usage(void)
{
	fprintf(stderr, "Usage: torisatool <dev> showerrors\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	int fd;
	struct torisa_debug td;
	int res;
	if (argc < 3) 
		usage();
	
	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Unable to open %s: %s\n", argv[1], strerror(errno));
		exit(1);
	}
	if (!strcasecmp(argv[2], "showerrors")) {
		res = ioctl(fd, TORISA_GETDEBUG, &td);
		if (res) {
			fprintf(stderr, "IOCTL failed: %s\n", strerror(errno));
			exit(1);
		}
		printf("Recorded misses: %u\n", td.txerrors);
		printf("IRQ execs: %u\n", td.irqcount);
		printf("Tasklet Schedules: %u\n", td.taskletsched);
		printf("Tasklets Run: %u\n", td.taskletrun);
		printf("Tasklets Executed: %u\n", td.taskletexec);
	} else 
		usage();
	exit(0);
}
