/* zaptel.c - pppd plugin to implement PPP over Zaptel HDLC channel.
 *
 * Copyright 2002  Digium, Inc. 
 *		   Mark Spencer <markster@digium.inc>
 *
 * Borrows from PPPoE by Michal Ostrowski <mostrows@styx.uwaterloo.ca>,
 *		  Jamal Hadi Salim <hadi@cyberus.ca>
 *
 * which in turn...
 *
 * Borrows heavily from the PPPoATM plugin by Mitchell Blank Jr.,
 * which is based in part on work from Jens Axboe and Paul Mackerras.
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

#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <pppd/pppd.h>
#include <pppd/fsm.h>
#include <pppd/lcp.h>
#include <pppd/ipcp.h>
#include <pppd/ccp.h>
#include <pppd/pathnames.h>

#include "zaptel.h"

extern int new_style_driver;

const char pppd_version[] = VERSION;

#define _PATH_ZAPOPT         _ROOT_PATH "/etc/ppp/options."

#define ZAP_MTU	(ZT_DEFAULT_MTU_MRU - 16)
extern int kill_link;
int     retries = 0;

int setdevname_zaptel(const char *cp);

static option_t zaptel_options[] = {
	{ "device name", o_wild, (void *) &setdevname_zaptel,
	  "Serial port device name",
	  OPT_DEVNAM | OPT_PRIVFIX | OPT_NOARG  | OPT_A2STRVAL | OPT_STATIC,
	  devnam},
	{ NULL }
};

static int zapfd = -1;
static int zapchan = 0;

static int connect_zaptel(void)
{
    
    ZT_PARAMS ztp;
    int res;
    int x;

    info("Zaptel device is '%s'\n", devnam);

    strlcpy(ppp_devnam, devnam, sizeof(ppp_devnam));

    if (strlen(devnam) && strcmp(devnam, "stdin")) {
	/* Get the channel number */
	zapchan = atoi(devnam);
	if (zapchan < 1) {
		fatal("'%s' is not a valid device name\n", devnam);
		return -1;
	}

	/* Open /dev/zap/channel interface */
	zapfd = open("/dev/zap/channel", O_RDWR);
	if (zapfd < 0) {
		fatal("Unable to open zaptel channel interface: '%s'\n", strerror(errno));
		return zapfd;
	}

	/* Specify which channel we really want */
	x = zapchan;
	res = ioctl(zapfd, ZT_SPECIFY, &x);
	if (res) {
		fatal("Unable to specify channel %d: %s\n", zapchan, strerror(errno));
		close(zapfd);
		zapfd = -1;
		return -1;
	}
    } else
        zapfd = STDIN_FILENO;


    /* Get channel parameters */
    memset(&ztp, 0, sizeof(ztp));
    ztp.channo = -1;

    res = ioctl(zapfd, ZT_GET_PARAMS, &ztp);

    if (res) {
	fatal("Device '%s' does not appear to be a zaptel device\n", devnam ? devnam : "<stdin>");
    }

    x = 1;

    /* Throw into HDLC/PPP mode */
    res = ioctl(zapfd, ZT_HDLCPPP, &x);

    if (res) {
	fatal("Unable to put device '%s' into HDLC mode\n", devnam);
	close(zapfd);
	zapfd = -1;
	return -1;
    }

    /* Once the logging is fixed, print a message here indicating
       connection parameters */
    zapchan = ztp.channo;
    info("Connected to zaptel device '%s' (%d)\n", ztp.name, ztp.channo);

    return zapfd;
}

static void disconnect_zaptel(void)
{
    int res;
    int x = 0;
    /* Throw out of HDLC mode */
    res = ioctl(zapfd, ZT_HDLCPPP, &x);

    if (res) {
	warn("Unable to take device '%s' out of HDLC mode\n", devnam);
    }

    /* Close if it's not stdin */
    if (strlen(devnam))
	close(zapfd);
    warn("Disconnect from zaptel");

}


static int setspeed_zaptel(const char *cp)
{
    return 0;
}

static void zaptel_extra_options()
{
    int ret;
    char buf[256];
    snprintf(buf, 256, _PATH_ZAPOPT "%s",devnam);
    if(!options_from_file(buf, 0, 0, 1))
	exit(EXIT_OPTION_ERROR);

}



static void send_config_zaptel(int mtu,
			      u_int32_t asyncmap,
			      int pcomp,
			      int accomp)
{
    int sock;

    if (mtu > ZAP_MTU) {
	warn("Couldn't increase MTU to %d.", mtu);
	mtu = ZAP_MTU;
    }
}


static void recv_config_zaptel(int mru,
			      u_int32_t asyncmap,
			      int pcomp,
			      int accomp)
{
    if (mru > ZAP_MTU)
	error("Couldn't increase MRU to %d", mru);
}

static void set_xaccm_pppoe(int unit, ext_accm accm)
{
    /* NOTHING */
}



struct channel zaptel_channel;

/* Check is cp is a valid zaptel device
 * return either 1 if "cp" is a reasonable thing to name a device
 * or die.
 * Note that we don't actually open the device at this point
 * We do need to fill in:
 *   devnam: a string representation of the device
 */

int (*old_setdevname_hook)(const char* cp) = NULL;
int setdevname_zaptel(const char *cp)
{
    int ret;
    int chan;

    /* If already set, forgoe */
    if (strlen(devnam))
	return 1;


    if (strcmp(cp, "stdin")) {
	ret = sscanf(cp, "%d", &chan);
	if (ret != 1) {
		fatal("Zaptel: Invalid channel: '%s'\n", cp);
		return -1;
	}
    }

    zap_copy_string(devnam, cp, sizeof(devnam));

    info("Using zaptel device '%s'\n", devnam);

    ret = 1;

    if( ret == 1 && the_channel != &zaptel_channel ){

	the_channel = &zaptel_channel;

	modem = 0;

	lcp_allowoptions[0].neg_accompression = 0;
	lcp_wantoptions[0].neg_accompression = 0;

	lcp_allowoptions[0].neg_pcompression = 0;
	lcp_wantoptions[0].neg_pcompression = 0;

	ccp_allowoptions[0].deflate = 0 ;
	ccp_wantoptions[0].deflate = 0 ;

	ipcp_allowoptions[0].neg_vj=0;
	ipcp_wantoptions[0].neg_vj=0;

	ccp_allowoptions[0].bsd_compress = 0;
	ccp_wantoptions[0].bsd_compress = 0;

	lcp_allowoptions[0].neg_asyncmap = 0;
	lcp_wantoptions[0].neg_asyncmap = 0;

    }
    return ret;
}



void plugin_init(void)
{
    if (!ppp_available() && !new_style_driver)
	fatal("Kernel doesn't support ppp_generic needed for Zaptel PPP");
    add_options(zaptel_options);

    info("Zaptel Plugin Initialized");
}

struct channel zaptel_channel = {
    options: zaptel_options,
    process_extra_options: &zaptel_extra_options,
    check_options: NULL,
    connect: &connect_zaptel,
    disconnect: &disconnect_zaptel,
    establish_ppp: &generic_establish_ppp,
    disestablish_ppp: &generic_disestablish_ppp,
    send_config: &send_config_zaptel,
    recv_config: &recv_config_zaptel,
    close: NULL,
    cleanup: NULL
};

