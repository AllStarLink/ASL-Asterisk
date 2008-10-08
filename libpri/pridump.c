/*
 * libpri: An implementation of Primary Rate ISDN
 *
 * Written by Mark Spencer <markster@digium.com>
 *
 * Copyright (C) 2001-2005, Digium, Inc.
 * All Rights Reserved.
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
 *
 * In addition, when this program is distributed with Asterisk in
 * any form that would qualify as a 'combined work' or as a
 * 'derivative work' (but not mere aggregation), you can redistribute
 * and/or modify the combination under the terms of the license
 * provided with that copy of Asterisk, instead of the license
 * terms granted here.
 */

/*
 * This program tests libpri call reception using a zaptel interface.
 * Its state machines are setup for RECEIVING CALLS ONLY, so if you
 * are trying to both place and receive calls you have to a bit more.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <zaptel/zaptel.h>
#include "libpri.h"
#include "pri_q921.h"
#include "pri_q931.h"

static int pri_open(char *dev)
{
	int dfd;
	struct zt_params p;
	
	dfd = open(dev, O_RDWR);
	if (dfd < 0) {
		fprintf(stderr, "Failed to open dchannel '%s': %s\n", dev, strerror(errno));
		return -1;
	}
	if (ioctl(dfd, ZT_GET_PARAMS, &p)) {
		fprintf(stderr, "Unable to get parameters on '%s': %s\n", dev, strerror(errno));
		return -1;
	}
	if ((p.sigtype != ZT_SIG_HDLCRAW) && (p.sigtype != ZT_SIG_HDLCFCS)) {
		fprintf(stderr, "%s is in %d signalling, not FCS HDLC or RAW HDLC mode\n", dev, p.sigtype);
		return -1;
	}
	return dfd;
}

static void dump_packet(struct pri *pri, char *buf, int len, int txrx)
{
	q921_h *h = (q921_h *)buf;
	q921_dump(pri, h, len, 1, txrx);
	if (!((h->h.data[0] & Q921_FRAMETYPE_MASK) & 0x3)) {
		q931_dump(pri, (q931_h *)(h->i.data), len - 4 - 2 /* FCS */, txrx);
	}
	fflush(stdout);
	fflush(stderr);
}


static int pri_bridge(int d1, int d2)
{
	char buf[1024];
	fd_set fds;
	int max;
	int e;
	int res;
	for(;;) {
		FD_ZERO(&fds);
		FD_SET(d1, &fds);
		FD_SET(d2, &fds);
		max = d1;
		if (max < d2)
			max = d2;
		ioctl(d1, ZT_GETEVENT, &e);
		ioctl(d2, ZT_GETEVENT, &e);
		res = select(max + 1, &fds, NULL, NULL, NULL);
		if (res < 0) {
			fprintf(stderr, "Select returned %d: %s\n", res, strerror(errno));
			continue;
		};
		if (FD_ISSET(d1, &fds)) {
			/* Copy from d1 to d2 */
			res = read(d1, buf, sizeof(buf));
			dump_packet((struct pri *)NULL, buf, res, 1);
			res = write(d2, buf, res);
		}
		if (FD_ISSET(d2, &fds)) {
			/* Copy from d2 to d1 */
			res = read(d2, buf, sizeof(buf));
			dump_packet((struct pri *)NULL, buf, res, 0);
			res = write(d1, buf, res);
		}
	}
}

static void my_pri_message(struct pri *pri, char *stuff)
{
	fprintf(stdout, "%s", stuff);
}

static void my_pri_error(struct pri *pri, char *stuff)
{
	fprintf(stderr, "%s", stuff);
}

int main(int argc, char *argv[])
{
	int d1, d2;
	
	if (argc < 3) {
		fprintf(stderr, "Usage: pridump <dev1> <dev2>\n");
		exit(1);
	}
	
	pri_set_message(my_pri_message);
	pri_set_error(my_pri_error);
	
	d1 = pri_open(argv[1]);
	if (d1 < 0)
		exit(1);
	d2 = pri_open(argv[2]);
	if (d2 < 0)
		exit(1);
	pri_bridge(d1, d2);
	return 0;
}
