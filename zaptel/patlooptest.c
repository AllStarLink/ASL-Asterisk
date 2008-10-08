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
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#ifdef STANDALONE_ZAPATA
#include "kernel/zaptel.h"
#else
#include <zaptel/zaptel.h>
#endif

#define BLOCK_SIZE 2039

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
	int res, x;
	int i;
	ZT_PARAMS tp;
	int bs = BLOCK_SIZE;
	int skipcount = 10;
	unsigned char c=0,c1=0;
	unsigned char inbuf[BLOCK_SIZE];
	unsigned char outbuf[BLOCK_SIZE];
	int setup=0;
	int errors=0;
	int bytes=0;
	int timeout=0;
	time_t start_time=0;
	if (argc < 2 || argc > 3 ) {
		fprintf(stderr, "Usage: %s <zaptel device> [timeout]\n",argv[0]);
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

	i = ZT_FLUSH_ALL;
	if (ioctl(fd,ZT_FLUSH,&i) == -1)
	   {
		perror("tor_flush");
		exit(255);
	   }
	if(argc==3){
		timeout=atoi(argv[2]);
		start_time=time(NULL);
		printf("Using Timeout of %d Seconds\n",timeout);
	}

	for(;;) {
		res = bs;
		for (x=0;x<bs;x++) 
			outbuf[x] = c1++;

		res = write(fd,outbuf,bs);
		if (res != bs)
		   {
			printf("Res is %d: %s\n", res, strerror(errno));
			ioctl(fd, ZT_GETEVENT, &x);
			printf("Event: %d\n", x);
			exit(1);
		}

		if (skipcount)
		   {
			if (skipcount > 1) read(fd,inbuf,bs);
			skipcount--;
			if (!skipcount) puts("Going for it...");
			continue;
		   }

		res = read(fd, inbuf, bs);
		if (res < bs) {
			printf("Res is %d\n", res);
			exit(1);
		}
		if (!setup) {
			c = inbuf[0];
			setup++;
		}
		for (x=0;x<bs;x++)  {
			if (inbuf[x] != c) {
				printf("(Error %d): Unexpected result, %d != %d, %d bytes since last error.\n", ++errors, inbuf[x],c, bytes);
				c = inbuf[x];
				bytes=0;
			}
			c++;
			bytes++;
		}
#if 0
		printf("(%d) Wrote %d bytes\n", packets++, res);
#endif
		if(timeout && (time(NULL)-start_time)>timeout){
			printf("Timeout achieved Ending Program\n");
			return errors;
		}
	}
	
}
