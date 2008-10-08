/*
 * Configuration program for Zapata Telephony Interface
 *
 * Written by Mark Spencer <markster@digium.com>
 * Based on previous works, designs, and architectures conceived and
 * written by Jim Dixon <jim@lambdatel.com>.
 * Radio Support by Jim Dixon <jim@lambdatel.com>
 *
 * Copyright (C) 2001 Jim Dixon / Zapata Telephony.
 * Copyright (C) 2001 Linux Support Services, Inc.
 *
 * All rights reserved.
 *
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
#include <getopt.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#ifdef STANDALONE_ZAPATA
#include "kernel/zaptel.h"
#include "tonezone.h"
#else
#include <zaptel/zaptel.h>
#include <zaptel/tonezone.h>
#endif
#include "ztcfg.h"

#define NUM_SPANS ZT_MAX_SPANS

#define NUM_TONES 15

/* Assume no more than 1024 dynamics */
#define NUM_DYNAMIC	1024

static int lineno=0;

static FILE *cf;

static char *filename=CONFIG_FILENAME;

int rxtones[NUM_TONES + 1],rxtags[NUM_TONES + 1],txtones[NUM_TONES + 1];
int bursttime = 0, debouncetime = 0, invertcor = 0, exttone = 0, corthresh = 0;
int txgain = 0, rxgain = 0, deemp = 0, preemp = 0;

int corthreshes[] = {3125,6250,9375,12500,15625,18750,21875,25000,0} ;

static int toneindex = 1;

#define DEBUG_READER (1 << 0)
#define DEBUG_PARSER (1 << 1)
#define DEBUG_APPLY  (1 << 2)
static int debug = 0;

static int errcnt = 0;

static int deftonezone = -1;

static struct zt_lineconfig lc[ZT_MAX_SPANS];

static struct zt_chanconfig cc[ZT_MAX_CHANNELS];

static struct zt_dynamic_span zds[NUM_DYNAMIC];

static const char *sig[ZT_MAX_CHANNELS];		/* Signalling */

static int slineno[ZT_MAX_CHANNELS];	/* Line number where signalling specified */

static int spans=0;

static int fo_real = 1;

static int verbose = 0;

static int force = 0;

static int stopmode = 0;

static int numdynamic = 0;

static char zonestoload[ZT_TONE_ZONE_MAX][10];

static int numzones = 0;

static int fd = -1;

static const char *lbostr[] = {
"0 db (CSU)/0-133 feet (DSX-1)",
"133-266 feet (DSX-1)",
"266-399 feet (DSX-1)",
"399-533 feet (DSX-1)",
"533-655 feet (DSX-1)",
"-7.5db (CSU)",
"-15db (CSU)",
"-22.5db (CSU)"
};

static const char *laws[] = {
	"Default",
	"Mu-law",
	"A-law"
};

static const char *sigtype_to_str(const int sig)
{
	switch (sig) {
	case 0:
		return "Unused";
	case ZT_SIG_EM:
		return "E & M";
	case ZT_SIG_EM_E1:
		return "E & M E1";
	case ZT_SIG_FXSLS:
		return "FXS Loopstart";
	case ZT_SIG_FXSGS:
		return "FXS Groundstart";
	case ZT_SIG_FXSKS:
		return "FXS Kewlstart";
	case ZT_SIG_FXOLS:
		return "FXO Loopstart";
	case ZT_SIG_FXOGS:
		return "FXO Groundstart";
	case ZT_SIG_FXOKS:
		return "FXO Kewlstart";
	case ZT_SIG_CAS:
		return "CAS / User";
	case ZT_SIG_DACS:
		return "DACS";
	case ZT_SIG_DACS_RBS:
		return "DACS w/RBS";
	case ZT_SIG_CLEAR:
		return "Clear channel";
	case ZT_SIG_SLAVE:
		return "Slave channel";
	case ZT_SIG_HDLCRAW:
		return "Raw HDLC";
	case ZT_SIG_HDLCNET:
		return "Network HDLC";
	case ZT_SIG_HDLCFCS:
		return "HDLC with FCS check";
	case ZT_SIG_HARDHDLC:
		return "Hardware assisted D-channel";
	case ZT_SIG_MTP2:
		return "MTP2";
	default:
		return "Unknown";
	}
}

int ind_ioctl(int channo, int fd, int op, void *data)
{
ZT_INDIRECT_DATA ind;

	ind.chan = channo;
	ind.op = op;
	ind.data = data;
	return ioctl(fd,ZT_INDIRECT,&ind);
}

static void clear_fields()
{

	memset(rxtones,0,sizeof(rxtones));
	memset(rxtags,0,sizeof(rxtags));
	memset(txtones,0,sizeof(txtones));
	bursttime = 0;
	debouncetime = 0;
	invertcor = 0;
	exttone = 0;
	txgain = 0;
	rxgain = 0;
	deemp = 0;
	preemp = 0;
}

static int error(char *fmt, ...)
{
	int res;
	static int shown=0;
	va_list ap;
	if (!shown) {
		fprintf(stderr, "Notice: Configuration file is %s\n", filename);
		shown++;
	}
	res = fprintf(stderr, "line %d: ", lineno);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	errcnt++;
	return res;
}

static void trim(char *buf)
{
	/* Trim off trailing spaces, tabs, etc */
	while(strlen(buf) && (buf[strlen(buf) -1] < 33))
		buf[strlen(buf) -1] = '\0';
}

static int parseargs(char *input, char *output[], int maxargs, char sep)
{
	char *c;
	int pos=0;
	c = input;
	output[pos++] = c;
	while(*c) {
		while(*c && (*c != sep)) c++;
		if (*c) {
			*c = '\0';
			c++;
			while(*c && (*c < 33)) c++;
			if (*c)  {
				if (pos >= maxargs)
					return -1;
				output[pos] = c;
				trim(output[pos]);
				pos++;
				output[pos] = NULL;
				/* Return error if we have too many */
			} else
				return pos;
		}
	}
	return pos;
}

int dspanconfig(char *keyword, char *args)
{
	static char *realargs[10];
	int argc;
	int res;
	int chans;
	int timing;
	argc = res = parseargs(args, realargs, 4, ',');
	if (res != 4) {
		error("Incorrect number of arguments to 'dynamic' (should be <driver>,<address>,<num channels>, <timing>)\n");
	}
	res = sscanf(realargs[2], "%d", &chans);
	if ((res == 1) && (chans < 1))
		res = -1;
	if (res != 1) {
		error("Invalid number of channels '%s', should be a number > 0.\n", realargs[2]);
	}

	res = sscanf(realargs[3], "%d", &timing);
	if ((res == 1) && (timing < 0))
		res = -1;
	if (res != 1) {
		error("Invalid timing '%s', should be a number > 0.\n", realargs[3]);
	}


	zap_copy_string(zds[numdynamic].driver, realargs[0], sizeof(zds[numdynamic].driver));
	zap_copy_string(zds[numdynamic].addr, realargs[1], sizeof(zds[numdynamic].addr));
	zds[numdynamic].numchans = chans;
	zds[numdynamic].timing = timing;
	
	numdynamic++;
	return 0;
}

int spanconfig(char *keyword, char *args)
{
	static char *realargs[10];
	int res;
	int argc;
	int span;
	int timing;
	argc = res = parseargs(args, realargs, 7, ',');
	if ((res < 5) || (res > 7)) {
		error("Incorrect number of arguments to 'span' (should be <spanno>,<timing>,<lbo>,<framing>,<coding>[, crc4 | yellow [, yellow]])\n");
	}
	res = sscanf(realargs[0], "%i", &span);
	if (res != 1) {
		error("Span number should be a valid span number, not '%s'\n", realargs[0]);
		return -1;
	}
	res = sscanf(realargs[1], "%i", &timing);
	if ((res != 1) || (timing < 0) || (timing > 15)) {
		error("Timing should be a number from 0 to 15, not '%s'\n", realargs[1]);
		return -1;
	}
	res = sscanf(realargs[2], "%i", &lc[spans].lbo);
	if (res != 1) {
		error("Line build-out (LBO) should be a number from 0 to 7 (usually 0) not '%s'\n", realargs[2]);
		return -1;
	}
	if ((lc[spans].lbo < 0) || (lc[spans].lbo > 7)) {
		error("Line build-out should be in the range 0 to 7, not %d\n", lc[spans].lbo);
		return -1;
	}
	if (!strcasecmp(realargs[3], "d4")) {
		lc[spans].lineconfig |= ZT_CONFIG_D4;
		lc[spans].lineconfig &= ~ZT_CONFIG_ESF;
		lc[spans].lineconfig &= ~ZT_CONFIG_CCS;
	} else if (!strcasecmp(realargs[3], "esf")) {
		lc[spans].lineconfig |= ZT_CONFIG_ESF;
		lc[spans].lineconfig &= ~ZT_CONFIG_D4;
		lc[spans].lineconfig &= ~ZT_CONFIG_CCS;
	} else if (!strcasecmp(realargs[3], "ccs")) {
		lc[spans].lineconfig |= ZT_CONFIG_CCS;
		lc[spans].lineconfig &= ~(ZT_CONFIG_ESF | ZT_CONFIG_D4);
	} else if (!strcasecmp(realargs[3], "cas")) {
		lc[spans].lineconfig &= ~ZT_CONFIG_CCS;
		lc[spans].lineconfig &= ~(ZT_CONFIG_ESF | ZT_CONFIG_D4);
	} else {
		error("Framing(T1)/Signalling(E1) must be one of 'd4', 'esf', 'cas' or 'ccs', not '%s'\n", realargs[3]);
		return -1;
	}
	if (!strcasecmp(realargs[4], "ami")) {
		lc[spans].lineconfig &= ~(ZT_CONFIG_B8ZS | ZT_CONFIG_HDB3);
		lc[spans].lineconfig |= ZT_CONFIG_AMI;
	} else if (!strcasecmp(realargs[4], "b8zs")) {
		lc[spans].lineconfig |= ZT_CONFIG_B8ZS;
		lc[spans].lineconfig &= ~(ZT_CONFIG_AMI | ZT_CONFIG_HDB3);
	} else if (!strcasecmp(realargs[4], "hdb3")) {
		lc[spans].lineconfig |= ZT_CONFIG_HDB3;
		lc[spans].lineconfig &= ~(ZT_CONFIG_AMI | ZT_CONFIG_B8ZS);
	} else {
		error("Coding must be one of 'ami', 'b8zs' or 'hdb3', not '%s'\n", realargs[4]);
		return -1;
	}
	if (argc > 5) {
		if (!strcasecmp(realargs[5], "yellow"))
			lc[spans].lineconfig |= ZT_CONFIG_NOTOPEN;
		else if (!strcasecmp(realargs[5], "crc4")) {
			lc[spans].lineconfig |= ZT_CONFIG_CRC4;
		} else {
			error("Only valid fifth arguments are 'yellow' or 'crc4', not '%s'\n", realargs[5]);
			return -1;
		}
	}
	if (argc > 6) {
		if (!strcasecmp(realargs[6], "yellow"))
			lc[spans].lineconfig |= ZT_CONFIG_NOTOPEN;
		else {
			error("Only valid sixth argument is 'yellow', not '%s'\n", realargs[6]);
			return -1;
		}
	}
	lc[spans].span = span;
	lc[spans].sync = timing;
	/* Valid span */
	spans++;
	return 0;
}

int apply_channels(int chans[], char *argstr)
{
	char *args[ZT_MAX_CHANNELS+1];
	char *range[3];
	int res,x, res2,y;
	int chan;
	int start, finish;
	char argcopy[256];
	res = parseargs(argstr, args, ZT_MAX_CHANNELS, ',');
	if (res < 0)
		error("Too many arguments...  Max is %d\n", ZT_MAX_CHANNELS);
	for (x=0;x<res;x++) {
		if (strchr(args[x], '-')) {
			/* It's a range */
			zap_copy_string(argcopy, args[x], sizeof(argcopy));
			res2 = parseargs(argcopy, range, 2, '-');
			if (res2 != 2) {
				error("Syntax error in range '%s'.  Should be <val1>-<val2>.\n", args[x]);
				return -1;
			}
			res2 =sscanf(range[0], "%i", &start);
			if (res2 != 1) {
				error("Syntax error.  Start of range '%s' should be a number from 1 to %d\n", args[x], ZT_MAX_CHANNELS - 1);
				return -1;
			} else if ((start < 1) || (start >= ZT_MAX_CHANNELS)) {
				error("Start of range '%s' must be between 1 and %d (not '%d')\n", args[x], ZT_MAX_CHANNELS - 1, start);
				return -1;
			}
			res2 =sscanf(range[1], "%i", &finish);
			if (res2 != 1) {
				error("Syntax error.  End of range '%s' should be a number from 1 to %d\n", args[x], ZT_MAX_CHANNELS - 1);
				return -1;
			} else if ((finish < 1) || (finish >= ZT_MAX_CHANNELS)) {
				error("end of range '%s' must be between 1 and %d (not '%d')\n", args[x], ZT_MAX_CHANNELS - 1, finish);
				return -1;
			}
			if (start > finish) {
				error("Range '%s' should start before it ends\n", args[x]);
				return -1;
			}
			for (y=start;y<=finish;y++)
				chans[y]=1;
		} else {
			/* It's a single channel */
			res2 =sscanf(args[x], "%i", &chan);
			if (res2 != 1) {
				error("Syntax error.  Channel should be a number from 1 to %d, not '%s'\n", ZT_MAX_CHANNELS - 1, args[x]);
				return -1;
			} else if ((chan < 1) || (chan >= ZT_MAX_CHANNELS)) {
				error("Channel must be between 1 and %d (not '%d')\n", ZT_MAX_CHANNELS - 1, chan);
				return -1;
			}
			chans[chan]=1;
		}		
	}
	return res;
}

int parse_idle(int *i, char *s)
{
	char a,b,c,d;
	if (s) {
		if (sscanf(s, "%c%c%c%c", &a,&b,&c,&d) == 4) {
			if (((a == '0') || (a == '1')) && ((b == '0') || (b == '1')) && ((c == '0') || (c == '1')) && ((d == '0') || (d == '1'))) {
				*i = 0;
				if (a == '1') 
					*i |= ZT_ABIT;
				if (b == '1')
					*i |= ZT_BBIT;
				if (c == '1')
					*i |= ZT_CBIT;
				if (d == '1')
					*i |= ZT_DBIT;
				return 0;
			}
		}
	}
	error("CAS Signalling requires idle definition in the form ':xxxx' at the end of the channel definition, where xxxx represent the a, b, c, and d bits\n");
	return -1;
}

static int parse_channel(char *channel, int *startchan)
{
	if (!channel || (sscanf(channel, "%i", startchan) != 1) || 
		(*startchan < 1)) {
		error("DACS requires a starting channel in the form ':x' where x is the channel\n");
		return -1;
	}
	return 0;
}

static int chanconfig(char *keyword, char *args)
{
	int chans[ZT_MAX_CHANNELS];
	int res = 0;
	int x;
	int master=0;
	int dacschan = 0;
	char *idle;
	bzero(chans, sizeof(chans));
	strtok(args, ":");
	idle = strtok(NULL, ":");
	if (!strcasecmp(keyword, "dacs") || !strcasecmp(keyword, "dacsrbs")) {
		res = parse_channel(idle, &dacschan);
	}
	if (!res)
		res = apply_channels(chans, args);
	if (res <= 0)
		return -1;
	for (x=1;x<ZT_MAX_CHANNELS;x++) 
		if (chans[x]) {
			if (slineno[x]) {
				error("Channel %d already configured as '%s' at line %d\n", x, sig[x], slineno[x]);
				continue;
			}
			if ((!strcasecmp(keyword, "dacs") || !strcasecmp(keyword, "dacsrbs")) && slineno[dacschan]) {
				error("DACS Destination channel %d already configured as '%s' at line %d\n", dacschan, sig[dacschan], slineno[dacschan]);
				continue;
			} else {
				cc[dacschan].chan = dacschan;
				cc[dacschan].master = dacschan;
				slineno[dacschan] = lineno;
			}
			cc[x].chan = x;
			cc[x].master = x;
			slineno[x] = lineno;
			if (!strcasecmp(keyword, "e&m")) {
				cc[x].sigtype = ZT_SIG_EM;
				sig[x] = sigtype_to_str(cc[x].sigtype);
			} else if (!strcasecmp(keyword, "e&me1")) {
				cc[x].sigtype = ZT_SIG_EM_E1;
				sig[x] = sigtype_to_str(cc[x].sigtype);
			} else if (!strcasecmp(keyword, "fxsls")) {
				cc[x].sigtype = ZT_SIG_FXSLS;
				sig[x] = sigtype_to_str(cc[x].sigtype);
			} else if (!strcasecmp(keyword, "fxsgs")) {
				cc[x].sigtype = ZT_SIG_FXSGS;
				sig[x] = sigtype_to_str(cc[x].sigtype);
			} else if (!strcasecmp(keyword, "fxsks")) {
				cc[x].sigtype = ZT_SIG_FXSKS;
				sig[x] = sigtype_to_str(cc[x].sigtype);
			} else if (!strcasecmp(keyword, "fxols")) {
				cc[x].sigtype = ZT_SIG_FXOLS;
				sig[x] = sigtype_to_str(cc[x].sigtype);
			} else if (!strcasecmp(keyword, "fxogs")) {
				cc[x].sigtype = ZT_SIG_FXOGS;
				sig[x] = sigtype_to_str(cc[x].sigtype);
			} else if (!strcasecmp(keyword, "fxoks")) {
				cc[x].sigtype = ZT_SIG_FXOKS;
				sig[x] = sigtype_to_str(cc[x].sigtype);
			} else if (!strcasecmp(keyword, "cas") || !strcasecmp(keyword, "user")) {
				if (parse_idle(&cc[x].idlebits, idle))
					return -1;
				cc[x].sigtype = ZT_SIG_CAS;
				sig[x] = sigtype_to_str(cc[x].sigtype);
			} else if (!strcasecmp(keyword, "dacs")) {
				/* Setup channel for monitor */
				cc[x].idlebits = dacschan;
				cc[x].sigtype = ZT_SIG_DACS;
				sig[x] = sigtype_to_str(cc[x].sigtype);
				/* Setup inverse */
				cc[dacschan].idlebits = x;
				cc[dacschan].sigtype = ZT_SIG_DACS;
				sig[x] = sigtype_to_str(cc[dacschan].sigtype);
				dacschan++;
			} else if (!strcasecmp(keyword, "dacsrbs")) {
				/* Setup channel for monitor */
				cc[x].idlebits = dacschan;
				cc[x].sigtype = ZT_SIG_DACS_RBS;
				sig[x] = sigtype_to_str(cc[x].sigtype);
				/* Setup inverse */
				cc[dacschan].idlebits = x;
				cc[dacschan].sigtype = ZT_SIG_DACS_RBS;
				sig[x] = sigtype_to_str(cc[dacschan].sigtype);
				dacschan++;
			} else if (!strcasecmp(keyword, "unused")) {
				cc[x].sigtype = 0;
				sig[x] = sigtype_to_str(cc[x].sigtype);
			} else if (!strcasecmp(keyword, "indclear") || !strcasecmp(keyword, "bchan")) {
				cc[x].sigtype = ZT_SIG_CLEAR;
				sig[x] = sigtype_to_str(cc[x].sigtype);
			} else if (!strcasecmp(keyword, "clear")) {
				sig[x] = sigtype_to_str(ZT_SIG_CLEAR);
				if (master) {
					cc[x].sigtype = ZT_SIG_SLAVE;
					cc[x].master = master;
				} else {
					cc[x].sigtype = ZT_SIG_CLEAR;
					master = x;
				}
			} else if (!strcasecmp(keyword, "rawhdlc")) {
				sig[x] = sigtype_to_str(ZT_SIG_HDLCRAW);
				if (master) {
					cc[x].sigtype = ZT_SIG_SLAVE;
					cc[x].master = master;
				} else {
					cc[x].sigtype = ZT_SIG_HDLCRAW;
					master = x;
				}
			} else if (!strcasecmp(keyword, "nethdlc")) {
				sig[x] = sigtype_to_str(ZT_SIG_HDLCNET);
				memset(cc[x].netdev_name, 0, sizeof(cc[x].netdev_name));
				if (master) {
					cc[x].sigtype = ZT_SIG_SLAVE;
					cc[x].master = master;
				} else {
					cc[x].sigtype = ZT_SIG_HDLCNET;
					if (idle) {
					    zap_copy_string(cc[x].netdev_name, idle, sizeof(cc[x].netdev_name));
					}
					master = x;
				}
			} else if (!strcasecmp(keyword, "fcshdlc")) {
				sig[x] = sigtype_to_str(ZT_SIG_HDLCFCS);
				if (master) {
					cc[x].sigtype = ZT_SIG_SLAVE;
					cc[x].master = master;
				} else {
					cc[x].sigtype = ZT_SIG_HDLCFCS;
					master = x;
				}
			} else if (!strcasecmp(keyword, "dchan")) {
				sig[x] = "D-channel";
				cc[x].sigtype = ZT_SIG_HDLCFCS;
			} else if (!strcasecmp(keyword, "hardhdlc")) {
				sig[x] = "Hardware assisted D-channel";
				cc[x].sigtype = ZT_SIG_HARDHDLC;
			} else if (!strcasecmp(keyword, "mtp2")) {
				sig[x] = "MTP2";
				cc[x].sigtype = ZT_SIG_MTP2;
			} else {
				fprintf(stderr, "Huh? (%s)\n", keyword);
			}
		}
	return 0;
}

static int setlaw(char *keyword, char *args)
{
	int res;
	int law;
	int x;
	int chans[ZT_MAX_CHANNELS];

	bzero(chans, sizeof(chans));
	res = apply_channels(chans, args);
	if (res <= 0)
		return -1;
	if (!strcasecmp(keyword, "alaw")) {
		law = ZT_LAW_ALAW;
	} else if (!strcasecmp(keyword, "mulaw")) {
		law = ZT_LAW_MULAW;
	} else if (!strcasecmp(keyword, "deflaw")) {
		law = ZT_LAW_DEFAULT;
	} else {
		fprintf(stderr, "Huh??? Don't know about '%s' law\n", keyword);
		return -1;
	}
	for (x=0;x<ZT_MAX_CHANNELS;x++) {
		if (chans[x])
			cc[x].deflaw = law;
	}
	return 0;
}

static int registerzone(char *keyword, char *args)
{
	if (numzones >= ZT_TONE_ZONE_MAX) {
		error("Too many tone zones specified\n");
		return 0;
	}
	zap_copy_string(zonestoload[numzones++], args, sizeof(zonestoload[0]));
	return 0;
}

static int defaultzone(char *keyword, char *args)
{
	struct tone_zone *z;
	if (!(z = tone_zone_find(args))) {
		error("No such tone zone known: %s\n", args);
		return 0;
	}
	deftonezone = z->zone;
	return 0;
}

#if 0
static int unimplemented(char *keyword, char *args)
{
	fprintf(stderr, "Warning: '%s' is not yet implemented\n", keyword);
	return 0;
}
#endif


/* Radio functions */

int ctcss(char *keyword, char *args)
{
	static char *realargs[10];
	int argc;
	int res;
	int rxtone;
	int rxtag;
	int txtone;
	int isdcs = 0;
	argc = res = parseargs(args, realargs, 3, ',');
	if (res != 3) {
		error("Incorrect number of arguments to 'ctcss' (should be <rxtone>,<rxtag>,<txtone>)\n");
	}
	res = sscanf(realargs[0], "%d", &rxtone);
	if ((res == 1) && (rxtone < 1))
		res = -1;
	if (res != 1) {
		error("Invalid rxtone '%s', should be a number > 0.\n", realargs[0]);
	}
	res = sscanf(realargs[1], "%i", &rxtag);
	if ((res == 1) && (rxtag < 0))
		res = -1;
	if (res != 1) {
		error("Invalid rxtag '%s', should be a number > 0.\n", realargs[1]);
	}
	if ((*realargs[2] == 'D') || (*realargs[2] == 'd'))
	{
		realargs[2]++;
		isdcs = 0x8000;
	}
	res = sscanf(realargs[2], "%d", &txtone);
	if ((res == 1) && (rxtag < 0))
		res = -1;
	if (res != 1) {
		error("Invalid txtone '%s', should be a number > 0.\n", realargs[2]);
	}

	if (toneindex >= NUM_TONES)
	{
		error("Cannot specify more then %d CTCSS tones\n",NUM_TONES);
	}
	rxtones[toneindex] = rxtone;
	rxtags[toneindex] = rxtag;
	txtones[toneindex] = txtone | isdcs;
	toneindex++;
	return 0;
}

int dcsrx(char *keyword, char *args)
{
	static char *realargs[10];
	int argc;
	int res;
	int rxtone;
	argc = res = parseargs(args, realargs, 1, ',');
	if (res != 1) {
		error("Incorrect number of arguments to 'dcsrx' (should be <rxtone>)\n");
	}
	res = sscanf(realargs[0], "%d", &rxtone);
	if ((res == 1) && (rxtone < 1))
		res = -1;
	if (res != 1) {
		error("Invalid rxtone '%s', should be a number > 0.\n", realargs[0]);
	}

	rxtones[0] = rxtone;
	return 0;
}

int tx(char *keyword, char *args)
{
	static char *realargs[10];
	int argc;
	int res;
	int txtone;
	int isdcs = 0;
	argc = res = parseargs(args, realargs, 1, ',');
	if (res != 1) {
		error("Incorrect number of arguments to 'tx' (should be <txtone>)\n");
	}
	if ((*realargs[0] == 'D') || (*realargs[0] == 'd'))
	{
		realargs[0]++;
		isdcs = 0x8000;
	}
	res = sscanf(realargs[0], "%d", &txtone);
	if ((res == 1) && (txtone < 1))
		res = -1;
	if (res != 1) {
		error("Invalid tx (tone) '%s', should be a number > 0.\n", realargs[0]);
	}

	txtones[0] = txtone | isdcs;
	return 0;
}

int debounce_time(char *keyword, char *args)
{
	static char *realargs[10];
	int argc;
	int res;
	int val;
	argc = res = parseargs(args, realargs, 1, ',');
	if (res != 1) {
		error("Incorrect number of arguments to 'debouncetime' (should be <value>)\n");
	}
	res = sscanf(realargs[0], "%d", &val);
	if ((res == 1) && (val < 1))
		res = -1;
	if (res != 1) {
		error("Invalid value '%s', should be a number > 0.\n", realargs[0]);
	}

	debouncetime = val;
	return 0;
}

int burst_time(char *keyword, char *args)
{
	static char *realargs[10];
	int argc;
	int res;
	int val;
	argc = res = parseargs(args, realargs, 1, ',');
	if (res != 1) {
		error("Incorrect number of arguments to 'bursttime' (should be <value>)\n");
	}
	res = sscanf(realargs[0], "%d", &val);
	if ((res == 1) && (val < 1))
		res = -1;
	if (res != 1) {
		error("Invalid value '%s', should be a number > 0.\n", realargs[0]);
	}

	bursttime = val;
	return 0;
}

int tx_gain(char *keyword, char *args)
{
	static char *realargs[10];
	int argc;
	int res;
	int val;
	argc = res = parseargs(args, realargs, 1, ',');
	if (res != 1) {
		error("Incorrect number of arguments to 'txgain' (should be <value>)\n");
	}
	res = sscanf(realargs[0], "%d", &val);
	if (res != 1) {
		error("Invalid value '%s', should be a number > 0.\n", realargs[0]);
	}

	txgain = val;
	return 0;
}

int rx_gain(char *keyword, char *args)
{
	static char *realargs[10];
	int argc;
	int res;
	int val;
	argc = res = parseargs(args, realargs, 1, ',');
	if (res != 1) {
		error("Incorrect number of arguments to 'rxgain' (should be <value>)\n");
	}
	res = sscanf(realargs[0], "%d", &val);
	if (res != 1) {
		error("Invalid value '%s', should be a number > 0.\n", realargs[0]);
	}

	rxgain = val;
	return 0;
}

int de_emp(char *keyword, char *args)
{
	static char *realargs[10];
	int argc;
	int res;
	int val;
	argc = res = parseargs(args, realargs, 1, ',');
	if (res != 1) {
		error("Incorrect number of arguments to 'de-emp' (should be <value>)\n");
	}
	res = sscanf(realargs[0], "%d", &val);
	if ((res == 1) && (val < 1))
		res = -1;
	if (res != 1) {
		error("Invalid value '%s', should be a number > 0.\n", realargs[0]);
	}

	deemp = val;
	return 0;
}

int pre_emp(char *keyword, char *args)
{
	static char *realargs[10];
	int argc;
	int res;
	int val;
	argc = res = parseargs(args, realargs, 1, ',');
	if (res != 1) {
		error("Incorrect number of arguments to 'pre_emp' (should be <value>)\n");
	}
	res = sscanf(realargs[0], "%d", &val);
	if ((res == 1) && (val < 1))
		res = -1;
	if (res != 1) {
		error("Invalid value '%s', should be a number > 0.\n", realargs[0]);
	}

	preemp = val;
	return 0;
}

int invert_cor(char *keyword, char *args)
{
	static char *realargs[10];
	int argc;
	int res;
	int val;
	argc = res = parseargs(args, realargs, 1, ',');
	if (res != 1) {
		error("Incorrect number of arguments to 'invertcor' (should be <value>)\n");
	}
	if ((*realargs[0] == 'y') || (*realargs[0] == 'Y')) val = 1;
	else if ((*realargs[0] == 'n') || (*realargs[0] == 'N')) val = 0;
	else
	{
		res = sscanf(realargs[0], "%d", &val);
		if ((res == 1) && (val < 0))
			res = -1;
		if (res != 1) {
			error("Invalid value '%s', should be a number > 0.\n", realargs[0]);
		}
	}
	invertcor = (val > 0);
	return 0;
}

int ext_tone(char *keyword, char *args)
{
	static char *realargs[10];
	int argc;
	int res;
	int val;
	argc = res = parseargs(args, realargs, 1, ',');
	if (res != 1) {
		error("Incorrect number of arguments to 'exttone' (should be <value>)\n");
	}
	if ((*realargs[0] == 'y') || (*realargs[0] == 'Y')) val = 1;
	else if ((*realargs[0] == 'n') || (*realargs[0] == 'N')) val = 0;
	else if ((*realargs[0] == 'i') || (*realargs[0] == 'I')) val = 2;
	else
	{
		res = sscanf(realargs[0], "%d", &val);
		if ((res == 1) && (val < 0))
			res = -1;
		if (val > 2) res = -1;
		if (res != 1) {
			error("Invalid value '%s', should be a number > 0.\n", realargs[0]);
		}
	}
	exttone = val;
	return 0;
}

int cor_thresh(char *keyword, char *args)
{
	static char *realargs[10];
	int argc;
	int res;
	int val;
	int x = 0;
	argc = res = parseargs(args, realargs, 1, ',');
	if (res != 1) {
		error("Incorrect number of arguments to 'corthresh' (should be <value>)\n");
	}
	res = sscanf(realargs[0], "%d", &val);
	if ((res == 1) && (val < 1))
		res = -1;
	for(x = 0; corthreshes[x]; x++)
	{
		if (corthreshes[x] == val) break;
	}
	if (!corthreshes[x]) res = -1;
	if (res != 1) {
		error("Invalid value '%s', should be a number > 0.\n", realargs[0]);
	}
	corthresh = x + 1;
	return 0;
}

int rad_apply_channels(int chans[], char *argstr)
{
	char *args[ZT_MAX_CHANNELS+1];
	char *range[3];
	int res,x, res2,y;
	int chan;
	int start, finish;
	char argcopy[256];
	res = parseargs(argstr, args, ZT_MAX_CHANNELS, ',');
	if (res < 0)
		error("Too many arguments...  Max is %d\n", ZT_MAX_CHANNELS);
	for (x=0;x<res;x++) {
		if (strchr(args[x], '-')) {
			/* It's a range */
			zap_copy_string(argcopy, args[x], sizeof(argcopy));
			res2 = parseargs(argcopy, range, 2, '-');
			if (res2 != 2) {
				error("Syntax error in range '%s'.  Should be <val1>-<val2>.\n", args[x]);
				return -1;
			}
			res2 =sscanf(range[0], "%i", &start);
			if (res2 != 1) {
				error("Syntax error.  Start of range '%s' should be a number from 1 to %d\n", args[x], ZT_MAX_CHANNELS - 1);
				return -1;
			} else if ((start < 1) || (start >= ZT_MAX_CHANNELS)) {
				error("Start of range '%s' must be between 1 and %d (not '%d')\n", args[x], ZT_MAX_CHANNELS - 1, start);
				return -1;
			}
			res2 =sscanf(range[1], "%i", &finish);
			if (res2 != 1) {
				error("Syntax error.  End of range '%s' should be a number from 1 to %d\n", args[x], ZT_MAX_CHANNELS - 1);
				return -1;
			} else if ((finish < 1) || (finish >= ZT_MAX_CHANNELS)) {
				error("end of range '%s' must be between 1 and %d (not '%d')\n", args[x], ZT_MAX_CHANNELS - 1, finish);
				return -1;
			}
			if (start > finish) {
				error("Range '%s' should start before it ends\n", args[x]);
				return -1;
			}
			for (y=start;y<=finish;y++)
				chans[y]=1;
		} else {
			/* It's a single channel */
			res2 =sscanf(args[x], "%i", &chan);
			if (res2 != 1) {
				error("Syntax error.  Channel should be a number from 1 to %d, not '%s'\n", ZT_MAX_CHANNELS - 1, args[x]);
				return -1;
			} else if ((chan < 1) || (chan >= ZT_MAX_CHANNELS)) {
				error("Channel must be between 1 and %d (not '%d')\n", ZT_MAX_CHANNELS - 1, chan);
				return -1;
			}
			chans[chan]=1;
		}		
	}
	return res;
}

static int rad_chanconfig(char *keyword, char *args)
{
	int chans[ZT_MAX_CHANNELS];
	int res = 0;
	int x,i,n;
	struct zt_radio_param p;

	toneindex = 1;
	bzero(chans, sizeof(chans));
	res = rad_apply_channels(chans, args);
	if (res <= 0)
		return -1;
	for (x=1;x<ZT_MAX_CHANNELS;x++) {
		if (chans[x]) {
			p.radpar = ZT_RADPAR_NUMTONES;
			if (ind_ioctl(x,fd,ZT_RADIO_GETPARAM,&p) == -1)
				n = 0; else n = p.data;
			if (n)
			{
				p.radpar = ZT_RADPAR_INITTONE;
				if (ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p) == -1)
					error("Cannot init tones for channel %d\n",x);
				if (!rxtones[0]) for(i = 1; i <= n; i++)
				{
					if (rxtones[i])
					{
						p.radpar = ZT_RADPAR_RXTONE;
						p.index = i;
						p.data = rxtones[i];
						if (ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p) == -1)
							error("Cannot set rxtone on channel %d\n",x);
					}
					if (rxtags[i])
					{
						p.radpar = ZT_RADPAR_RXTONECLASS;
						p.index = i;
						p.data = rxtags[i];
						if (ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p) == -1)
							error("Cannot set rxtag on channel %d\n",x);
					}
					if (txtones[i])
					{
						p.radpar = ZT_RADPAR_TXTONE;
						p.index = i;
						p.data = txtones[i];
						if (ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p) == -1)
							error("Cannot set txtone on channel %d\n",x);
					}
				} else { /* if we may have DCS receive */
					if (rxtones[0])
					{
						p.radpar = ZT_RADPAR_RXTONE;
						p.index = 0;
						p.data = rxtones[0];
						if (ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p) == -1)
							error("Cannot set DCS rxtone on channel %d\n",x);
					}
				}
				if (txtones[0])
				{
					p.radpar = ZT_RADPAR_TXTONE;
					p.index = 0;
					p.data = txtones[0];
					if (ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p) == -1)
						error("Cannot set default txtone on channel %d\n",x);
				}
			}
			if (debouncetime)
			{
				p.radpar = ZT_RADPAR_DEBOUNCETIME;
				p.data = debouncetime;
				if (ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p) == -1)
					error("Cannot set debouncetime on channel %d\n",x);
			}
			if (bursttime)
			{
				p.radpar = ZT_RADPAR_BURSTTIME;
				p.data = bursttime;
				if (ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p) == -1)
					error("Cannot set bursttime on channel %d\n",x);
			}
			p.radpar = ZT_RADPAR_DEEMP;
			p.data = deemp;
			ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p);
			p.radpar = ZT_RADPAR_PREEMP;
			p.data = preemp;
			ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p);
			p.radpar = ZT_RADPAR_TXGAIN;
			p.data = txgain;
			ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p);
			p.radpar = ZT_RADPAR_RXGAIN;
			p.data = rxgain;
			ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p);
			p.radpar = ZT_RADPAR_INVERTCOR;
			p.data = invertcor;
			ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p);
			p.radpar = ZT_RADPAR_EXTRXTONE;
			p.data = exttone;
			ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p);
			if (corthresh)
			{
				p.radpar = ZT_RADPAR_CORTHRESH;
				p.data = corthresh - 1;
				if (ind_ioctl(x,fd,ZT_RADIO_SETPARAM,&p) == -1)
					error("Cannot set corthresh on channel %d\n",x);
			}
		}
	}
	clear_fields();
	return 0;
}

/* End Radio functions */

static void printconfig(int fd)
{
	int x,y;
	int ps;
	int configs=0;
	struct zt_versioninfo vi;

	strcpy(vi.version, "Unknown");
	strcpy(vi.echo_canceller, "Unknown");

	if (ioctl(fd, ZT_GETVERSION, &vi))
		error("Unable to read Zaptel version information.\n");

	printf("\nZaptel Version: %s\n"
	       "Echo Canceller: %s\n"
	       "Configuration\n"
	       "======================\n\n", vi.version, vi.echo_canceller);
	for (x=0;x<spans;x++) 
		printf("SPAN %d: %3s/%4s Build-out: %s\n",
		       x+1, ( lc[x].lineconfig & ZT_CONFIG_D4 ? "D4" :
			      lc[x].lineconfig & ZT_CONFIG_ESF ? "ESF" :
			      lc[x].lineconfig & ZT_CONFIG_CCS ? "CCS" : "CAS" ),
			( lc[x].lineconfig & ZT_CONFIG_AMI ? "AMI" :
			  lc[x].lineconfig & ZT_CONFIG_B8ZS ? "B8ZS" :
			  lc[x].lineconfig & ZT_CONFIG_HDB3 ? "HDB3" : "???" ),
			lbostr[lc[x].lbo]);
	for (x=0;x<numdynamic;x++) {
		printf("Dynamic span %d: driver %s, addr %s, channels %d, timing %d\n",
		       x +1, zds[x].driver, zds[x].addr, zds[x].numchans, zds[x].timing);
	}
	if (verbose > 1) {
		printf("\nChannel map:\n\n");
		for (x=1;x<ZT_MAX_CHANNELS;x++) {
			if ((cc[x].sigtype != ZT_SIG_SLAVE) && (cc[x].sigtype)) {
				configs++;
				ps = 0;
				if ((cc[x].sigtype & __ZT_SIG_DACS) == __ZT_SIG_DACS)
					printf("Channel %02d %s to %02d", x, sig[x], cc[x].idlebits);
				else {
					printf("Channel %02d: %s (%s)", x, sig[x], laws[cc[x].deflaw]);
					for (y=1;y<ZT_MAX_CHANNELS;y++) 
						if (cc[y].master == x)  {
							printf("%s%02d", ps++ ? " " : " (Slaves: ", y);
						}
				}
				if (ps) printf(")\n"); else printf("\n");
			} else
				if (cc[x].sigtype) configs++;
		}
	} else 
		for (x=1;x<ZT_MAX_CHANNELS;x++) 
			if (cc[x].sigtype)
				configs++;
	printf("\n%d channels to configure.\n\n", configs);
}

static struct handler {
	char *keyword;
	int (*func)(char *keyword, char *args);
} handlers[] = {
	{ "span", spanconfig },
	{ "dynamic", dspanconfig },
	{ "loadzone", registerzone },
	{ "defaultzone", defaultzone },
	{ "e&m", chanconfig },
	{ "e&me1", chanconfig },
	{ "fxsls", chanconfig },
	{ "fxsgs", chanconfig },
	{ "fxsks", chanconfig },
	{ "fxols", chanconfig },
	{ "fxogs", chanconfig },
	{ "fxoks", chanconfig },
	{ "rawhdlc", chanconfig },
	{ "nethdlc", chanconfig },
	{ "fcshdlc", chanconfig },
	{ "hardhdlc", chanconfig },
	{ "mtp2", chanconfig },
	{ "dchan", chanconfig },
	{ "bchan", chanconfig },
	{ "indclear", chanconfig },
	{ "clear", chanconfig },
	{ "unused", chanconfig },
	{ "cas", chanconfig },
	{ "dacs", chanconfig },
	{ "dacsrbs", chanconfig },
	{ "user", chanconfig },
	{ "alaw", setlaw },
	{ "mulaw", setlaw },
	{ "deflaw", setlaw },
	{ "ctcss", ctcss },
	{ "dcsrx", dcsrx },
	{ "rxdcs", dcsrx },
	{ "tx", tx },
	{ "debouncetime", debounce_time },
	{ "bursttime", burst_time },
	{ "exttone", ext_tone },
	{ "invertcor", invert_cor },
	{ "corthresh", cor_thresh },
	{ "rxgain", rx_gain },
	{ "txgain", tx_gain },
	{ "deemp", de_emp },
	{ "preemp", pre_emp },
	{ "channel", rad_chanconfig },
	{ "channels", rad_chanconfig },
};

static char *readline()
{
	static char buf[256];
	char *c;
	do {
		if (!fgets(buf, sizeof(buf), cf)) 
			return NULL;
		/* Strip comments */
		c = strchr(buf, '#');
		if (c)
			*c = '\0';
		trim(buf);
		lineno++;
	} while (!strlen(buf));
	return buf;
}

static void usage(char *argv0, int exitcode)
{
	char *c;
	c = strrchr(argv0, '/');
	if (!c)
		c = argv0;
	else
		c++;
	fprintf(stderr, 
		"Usage: %s [options]\n"
		"    Valid options are:\n"
		"  -c <filename>     -- Use <filename> instead of " CONFIG_FILENAME "\n"
		"  -d [level]        -- Generate debugging output. (Default level is 1.)\n"
		"  -f                -- Always reconfigure every channel\n"
		"  -h                -- Generate this help statement\n"
		"  -s                -- Shutdown spans only\n"
		"  -t                -- Test mode only, do not apply\n"
		"  -v                -- Verbose (more -v's means more verbose)\n"
	,c);
	exit(exitcode);
}

int main(int argc, char *argv[])
{
	int c;
	char *buf;
	char *key, *value;
	int x,found;
	while((c = getopt(argc, argv, "fthc:vsd::")) != -1) {
		switch(c) {
		case 'c':
			filename=optarg;
			break;
		case 'h':
			usage(argv[0], 0);
			break;
		case '?':
			usage(argv[0], 1);
			break;
		case 'v':
			verbose++;
			break;
		case 'f':
			force++;
			break;
		case 't':
			fo_real = 0;
			break;
		case 's':
			stopmode = 1;
			break;
		case 'd':
			if (optarg)
				debug = atoi(optarg);
			else
				debug = 1;	
			break;
		}
	}
	if (fd == -1) fd = open(MASTER_DEVICE, O_RDWR);
	if (fd < 0) 
		error("Unable to open master device '%s'\n", MASTER_DEVICE);
	cf = fopen(filename, "r");
	if (cf) {
		while((buf = readline())) {
			if (debug & DEBUG_READER) 
				fprintf(stderr, "Line %d: %s\n", lineno, buf);
			key = value = buf;
			while(value && *value && (*value != '=')) value++;
			if (value)
				*value='\0';
			if (value)
				value++;
			while(value && *value && (*value < 33)) value++;
			if (*value) {
				trim(key);
				if (debug & DEBUG_PARSER)
					fprintf(stderr, "Keyword: [%s], Value: [%s]\n", key, value);
			} else
				error("Syntax error.  Should be <keyword>=<value>\n");
			found=0;
			for (x=0;x<sizeof(handlers) / sizeof(handlers[0]);x++) {
				if (!strcasecmp(key, handlers[x].keyword)) {
					found++;
					handlers[x].func(key, value);
					break;
				}
			}
			if (!found) 
				error("Unknown keyword '%s'\n", key);
		}
		if (debug & DEBUG_READER)
			fprintf(stderr, "<End of File>\n");
		fclose(cf);
	} else {
		error("Unable to open configuration file '%s'\n", filename);
	}

	if (!errcnt) {
		if (verbose) {
			printconfig(fd);
		}
		if (fo_real) {
			if (debug & DEBUG_APPLY) {
				printf("About to open Master device\n");
				fflush(stdout);
			}
			for (x=0;x<numdynamic;x++) {
				/* destroy them all */
				ioctl(fd, ZT_DYNAMIC_DESTROY, &zds[x]);
			}
			if (stopmode) {
				for (x=0;x<spans;x++) {
					if (ioctl(fd, ZT_SHUTDOWN, &lc[x].span)) {
						fprintf(stderr, "Zaptel shutdown failed: %s\n", strerror(errno));
						close(fd);
						exit(1);
					}
				}
			} else {
				for (x=0;x<spans;x++) {
					if (ioctl(fd, ZT_SPANCONFIG, lc + x)) {
						fprintf(stderr, "ZT_SPANCONFIG failed on span %d: %s (%d)\n", lc[x].span, strerror(errno), errno);
						close(fd);
						exit(1);
					}
				}
				for (x=0;x<numdynamic;x++) {
					if (ioctl(fd, ZT_DYNAMIC_CREATE, &zds[x])) {
						fprintf(stderr, "Zaptel dynamic span creation failed: %s\n", strerror(errno));
						close(fd);
						exit(1);
					}
				}
				for (x=1;x<ZT_MAX_CHANNELS;x++) {
					struct zt_params current_state;
					int master;
					int needupdate = force;
					
					if (debug & DEBUG_APPLY) {
						printf("Configuring device %d\n", x);
						fflush(stdout);
					}
					if (!cc[x].sigtype)
						continue;
					
					if (!needupdate) {
						memset(&current_state, 0, sizeof(current_state));
						current_state.channo = cc[x].chan | ZT_GET_PARAMS_RETURN_MASTER;
						if (ioctl(fd, ZT_GET_PARAMS, &current_state))
							needupdate = 1;
					}
					
					if (!needupdate) {
						master = current_state.channo >> 16;
						
						if (cc[x].sigtype != current_state.sigtype) {
							needupdate++;
							if (verbose > 1)
								printf("Changing signalling on channel %d from %s to %s\n",
								       cc[x].chan, sigtype_to_str(current_state.sigtype),
								       sigtype_to_str(cc[x].sigtype));
						}
						
						if ((cc[x].deflaw != ZT_LAW_DEFAULT) && (cc[x].deflaw != current_state.curlaw)) {
							needupdate++;
							if (verbose > 1)
								printf("Changing law on channel %d from %s to %s\n",
								       cc[x].chan, laws[current_state.curlaw],
								       laws[cc[x].deflaw]);
						}
						
						if (cc[x].master != master) {
							needupdate++;
							if (verbose > 1)
								printf("Changing master of channel %d from %d to %d\n",
								       cc[x].chan, master,
								       cc[x].master);
						}
						
						if (cc[x].idlebits != current_state.idlebits) {
							needupdate++;
							if (verbose > 1)
								printf("Changing idle bits of channel %d from %d to %d\n",
								       cc[x].chan, current_state.idlebits,
								       cc[x].idlebits);
						}
					}
					
					if (needupdate && ioctl(fd, ZT_CHANCONFIG, &cc[x])) {
						fprintf(stderr, "ZT_CHANCONFIG failed on channel %d: %s (%d)\n", x, strerror(errno), errno);
						if (errno == EINVAL) {
							fprintf(stderr, "Did you forget that FXS interfaces are configured with FXO signalling\n"
								"and that FXO interfaces use FXS signalling?\n");
						}
						close(fd);
						exit(1);
					}
				}
				for (x=0;x<numzones;x++) {
					if (debug & DEBUG_APPLY) {
						printf("Loading tone zone for %s\n", zonestoload[x]);
						fflush(stdout);
					}
					if (tone_zone_register(fd, zonestoload[x]))
						if (errno != EBUSY)
							error("Unable to register tone zone '%s'\n", zonestoload[x]);
				}
				if (debug & DEBUG_APPLY) {
					printf("Doing startup\n");
					fflush(stdout);
				}
				if (deftonezone > -1) {
					if (ioctl(fd, ZT_DEFAULTZONE, &deftonezone)) {
						fprintf(stderr, "ZT_DEFAULTZONE failed: %s (%d)\n", strerror(errno), errno);
						close(fd);
						exit(1);
					}
				}
				for (x=0;x<spans;x++) {
					if (ioctl(fd, ZT_STARTUP, &lc[x].span)) {
						fprintf(stderr, "Zaptel startup failed: %s\n", strerror(errno));
						close(fd);
						exit(1);
					}
				}
			}
		}
	} else {
		fprintf(stderr, "\n%d error(s) detected\n\n", errcnt);
		exit(1);
	}
	exit(0);
}
