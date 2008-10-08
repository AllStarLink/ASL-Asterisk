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
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "zap.h"
#include <zaptel.h>

int main(int argc, char *argv[])
{
	ZAP *z;
	ZT_PARAMS p;
	char tmp[1024];
	int len;
	int res;
	int firstpass=1;
	int linear=0;
	if (argc < 2) {
		fprintf(stderr, "Usage: usbfxstest <device> [options]\n");
		exit(1);
	}
	if (argc > 2) {
		if (!strcasecmp(argv[2], "linear")) {
			linear=1;
		}
	}
	z = zap_open(argv[1], 0);
	if (!z) {
		fprintf(stderr, "Unable to open %s: %s\n", argv[1], strerror(errno));
		exit(1);
	}
	/* Ring phone until it goes off hook.  This shows how you mix direct
	   and indirect calls */
	for (;;) {
		p.channo = 0;
		res = ioctl(zap_fd(z), ZT_GET_PARAMS, &p);
		if (res) {
			fprintf(stderr, "Failed to get parameters: %s\n", strerror(errno));
			exit(1);
		}
		if (p.rxisoffhook)
			break;
		if (firstpass)
			res = zap_ringclid(z, "2565551212", "Nifty Cool");
		else
			res = zap_ring(z, 1);
	}
	if (linear) {
		printf("Going linear!\n");
		zap_setlinear(z, linear);
	}
	printf("Off Hook!\n");
	len = 204;
	for (;;) {
		/* Record, play, and check for events */
		res = zap_recchunk(z, tmp, len, ZAP_DTMFINT);
		if (res == len) {
			res = zap_playchunk(z, tmp, len, 0);
		}
		if (res < len) {
			res = zap_getevent(z);
			if (zap_dtmfwaiting(z)) {
				zap_getdtmf(z, 1, NULL, 0, 1, 1, 0);                    
				printf("Got DTMF: %s\n", zap_dtmfbuf(z));
				zap_clrdtmfn(z);
			} else if (res) {
				switch(res) {
				case ZAP_EVENT_ONHOOK:
					printf("On Hook!\n");
					break;
				case ZAP_EVENT_RINGANSWER:
					printf("Off hook!\n");
					break;
				case ZAP_EVENT_WINKFLASH:
					printf("Flash!\n");
					break;
				default:
					printf("Unknown event %d\n", res);
				}
			}
		}
	}
	return 0;
}
