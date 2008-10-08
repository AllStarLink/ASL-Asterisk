/*
 * Configuration program for Zapata Telephony Interface
 *
 * Written by Mark Spencer <markster@digium.com>
 * Based on previous works, designs, and architectures conceived and
 * written by Jim Dixon <jim@lambdatel.com>.
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
#include <math.h>
#ifdef STANDALONE_ZAPATA
#include "kernel/zaptel.h"
#include "tonezone.h"
#else
#include <zaptel/zaptel.h>
#include <zaptel/tonezone.h>
#endif
#include "ztcfg.h"

#define NUM_SPANS ZT_MAX_SPANS

/* Assume no more than 1024 dynamics */
#define NUM_DYNAMIC	1024

static int lineno=0;

static FILE *cf;

static char *filename=CONFIG_FILENAME;

#define DEBUG_READER (1 << 0)
#define DEBUG_PARSER (1 << 1)
#define DEBUG_APPLY  (1 << 2)
static int debug = 0;

static int errcnt = 0;

static int deftonezone = -1;

static struct zt_lineconfig lc[ZT_MAX_SPANS];

static struct zt_chanconfig cc[ZT_MAX_CHANNELS];

static struct zt_sfconfig sf[ZT_MAX_CHANNELS];

static struct zt_dynamic_span zds[NUM_DYNAMIC];

static char *sig[ZT_MAX_CHANNELS];		/* Signalling */

static int slineno[ZT_MAX_CHANNELS];	/* Line number where signalling specified */

static int spans=0;

static int fo_real = 1;

static int verbose = 0;

static int stopmode = 0;

static int numdynamic = 0;

static char zonestoload[ZT_TONE_ZONE_MAX][10];

static int numzones = 0;

static char *lbostr[] = {
"0 db (CSU)/0-133 feet (DSX-1)",
"133-266 feet (DSX-1)",
"266-399 feet (DSX-1)",
"399-533 feet (DSX-1)",
"533-655 feet (DSX-1)",
"-7.5db (CSU)",
"-15db (CSU)",
"-22.5db (CSU)"
};

static char *laws[] = {
	"Default",
	"Mu-law",
	"A-law"
};

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
	argc = res = parseargs(args, realargs, 6, ',');
	if ((res < 5) || (res > 6)) {
		error("Incorrect number of arguments to 'span' (should be <spanno>,<timing>,<lbo>,<framing>,<coding>[,yellow])\n");
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
	lc[spans].span = spans + 1;
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

void mknotch(float freq, float bw, long *p1, long *p2, long *p3);

static void mktxtone(float freq, float l, int *fac, int *init_v2, int *init_v3)
{
float	gain;

	/* Bring it down -8 dbm */
	gain = pow(10.0, (l - 3.14) / 20.0) * 65536.0 / 2.0;

	*fac = 2.0 * cos(2.0 * M_PI * (freq / 8000.0)) * 32768.0;
	*init_v2 = sin(-4.0 * M_PI * (freq / 8000.0)) * gain;
	*init_v3 = sin(-2.0 * M_PI * (freq / 8000.0)) * gain;
}

static int parse_sf(struct zt_sfconfig *sf, char *str)
{
char	*realargs[10],*args;
int	res;
float	rxfreq,rxbw,txfreq,txlevel;
int	flags = 0;

	args = strdup(str);
	res = parseargs(args, realargs, 6, ',');
	if (res != 6)
	{
		error("Incorrect number of arguments to 'sf' (should be :<rx freq (hz)>,<bandwidth (hz)>,<normal/reverse (rx)>,<tx freq (hz)>,<tx level (dbm)>,<normal/reverse (tx)>)\n");
		free(args);
		return -1;
	}
	res = sscanf(realargs[0],"%f",&rxfreq);
	if ((res < 1) || (rxfreq && ((rxfreq < 100.0) || (rxfreq >= 4000.0))))
	{
		error("Invalid rx freq. specification (should be between 100.0 and 4000.0 hertz\n");
		free(args);
		return -1;
	}
	res = sscanf(realargs[1],"%f",&rxbw);
	if ((res < 1) || (rxfreq && ((rxbw < 5.0) || (rxbw >= 1000.0))))
	{
		error("Invalid rx bandwidth specification (should be between 5.0 and 1000.0 hertz\n");
		free(args);
		return -1;
	}
	res = sscanf(realargs[3],"%f",&txfreq);
	if ((res < 1) || (txfreq && ((txfreq < 100.0) || (txfreq >= 4000.0))))
	{
		error("Invalid tx freq. specification (should be between 100.0 and 4000.0 hertz\n");
		free(args);
		return -1;
	}
	res = sscanf(realargs[4],"%f",&txlevel);
	if ((res < 1) || (txfreq && ((txlevel < -50.0) || (txlevel > 3.0))))
	{
		error("Invalid tx level specification (should be between -50.0 and 3.0 dbm\n");
		free(args);
		return -1;
	}
	if ((*realargs[2] == 'i') || (*realargs[2] == 'I') ||
	 (*realargs[2] == 'r') || (*realargs[2] == 'R'))
		flags |= ZT_REVERSE_RXTONE;
	if ((*realargs[5] == 'i') || (*realargs[5] == 'I') ||
	 (*realargs[5] == 'r') || (*realargs[5] == 'R'))
		flags |= ZT_REVERSE_TXTONE;
	if (rxfreq) mknotch(rxfreq,rxbw,&sf->rxp1,&sf->rxp2,&sf->rxp3);
	if (txfreq) mktxtone(txfreq,txlevel,&sf->txtone,&sf->tx_v2,&sf->tx_v3);
	sf->toneflag = flags;
	free(args);
	return 0;
}

static int chanconfig(char *keyword, char *args)
{
	int chans[ZT_MAX_CHANNELS];
	int res;
	int x;
	int master=0;
	char *idle;
	bzero(chans, sizeof(chans));
	strtok(args, ":");
	idle = strtok(NULL, ":");
	res = apply_channels(chans, args);
	if (res <= 0)
		return -1;
	for (x=1;x<ZT_MAX_CHANNELS;x++) 
		if (chans[x]) {
			if (slineno[x]) {
				error("Channel %d already configured as '%s' at line %d\n", x, sig[x], slineno[x]);
				continue;
			}
			cc[x].chan = x;
			memset(&sf[x],0,sizeof(struct zt_sfconfig));
			sf[x].chan = x;
			cc[x].master = x;
			slineno[x] = lineno;
			if (!strcasecmp(keyword, "e&m")) {
				sig[x] = "E & M";
				cc[x].sigtype = ZT_SIG_EM;
			} else if (!strcasecmp(keyword, "sf")) {
				if (idle && parse_sf(&sf[x], idle))
					return -1;
				sig[x] = "Single Freq. Tone Only (No Signalling)";
				cc[x].sigtype = ZT_SIG_SF;
			} else if (!strcasecmp(keyword, "fxsls")) {
				sig[x] = "FXS Loopstart";
				cc[x].sigtype = ZT_SIG_FXSLS;
			} else if (!strcasecmp(keyword, "fxsgs")) {
				sig[x] = "FXS Groundstart";
				cc[x].sigtype = ZT_SIG_FXSGS;
			} else if (!strcasecmp(keyword, "fxsks")) {
				sig[x] = "FXS Kewlstart";
				cc[x].sigtype = ZT_SIG_FXSKS;
			} else if (!strcasecmp(keyword, "fxols")) {
				sig[x] = "FXO Loopstart";
				cc[x].sigtype = ZT_SIG_FXOLS;
			} else if (!strcasecmp(keyword, "fxogs")) {
				sig[x] = "FXO Groundstart";
				cc[x].sigtype = ZT_SIG_FXOGS;
			} else if (!strcasecmp(keyword, "fxoks")) {
				sig[x] = "FXO Kewlstart";
				cc[x].sigtype = ZT_SIG_FXOKS;
			} else if (!strcasecmp(keyword, "cas") || !strcasecmp(keyword, "user")) {
				if (parse_idle(&cc[x].idlebits, idle))
					return -1;
				sig[x] = "CAS / User";
				cc[x].sigtype = ZT_SIG_CAS;
			} else if (!strcasecmp(keyword, "unused")) {
				sig[x] = "Unused";
				cc[x].sigtype = 0;
			} else if (!strcasecmp(keyword, "indclear") || !strcasecmp(keyword, "bchan")) {
				sig[x] = "Individual Clear channel";
				cc[x].sigtype = ZT_SIG_CLEAR;
			} else if (!strcasecmp(keyword, "clear")) {
				sig[x] = "Clear channel";
				if (master) {
					cc[x].sigtype = ZT_SIG_SLAVE;
					cc[x].master = master;
				} else {
					cc[x].sigtype = ZT_SIG_CLEAR;
					master = x;
				}
			} else if (!strcasecmp(keyword, "rawhdlc")) {
				sig[x] = "Raw HDLC";
				if (master) {
					cc[x].sigtype = ZT_SIG_SLAVE;
					cc[x].master = master;
				} else {
					cc[x].sigtype = ZT_SIG_HDLCRAW;
					master = x;
				}
			} else if (!strcasecmp(keyword, "nethdlc")) {
				sig[x] = "Network HDLC";
				if (master) {
					cc[x].sigtype = ZT_SIG_SLAVE;
					cc[x].master = master;
				} else {
					cc[x].sigtype = ZT_SIG_HDLCNET;
					master = x;
				}
			} else if (!strcasecmp(keyword, "fcshdlc") || !strcasecmp(keyword, "dchan")) {
				sig[x] = "HDLC with FCS check";
				if (master) {
					cc[x].sigtype = ZT_SIG_SLAVE;
					cc[x].master = master;
				} else {
					cc[x].sigtype = ZT_SIG_HDLCFCS;
					master = x;
				}
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

static void printconfig()
{
	int x,y;
	int ps;
	int configs=0;
	printf("\nZaptel Configuration\n"
	       "======================\n\n");
	for (x=0;x<spans;x++) 
		printf("SPAN %d: %3s/%4s Build-out: %s\n",
			x+1, ( lc[x].lineconfig & ZT_CONFIG_ESF ? "ESF" : "D4"),
			(lc[x].lineconfig & ZT_CONFIG_B8ZS ? "B8ZS" :  "AMI"),
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
				printf("Channel %02d: %s (%s)", x, sig[x], laws[cc[x].deflaw]);
				ps = 0;
				for (y=1;y<ZT_MAX_CHANNELS;y++) 
					if (cc[y].master == x)  {
						printf("%s%02d", ps++ ? " " : " (Slaves: ", y);
					}
				if (ps) printf(")\n"); else printf("\n");
			} else
				if (cc[x].sigtype) configs++;
		}
	} else 
		for (x=1;x<ZT_MAX_CHANNELS;x++) 
			if (cc[x].sigtype)
				configs++;
	printf("\n%d channels configured.\n\n", configs);
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
	{ "sf", chanconfig },
	{ "fxsls", chanconfig },
	{ "fxsgs", chanconfig },
	{ "fxsks", chanconfig },
	{ "fxols", chanconfig },
	{ "fxogs", chanconfig },
	{ "fxoks", chanconfig },
	{ "rawhdlc", chanconfig },
	{ "nethdlc", chanconfig },
	{ "fcshdlc", chanconfig },
	{ "dchan", chanconfig },
	{ "bchan", chanconfig },
	{ "indclear", chanconfig },
	{ "clear", chanconfig },
	{ "unused", chanconfig },
	{ "cas", chanconfig },
	{ "user", chanconfig },
	{ "alaw", setlaw },
	{ "mulaw", setlaw },
	{ "deflaw", setlaw },
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
		"  -h                -- Generate this help statement\n"
		"  -v                -- Verbose (more -v's means more verbose)\n"
		"  -t                -- Test mode only, do not apply\n"
		"  -s                -- Shutdown spans only\n"
	,c);
	exit(exitcode);
}

int main(int argc, char *argv[])
{
	char c;
	char *buf;
	char *key, *value;
	int x,found;
	int fd;
	while((c = getopt(argc, argv, "hc:vs")) != -1) {
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
		case 't':
			fo_real = 0;
			break;
		case 's':
			stopmode = 1;
			break;
		}
	}
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
			printconfig();
		}
		if (fo_real) {
			if (debug & DEBUG_APPLY) {
				printf("About to open Master device\n");
				fflush(stdout);
			}
			fd = open(MASTER_DEVICE, O_RDWR);
			if (fd < 0) 
				error("Unable to open master device '%s'\n", MASTER_DEVICE);
			else {
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
						if (debug & DEBUG_APPLY) {
							printf("Configuring device %d\n", x);
							fflush(stdout);
						}
						if (cc[x].sigtype) {
							if (ioctl(fd, ZT_CHANCONFIG, &cc[x]) == -1) {
								fprintf(stderr, "ZT_CHANCONFIG failed on channel %d: %s (%d)\n", x, strerror(errno), errno);
								close(fd);
								exit(1);
							}
						}
					}
					for (x=0;x<numzones;x++) {
						if (debug & DEBUG_APPLY) {
							printf("Loading tone zone for %s\n", zonestoload[x]);
							fflush(stdout);
						}
						if (tone_zone_register(fd, zonestoload[x]))
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
					for (x=1;x<ZT_MAX_CHANNELS;x++) {
						if ((sf[x].rxp1 || sf[x].txtone))
						{
							if (ioctl(fd, ZT_SFCONFIG, &sf[x]) == -1) {
								fprintf(stderr, "ZT_SFCONFIG failed on channel %d: %s (%d)\n", x, strerror(errno), errno);
								close(fd);
								exit(1);
							}
						}
					}
				}
				close(fd);
			}
		}
	} else {
		fprintf(stderr, "\n%d error(s) detected\n\n", errcnt);
		exit(1);
	}
	exit(0);
}
