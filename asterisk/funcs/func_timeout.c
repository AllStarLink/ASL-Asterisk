/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
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
 * \brief Channel timeout related dialplan functions
 *
 * \author Mark Spencer <markster@digium.com> 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 146799 $")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/options.h"

static int timeout_read(struct ast_channel *chan, char *cmd, char *data,
			char *buf, size_t len)
{
	time_t myt;

	if (!chan)
		return -1;

	if (!data) {
		ast_log(LOG_ERROR, "Must specify type of timeout to get.\n");
		return -1;
	}

	switch (*data) {
	case 'a':
	case 'A':
		if (chan->whentohangup == 0) {
			ast_copy_string(buf, "0", len);
		} else {
			time(&myt);
			snprintf(buf, len, "%d", (int) (chan->whentohangup - myt));
		}
		break;

	case 'r':
	case 'R':
		if (chan->pbx) {
			snprintf(buf, len, "%d", chan->pbx->rtimeout);
		}
		break;

	case 'd':
	case 'D':
		if (chan->pbx) {
			snprintf(buf, len, "%d", chan->pbx->dtimeout);
		}
		break;

	default:
		ast_log(LOG_ERROR, "Unknown timeout type specified.\n");
		return -1;
	}

	return 0;
}

static int timeout_write(struct ast_channel *chan, char *cmd, char *data,
			 const char *value)
{
	int x;
	char timestr[64];
	struct tm myt;

	if (!chan)
		return -1;

	if (!data) {
		ast_log(LOG_ERROR, "Must specify type of timeout to set.\n");
		return -1;
	}

	if (!value)
		return -1;

	x = atoi(value);
	if (x < 0)
		x = 0;

	switch (*data) {
	case 'a':
	case 'A':
		ast_channel_setwhentohangup(chan, x);
		if (option_verbose > 2) {
			if (chan->whentohangup) {
				strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S UTC",
					 gmtime_r(&chan->whentohangup, &myt));
				ast_verbose(VERBOSE_PREFIX_3 "Channel will hangup at %s.\n",
					    timestr);
			} else {
				ast_verbose(VERBOSE_PREFIX_3 "Channel hangup cancelled.\n");
			}
		}
		break;

	case 'r':
	case 'R':
		if (chan->pbx) {
			chan->pbx->rtimeout = x;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Response timeout set to %d\n",
					    chan->pbx->rtimeout);
		}
		break;

	case 'd':
	case 'D':
		if (chan->pbx) {
			chan->pbx->dtimeout = x;
			if (option_verbose > 2)
				ast_verbose(VERBOSE_PREFIX_3 "Digit timeout set to %d\n",
					    chan->pbx->dtimeout);
		}
		break;

	default:
		ast_log(LOG_ERROR, "Unknown timeout type specified.\n");
		break;
	}

	return 0;
}

static struct ast_custom_function timeout_function = {
	.name = "TIMEOUT",
	.synopsis = "Gets or sets timeouts on the channel.",
	.syntax = "TIMEOUT(timeouttype)",
	.desc =
		"Gets or sets various channel timeouts. The timeouts that can be\n"
		"manipulated are:\n" "\n"
		"absolute: The absolute maximum amount of time permitted for a call.  A\n"
		"	   setting of 0 disables the timeout.\n" "\n"
		"digit:    The maximum amount of time permitted between digits when the\n"
		"          user is typing in an extension.  When this timeout expires,\n"
		"          after the user has started to type in an extension, the\n"
		"          extension will be considered complete, and will be\n"
		"          interpreted.  Note that if an extension typed in is valid,\n"
		"          it will not have to timeout to be tested, so typically at\n"
		"          the expiry of this timeout, the extension will be considered\n"
		"          invalid (and thus control would be passed to the 'i'\n"
		"          extension, or if it doesn't exist the call would be\n"
		"          terminated).  The default timeout is 5 seconds.\n" "\n"
		"response: The maximum amount of time permitted after falling through a\n"
		"	   series of priorities for a channel in which the user may\n"
		"	   begin typing an extension.  If the user does not type an\n"
		"	   extension in this amount of time, control will pass to the\n"
		"	   't' extension if it exists, and if not the call would be\n"
		"	   terminated.  The default timeout is 10 seconds.\n",
	.read = timeout_read,
	.write = timeout_write,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&timeout_function);
}

static int load_module(void)
{
	return ast_custom_function_register(&timeout_function);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Channel timeout dialplan functions");
