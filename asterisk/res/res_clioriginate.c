/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005 - 2006, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
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

/*! 
 * \file
 * \author Russell Bryant <russell@digium.com>
 *
 * \brief Originate calls via the CLI
 * 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 102378 $");

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/frame.h"

/*! The timeout for originated calls, in seconds */
#define TIMEOUT 30

static char orig_help[] = 
"  There are two ways to use this command. A call can be originated between a\n"
"channel and a specific application, or between a channel and an extension in\n"
"the dialplan. This is similar to call files or the manager originate action.\n"
"Calls originated with this command are given a timeout of 30 seconds.\n\n"

"Usage1: originate <tech/data> application <appname> [appdata]\n"
"  This will originate a call between the specified channel tech/data and the\n"
"given application. Arguments to the application are optional. If the given\n"
"arguments to the application include spaces, all of the arguments to the\n"
"application need to be placed in quotation marks.\n\n"

"Usage2: originate <tech/data> extension [exten@][context]\n"
"  This will originate a call between the specified channel tech/data and the\n"
"given extension. If no context is specified, the 'default' context will be\n"
"used. If no extension is given, the 's' extension will be used.\n";

static int handle_orig(int fd, int argc, char *argv[]);
static char *complete_orig(const char *line, const char *word, int pos, int state);

struct ast_cli_entry cli_cliorig[] = {
	{ { "originate", NULL },
	handle_orig, "Originate a call",
	orig_help, complete_orig },
};

static int orig_app(int fd, const char *chan, const char *app, const char *appdata)
{
	char *chantech;
	char *chandata;
	int reason = 0;
	
	if (ast_strlen_zero(app))
		return RESULT_SHOWUSAGE;

	chandata = ast_strdupa(chan);
	
	chantech = strsep(&chandata, "/");
	if (!chandata) {
		ast_cli(fd, "*** No data provided after channel type! ***\n");
		return RESULT_SHOWUSAGE;
	}

	ast_pbx_outgoing_app(chantech, AST_FORMAT_SLINEAR, chandata, TIMEOUT * 1000, app, appdata, &reason, 0, NULL, NULL, NULL, NULL, NULL);

	return RESULT_SUCCESS;
}

static int orig_exten(int fd, const char *chan, const char *data)
{
	char *chantech;
	char *chandata;
	char *exten = NULL;
	char *context = NULL;
	int reason = 0;

	chandata = ast_strdupa(chan);
	
	chantech = strsep(&chandata, "/");
	if (!chandata) {
		ast_cli(fd, "*** No data provided after channel type! ***\n");
		return RESULT_SHOWUSAGE;
	}

	if (!ast_strlen_zero(data)) {
		context = ast_strdupa(data);
		exten = strsep(&context, "@");
	}

	if (ast_strlen_zero(exten))
		exten = "s";
	if (ast_strlen_zero(context))
		context = "default";
	
	ast_pbx_outgoing_exten(chantech, AST_FORMAT_SLINEAR, chandata, TIMEOUT * 1000, context, exten, 1, &reason, 0, NULL, NULL, NULL, NULL, NULL);

	return RESULT_SUCCESS;
}

static int handle_orig(int fd, int argc, char *argv[])
{
	int res;

	if (ast_strlen_zero(argv[1]) || ast_strlen_zero(argv[2]))
		return RESULT_SHOWUSAGE;

	/* ugly, can be removed when CLI entries have ast_module pointers */
	ast_module_ref(ast_module_info->self);

	if (!strcasecmp("application", argv[2])) {
		res = orig_app(fd, argv[1], argv[3], argv[4]);	
	} else if (!strcasecmp("extension", argv[2])) {
		res = orig_exten(fd, argv[1], argv[3]);
	} else
		res = RESULT_SHOWUSAGE;

	ast_module_unref(ast_module_info->self);

	return res;
}

static char *complete_orig(const char *line, const char *word, int pos, int state)
{
	static char *choices[] = { "application", "extension", NULL };
	char *ret;

	if (pos != 2)
		return NULL;

	/* ugly, can be removed when CLI entries have ast_module pointers */
	ast_module_ref(ast_module_info->self);
	ret = ast_cli_complete(word, choices, state);
	ast_module_unref(ast_module_info->self);

	return ret;
}

static int unload_module(void)
{
	ast_cli_unregister_multiple(cli_cliorig, sizeof(cli_cliorig) / sizeof(struct ast_cli_entry));
	return 0;
}

static int load_module(void)
{
	ast_cli_register_multiple(cli_cliorig, sizeof(cli_cliorig) / sizeof(struct ast_cli_entry));
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Call origination from the CLI");
