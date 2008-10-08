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
#include "kernel/zaptel.h"
#include "tonezone.h"
#include "kernel/wctdm.h"

static int tones[] = {
	ZT_TONE_DIALTONE,
	ZT_TONE_BUSY,
	ZT_TONE_RINGTONE,
	ZT_TONE_CONGESTION,
	ZT_TONE_DIALRECALL,
};

int main(int argc, char *argv[])
{
	int fd;
	int res;
	int x;
	if (argc < 3) {
		fprintf(stderr, "Usage: fxstest <zap device> <cmd>\n"
		       "       where cmd is one of:\n"
		       "       stats - reports voltages\n"
		       "       regdump - dumps ProSLIC registers\n"
		       "       tones - plays a series of tones\n"
		       "       polarity - tests polarity reversal\n"
		       "       ring - rings phone\n");
		exit(1);
	}
	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Unable to open %s: %s\n", argv[1], strerror(errno));
		exit(1);
	}
	if (!strcasecmp(argv[2], "ring")) {
		fprintf(stderr, "Ringing phone...\n");
		x = ZT_RING;
		res = ioctl(fd, ZT_HOOK, &x);
		if (res) {
			fprintf(stderr, "Unable to ring phone...\n");
		} else {
			fprintf(stderr, "Phone is ringing...\n");
			sleep(2);
		}
	} else if (!strcasecmp(argv[2], "polarity")) {
		fprintf(stderr, "Twiddling polarity...\n");
		x = 0;
		res = ioctl(fd, ZT_SETPOLARITY, &x);
		if (res) {
			fprintf(stderr, "Unable to polarity...\n");
		} else {
			fprintf(stderr, "Polarity is forward...\n");
			sleep(2);
			x = 1;
			ioctl(fd, ZT_SETPOLARITY, &x);
			fprintf(stderr, "Polarity is reversed...\n");
			sleep(5);
			x = 0;
			ioctl(fd, ZT_SETPOLARITY, &x);
			fprintf(stderr, "Polarity is forward...\n");
			sleep(2);
		}
	} else if (!strcasecmp(argv[2], "tones")) {
		int x = 0;
		for (;;) {
			res = tone_zone_play_tone(fd, tones[x]);
			if (res)
				fprintf(stderr, "Unable to play tone %d\n", tones[x]);
			sleep(3);
			x=(x+1) % (sizeof(tones) / sizeof(tones[0]));
		}
	} else if (!strcasecmp(argv[2], "stats")) {
		struct wctdm_stats stats;
		res = ioctl(fd, WCTDM_GET_STATS, &stats);
		if (res) {
			fprintf(stderr, "Unable to get stats on channel %s\n", argv[1]);
		} else {
			printf("TIP: %7.4f Volts\n", (float)stats.tipvolt / 1000.0);
			printf("RING: %7.4f Volts\n", (float)stats.ringvolt / 1000.0);
			printf("VBAT: %7.4f Volts\n", (float)stats.batvolt / 1000.0);
		}
	} else if (!strcasecmp(argv[2], "regdump")) {
		struct wctdm_regs regs;
		int numregs = NUM_REGS;
		memset(&regs, 0, sizeof(regs));
		res = ioctl(fd, WCTDM_GET_REGS, &regs);
		if (res) {
			fprintf(stderr, "Unable to get registers on channel %s\n", argv[1]);
		} else {
			for (x=60;x<NUM_REGS;x++) {
				if (regs.direct[x])
					break;
			}
			if (x == NUM_REGS) 
				numregs = 60;
			printf("Direct registers: \n");
			for (x=0;x<numregs;x++) {
				printf("%3d. %02x  ", x, regs.direct[x]);
				if ((x % 8) == 7)
					printf("\n");
			}
			if (numregs == NUM_REGS) {
				printf("\n\nIndirect registers: \n");
				for (x=0;x<NUM_INDIRECT_REGS;x++) {
					printf("%3d. %04x  ", x, regs.indirect[x]);
					if ((x % 6) == 5)
						printf("\n");
				}
			}
			printf("\n\n");
		}
	} else if (!strcasecmp(argv[2], "setdirect") ||
				!strcasecmp(argv[2], "setindirect")) {
		struct wctdm_regop regop;
		int val;
		int reg;
		if ((argc < 5) || (sscanf(argv[3], "%i", &reg) != 1) ||
			(sscanf(argv[4], "%i", &val) != 1)) {
			fprintf(stderr, "Need a register and value...\n");
		} else {
			regop.reg = reg;
			regop.val = val;
			if (!strcasecmp(argv[2], "setindirect")) {
				regop.indirect = 1;
				regop.val &= 0xff;
			} else {
				regop.indirect = 0;
			}
			res = ioctl(fd, WCTDM_SET_REG, &regop);
			if (res) 
				fprintf(stderr, "Unable to get registers on channel %s\n", argv[1]);
			else
				printf("Success.\n");
		}
	} else
		fprintf(stderr, "Invalid command\n");
	close(fd);
	return 0;
}
