/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
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
 * \brief App to flash a DAHDI trunk
 *
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */
 
/*** MODULEINFO
	<depend>dahdi</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 182652 $")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/image.h"
#include "asterisk/options.h"

#include "asterisk/dahdi_compat.h"

static char *app = "Flash";

static char *dahdi_synopsis = "Flashes a DAHDI trunk";

static char *dahdi_descrip = 
"Performs a flash on a DAHDI trunk.  This can be used\n"
"to access features provided on an incoming analogue circuit\n"
"such as conference and call waiting. Use with SendDTMF() to\n"
"perform external transfers\n";

static char *zap_synopsis = "Flashes a Zap trunk";

static char *zap_descrip = 
"Performs a flash on a Zap trunk.  This can be used\n"
"to access features provided on an incoming analogue circuit\n"
"such as conference and call waiting. Use with SendDTMF() to\n"
"perform external transfers\n";

static inline int zt_wait_event(int fd)
{
	/* Avoid the silly zt_waitevent which ignores a bunch of events */
	int i,j=0;
	i = DAHDI_IOMUX_SIGEVENT;
	if (ioctl(fd, DAHDI_IOMUX, &i) == -1) return -1;
	if (ioctl(fd, DAHDI_GETEVENT, &j) == -1) return -1;
	return j;
}

static int flash_exec(struct ast_channel *chan, void *data)
{
	int res = -1;
	int x;
	struct ast_module_user *u;
	struct dahdi_params ztp;
	u = ast_module_user_add(chan);
	if (!strcasecmp(chan->tech->type, dahdi_chan_name)) {
		memset(&ztp, 0, sizeof(ztp));
		res = ioctl(chan->fds[0], DAHDI_GET_PARAMS, &ztp);
		if (!res) {
			if (ztp.sigtype & __DAHDI_SIG_FXS) {
				x = DAHDI_FLASH;
				res = ioctl(chan->fds[0], DAHDI_HOOK, &x);
				if (!res || (errno == EINPROGRESS)) {
					if (res) {
						/* Wait for the event to finish */
						zt_wait_event(chan->fds[0]);
					}
					res = ast_safe_sleep(chan, 1000);
					if (option_verbose > 2)
						ast_verbose(VERBOSE_PREFIX_3 "Flashed channel %s\n", chan->name);
				} else
					ast_log(LOG_WARNING, "Unable to flash channel %s: %s\n", chan->name, strerror(errno));
			} else
				ast_log(LOG_WARNING, "%s is not an FXO Channel\n", chan->name);
		} else
			ast_log(LOG_WARNING, "Unable to get parameters of %s: %s\n", chan->name, strerror(errno));
	} else
		ast_log(LOG_WARNING, "%s is not a DAHDI channel\n", chan->name);
	ast_module_user_remove(u);
	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(app);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	if (*dahdi_chan_mode == CHAN_ZAP_MODE) {
		return ast_register_application(app, flash_exec, zap_synopsis, zap_descrip);
	} else {
		return ast_register_application(app, flash_exec, dahdi_synopsis, dahdi_descrip);
	}
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Flash channel application");
