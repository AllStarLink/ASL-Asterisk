/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) <Year>, <Your Name Here>
 *
 * <Your Name Here> <<Your Email Here>>
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
 * \brief Skeleton application
 *
 * \author <Your Name Here> <<Your Email Here>>
 * 
 * This is a skeleton for development of an Asterisk application 
 * \ingroup applications
 */

/*** MODULEINFO
	<defaultenabled>no</defaultenabled>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 135847 $")

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"

static char *app = "Skel";
static char *synopsis = 
"Skeleton application.";
static char *descrip = "This application is a template to build other applications from.\n"
 " It shows you the basic structure to create your own Asterisk applications.\n";

enum {
	OPTION_A = (1 << 0),
	OPTION_B = (1 << 1),
	OPTION_C = (1 << 2),
} option_flags;

enum {
	OPTION_ARG_B = 0,
	OPTION_ARG_C = 1,
	/* This *must* be the last value in this enum! */
	OPTION_ARG_ARRAY_SIZE = 2,
} option_args;

AST_APP_OPTIONS(app_opts,{
	AST_APP_OPTION('a', OPTION_A),
	AST_APP_OPTION_ARG('b', OPTION_B, OPTION_ARG_B),
	AST_APP_OPTION_ARG('c', OPTION_C, OPTION_ARG_C),
});


static int app_exec(struct ast_channel *chan, void *data)
{
	int res = 0;
	struct ast_flags flags;
	struct ast_module_user *u;
	char *parse, *opts[OPTION_ARG_ARRAY_SIZE];
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(dummy);
		AST_APP_ARG(options);
	);

	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "%s requires an argument (dummy|[options])\n", app);
		return -1;
	}

	u = ast_module_user_add(chan);

	/* Do our thing here */

	/* We need to make a copy of the input string if we are going to modify it! */
	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.argc == 2)
		ast_app_parse_options(app_opts, &flags, opts, args.options);

	if (!ast_strlen_zero(args.dummy)) 
		ast_log(LOG_NOTICE, "Dummy value is : %s\n", args.dummy);

	if (ast_test_flag(&flags, OPTION_A))
		ast_log(LOG_NOTICE, "Option A is set\n");

	if (ast_test_flag(&flags, OPTION_B))
		ast_log(LOG_NOTICE, "Option B is set with : %s\n", opts[OPTION_ARG_B] ? opts[OPTION_ARG_B] : "<unspecified>");

	if (ast_test_flag(&flags, OPTION_C))
		ast_log(LOG_NOTICE, "Option C is set with : %s\n", opts[OPTION_ARG_C] ? opts[OPTION_ARG_C] : "<unspecified>");

	ast_module_user_remove(u);

	return res;
}

static int unload_module(void)
{
	int res;
	res = ast_unregister_application(app);
	return res;	
}

static int load_module(void)
{
	return ast_register_application(app, app_exec, synopsis, descrip);
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Skeleton (sample) Application");
