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
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <linux/types.h>
#include <linux/ppp_defs.h> 
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include "bittest.h"

#ifdef STANDALONE_ZAPATA
#include "kernel/zaptel.h"
#else
#include <zaptel/zaptel.h>
#endif

/* #define BLOCK_SIZE 2048 */
#define BLOCK_SIZE 2041

void print_packet(unsigned char *buf, int len)
{
	int x;
	printf("{ ");
	for (x=0;x<len;x++)
		printf("%02x ",buf[x]);
	printf("}\n");
}

int main(int argc, char *argv[])
{
	int fd;
	int res, res1, x;
	ZT_PARAMS tp;
	int bs = BLOCK_SIZE;
	unsigned char c=0;
	unsigned char outbuf[BLOCK_SIZE];
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <tor device>\n", argv[0]);
		exit(1);
	}
	fd = open(argv[1], O_RDWR, 0600);
	if (fd < 0) {
		fprintf(stderr, "Unable to open %s: %s\n", argv[1], strerror(errno));
		exit(1);
	}
	if (ioctl(fd, ZT_SET_BLOCKSIZE, &bs)) {
		fprintf(stderr, "Unable to set block size to %d: %s\n", bs, strerror(errno));
		exit(1);
	}
	if (ioctl(fd, ZT_GET_PARAMS, &tp)) {
		fprintf(stderr, "Unable to get channel parameters\n");
		exit(1);
	}
	ioctl(fd, ZT_GETEVENT);
#if 0
	print_packet(outbuf, res);
	printf("FCS is %x, PPP_GOODFCS is %x\n",
	fcs,PPP_GOODFCS);
#endif
	for(;;) {
		res = bs;
		for (x=0;x<bs;x++) {
			outbuf[x] = c;
			c = bit_next(c);
		}
		res1 = write(fd, outbuf, res);
		if (res1 < res) {
			int e;
			ZT_SPANINFO zi;
			res = ioctl(fd,ZT_GETEVENT,&e);
			if (res == -1)
			{
				perror("ZT_GETEVENT");
				exit(1);
			}
			if (e == ZT_EVENT_NOALARM)
				printf("ALARMS CLEARED\n");
			if (e == ZT_EVENT_ALARM)
			{
				zi.spanno = 0;
				res = ioctl(fd,ZT_SPANSTAT,&zi);
				if (res == -1)
				{
					perror("ZT_SPANSTAT");
					exit(1);
				}
				printf("Alarm mask %x hex\n",zi.alarms);
			}
			continue;
		}
#if 0
		printf("(%d) Wrote %d bytes\n", packets++, res);
#endif
	}
	
}
