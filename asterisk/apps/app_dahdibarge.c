/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Special thanks to comphealth.com for sponsoring this
 * GPL application.
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Zap Barge support
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \note Special thanks to comphealth.com for sponsoring this
 * GPL application.
 * 
 * \ingroup applications
 */

/*** MODULEINFO
	<depend>dahdi</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 211528 $")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/config.h"
#include "asterisk/app.h"
#include "asterisk/options.h"
#include "asterisk/cli.h"
#include "asterisk/say.h"
#include "asterisk/utils.h"

#include "asterisk/dahdi_compat.h"

static char *dahdi_app = "DAHDIBarge";
static char *zap_app = "ZapBarge";

static char *dahdi_synopsis = "Barge in (monitor) DAHDI channel";
static char *zap_synopsis = "Barge in (monitor) Zap channel";

static char *dahdi_descrip = 
"  DAHDIBarge([channel]): Barges in on a specified DAHDI\n"
"channel or prompts if one is not specified.  Returns\n"
"-1 when caller user hangs up and is independent of the\n"
"state of the channel being monitored.";

static char *zap_descrip = 
"  ZapBarge([channel]): Barges in on a specified Zaptel\n"
"channel or prompts if one is not specified.  Returns\n"
"-1 when caller user hangs up and is independent of the\n"
"state of the channel being monitored.";

#define CONF_SIZE 160

static int careful_write(int fd, unsigned char *data, int len)
{
	int res;
	while(len) {
		res = write(fd, data, len);
		if (res < 1) {
			if (errno != EAGAIN) {
				ast_log(LOG_WARNING, "Failed to write audio data to conference: %s\n", strerror(errno));
				return -1;
			} else
				return 0;
		}
		len -= res;
		data += res;
	}
	return 0;
}

static int conf_run(struct ast_channel *chan, int confno, int confflags)
{
	int fd;
	struct dahdi_confinfo ztc;
	struct ast_frame *f;
	struct ast_channel *c;
	struct ast_frame fr;
	int outfd;
	int ms;
	int nfds;
	int res;
	int flags;
	int retryzap;
	int origfd;
	int ret = -1;
	struct dahdi_bufferinfo bi;
	char __buf[CONF_SIZE + AST_FRIENDLY_OFFSET];
	char *buf = __buf + AST_FRIENDLY_OFFSET;

	/* Set it into U-law mode (write) */
	if (ast_set_write_format(chan, AST_FORMAT_ULAW) < 0) {
		ast_log(LOG_WARNING, "Unable to set '%s' to write ulaw mode\n", chan->name);
		goto outrun;
	}

	/* Set it into U-law mode (read) */
	if (ast_set_read_format(chan, AST_FORMAT_ULAW) < 0) {
		ast_log(LOG_WARNING, "Unable to set '%s' to read ulaw mode\n", chan->name);
		goto outrun;
	}
	ast_indicate(chan, -1);
	retryzap = strcasecmp(chan->tech->type, dahdi_chan_name);
zapretry:
	origfd = chan->fds[0];
	if (retryzap) {
		fd = open(DAHDI_FILE_PSEUDO, O_RDWR);
		if (fd < 0) {
			ast_log(LOG_WARNING, "Unable to open pseudo channel: %s\n", strerror(errno));
			goto outrun;
		}
		/* Make non-blocking */
		flags = fcntl(fd, F_GETFL);
		if (flags < 0) {
			ast_log(LOG_WARNING, "Unable to get flags: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
			ast_log(LOG_WARNING, "Unable to set flags: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		/* Setup buffering information */
		memset(&bi, 0, sizeof(bi));
		bi.bufsize = CONF_SIZE;
		bi.txbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.rxbufpolicy = DAHDI_POLICY_IMMEDIATE;
		bi.numbufs = 4;
		if (ioctl(fd, DAHDI_SET_BUFINFO, &bi)) {
			ast_log(LOG_WARNING, "Unable to set buffering information: %s\n", strerror(errno));
			close(fd);
			goto outrun;
		}
		nfds = 1;
	} else {
		/* XXX Make sure we're not running on a pseudo channel XXX */
		fd = chan->fds[0];
		nfds = 0;
	}
	memset(&ztc, 0, sizeof(ztc));
	/* Check to see if we're in a conference... */
	ztc.chan = 0;	
	if (ioctl(fd, DAHDI_GETCONF, &ztc)) {
		ast_log(LOG_WARNING, "Error getting conference\n");
		close(fd);
		goto outrun;
	}
	if (ztc.confmode) {
		/* Whoa, already in a conference...  Retry... */
		if (!retryzap) {
			ast_log(LOG_DEBUG, "Channel is in a conference already, retrying with pseudo\n");
			retryzap = 1;
			goto zapretry;
		}
	}
	memset(&ztc, 0, sizeof(ztc));
	/* Add us to the conference */
	ztc.chan = 0;	
	ztc.confno = confno;
	ztc.confmode = DAHDI_CONF_MONITORBOTH;

	if (ioctl(fd, DAHDI_SETCONF, &ztc)) {
		ast_log(LOG_WARNING, "Error setting conference\n");
		close(fd);
		goto outrun;
	}
	ast_log(LOG_DEBUG, "Placed channel %s in channel %d monitor\n", chan->name, confno);

	for(;;) {
		outfd = -1;
		ms = -1;
		c = ast_waitfor_nandfds(&chan, 1, &fd, nfds, NULL, &outfd, &ms);
		if (c) {
			if (c->fds[0] != origfd) {
				if (retryzap) {
					/* Kill old pseudo */
					close(fd);
				}
				ast_log(LOG_DEBUG, "Ooh, something swapped out under us, starting over\n");
				retryzap = 0;
				goto zapretry;
			}
			f = ast_read(c);
			if (!f) 
				break;
			if ((f->frametype == AST_FRAME_DTMF) && (f->subclass == '#')) {
				ret = 0;
				ast_frfree(f);
				break;
			} else if (fd != chan->fds[0]) {
				if (f->frametype == AST_FRAME_VOICE) {
					if (f->subclass == AST_FORMAT_ULAW) {
						/* Carefully write */
						careful_write(fd, f->data, f->datalen);
					} else
						ast_log(LOG_WARNING, "Huh?  Got a non-ulaw (%d) frame in the conference\n", f->subclass);
				}
			}
			ast_frfree(f);
		} else if (outfd > -1) {
			res = read(outfd, buf, CONF_SIZE);
			if (res > 0) {
				memset(&fr, 0, sizeof(fr));
				fr.frametype = AST_FRAME_VOICE;
				fr.subclass = AST_FORMAT_ULAW;
				fr.datalen = res;
				fr.samples = res;
				fr.data = buf;
				fr.offset = AST_FRIENDLY_OFFSET;
				if (ast_write(chan, &fr) < 0) {
					ast_log(LOG_WARNING, "Unable to write frame to channel: %s\n", strerror(errno));
					/* break; */
				}
			} else 
				ast_log(LOG_WARNING, "Failed to read frame: %s\n", strerror(errno));
		}
	}
	if (fd != chan->fds[0])
		close(fd);
	else {
		/* Take out of conference */
		/* Add us to the conference */
		ztc.chan = 0;	
		ztc.confno = 0;
		ztc.confmode = 0;
		if (ioctl(fd, DAHDI_SETCONF, &ztc)) {
			ast_log(LOG_WARNING, "Error setting conference\n");
		}
	}

outrun:

	return ret;
}

static int exec(struct ast_channel *chan, void *data, int dahdimode)
{
	int res=-1;
	struct ast_module_user *u;
	int retrycnt = 0;
	int confflags = 0;
	int confno = 0;
	char confstr[80] = "";

	u = ast_module_user_add(chan);
	
	if (!ast_strlen_zero(data)) {
		if (dahdimode) {
			if ((sscanf(data, "DAHDI/%30d", &confno) != 1) &&
			    (sscanf(data, "%30d", &confno) != 1)) {
				ast_log(LOG_WARNING, "Argument (if specified) must be a channel number, not '%s'\n", (char *) data);
				ast_module_user_remove(u);
				return 0;
			}
		} else {
			if ((sscanf(data, "Zap/%30d", &confno) != 1) &&
			    (sscanf(data, "%30d", &confno) != 1)) {
				ast_log(LOG_WARNING, "Argument (if specified) must be a channel number, not '%s'\n", (char *) data);
				ast_module_user_remove(u);
				return 0;
			}
		}
	}
	
	if (chan->_state != AST_STATE_UP)
		ast_answer(chan);

	while(!confno && (++retrycnt < 4)) {
		/* Prompt user for conference number */
		confstr[0] = '\0';
		res = ast_app_getdata(chan, "conf-getchannel",confstr, sizeof(confstr) - 1, 0);
		if (res <0) goto out;
		if (sscanf(confstr, "%30d", &confno) != 1)
			confno = 0;
	}
	if (confno) {
		/* XXX Should prompt user for pin if pin is required XXX */
		/* Run the conference */
		res = conf_run(chan, confno, confflags);
	}
out:
	/* Do the conference */
	ast_module_user_remove(u);
	return res;
}

static int exec_zap(struct ast_channel *chan, void *data)
{
	ast_log(LOG_WARNING, "Use of the command %s is deprecated, please use %s instead.\n", zap_app, dahdi_app);
	
	return exec(chan, data, 0);
}

static int exec_dahdi(struct ast_channel *chan, void *data)
{
	return exec(chan, data, 1);
}

static int unload_module(void) 
{
	int res = 0;

	if (*dahdi_chan_mode == CHAN_DAHDI_PLUS_ZAP_MODE) {
		res |= ast_unregister_application(dahdi_app);
	}

	res |= ast_unregister_application(zap_app);
	
	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	int res = 0;

	if (*dahdi_chan_mode == CHAN_DAHDI_PLUS_ZAP_MODE) {
		res |= ast_register_application(dahdi_app, exec_dahdi, dahdi_synopsis, dahdi_descrip);
	}

	res |= ast_register_application(zap_app, exec_zap, zap_synopsis, zap_descrip);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Barge in on channel application");
