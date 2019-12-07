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
 * \brief Realtime PBX Module
 *
 * \arg See also: \ref AstARA
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 85515 $")

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/frame.h"
#include "asterisk/term.h"
#include "asterisk/manager.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/lock.h"
#include "asterisk/md5.h"
#include "asterisk/linkedlists.h"
#include "asterisk/chanvars.h"
#include "asterisk/sched.h"
#include "asterisk/io.h"
#include "asterisk/utils.h"
#include "asterisk/crypto.h"
#include "asterisk/astdb.h"

#define MODE_MATCH 		0
#define MODE_MATCHMORE 	1
#define MODE_CANMATCH 	2

#define EXT_DATA_SIZE 256


/* Realtime switch looks up extensions in the supplied realtime table.

	[context@][realtimetable][/options]

	If the realtimetable is omitted it is assumed to be "extensions".  If no context is 
	specified the context is assumed to be whatever is the container.

	The realtime table should have entries for context,exten,priority,app,args
	
	The realtime table currently does not support callerid fields.

*/


static struct ast_variable *realtime_switch_common(const char *table, const char *context, const char *exten, int priority, int mode)
{
	struct ast_variable *var;
	struct ast_config *cfg;
	char pri[20];
	char *ematch;
	char rexten[AST_MAX_EXTENSION + 20]="";
	int match;
	snprintf(pri, sizeof(pri), "%d", priority);
	switch(mode) {
	case MODE_MATCHMORE:
		ematch = "exten LIKE";
		snprintf(rexten, sizeof(rexten), "%s_%%", exten);
		break;
	case MODE_CANMATCH:
		ematch = "exten LIKE";
		snprintf(rexten, sizeof(rexten), "%s%%", exten);
		break;
	case MODE_MATCH:
	default:
		ematch = "exten";
		ast_copy_string(rexten, exten, sizeof(rexten));
	}
	var = ast_load_realtime(table, ematch, rexten, "context", context, "priority", pri, NULL);
	if (!var) {
		cfg = ast_load_realtime_multientry(table, "exten LIKE", "\\_%", "context", context, "priority", pri, NULL);	
		if (cfg) {
			char *cat = ast_category_browse(cfg, NULL);

			while(cat) {
				switch(mode) {
				case MODE_MATCHMORE:
					match = ast_extension_close(cat, exten, 1);
					break;
				case MODE_CANMATCH:
					match = ast_extension_close(cat, exten, 0);
					break;
				case MODE_MATCH:
				default:
					match = ast_extension_match(cat, exten);
				}
				if (match) {
					var = ast_category_detach_variables(ast_category_get(cfg, cat));
					break;
				}
				cat = ast_category_browse(cfg, cat);
			}
			ast_config_destroy(cfg);
		}
	}
	return var;
}

static struct ast_variable *realtime_common(const char *context, const char *exten, int priority, const char *data, int mode)
{
	const char *ctx = NULL;
	char *table;
	struct ast_variable *var=NULL;
	char *buf = ast_strdupa(data);
	if (buf) {
		char *opts = strchr(buf, '/');
		if (opts)
			*opts++ = '\0';
		table = strchr(buf, '@');
		if (table) {
			*table++ = '\0';
			ctx = buf;
		}
		ctx = S_OR(ctx, context);
		table = S_OR(table, "extensions");
		var = realtime_switch_common(table, ctx, exten, priority, mode);
	}
	return var;
}

static int realtime_exists(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	struct ast_variable *var = realtime_common(context, exten, priority, data, MODE_MATCH);
	if (var) {
		ast_variables_destroy(var);
		return 1;
	}
	return 0;
}

static int realtime_canmatch(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	struct ast_variable *var = realtime_common(context, exten, priority, data, MODE_CANMATCH);
	if (var) {
		ast_variables_destroy(var);
		return 1;
	}
	return 0;
}

static int realtime_exec(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	int res = -1;
	struct ast_variable *var = realtime_common(context, exten, priority, data, MODE_MATCH);

	if (var) {
		char *tmp="";
		char *app = NULL;
		struct ast_variable *v;

		for (v = var; v ; v = v->next) {
			if (!strcasecmp(v->name, "app"))
				app = ast_strdupa(v->value);
			else if (!strcasecmp(v->name, "appdata"))
				tmp = ast_strdupa(v->value);
		}
		ast_variables_destroy(var);
		if (!ast_strlen_zero(app)) {
			struct ast_app *a = pbx_findapp(app);
			if (a) {
				char appdata[512]="";
				char tmp1[80];
				char tmp2[80];
				char tmp3[EXT_DATA_SIZE];

				if(!ast_strlen_zero(tmp))
					pbx_substitute_variables_helper(chan, tmp, appdata, sizeof(appdata) - 1);
				if (option_verbose > 2)
					ast_verbose( VERBOSE_PREFIX_3 "Executing %s(\"%s\", \"%s\")\n",
						 term_color(tmp1, app, COLOR_BRCYAN, 0, sizeof(tmp1)),
						 term_color(tmp2, chan->name, COLOR_BRMAGENTA, 0, sizeof(tmp2)),
						 term_color(tmp3, S_OR(appdata, ""), COLOR_BRMAGENTA, 0, sizeof(tmp3)));
				manager_event(EVENT_FLAG_CALL, "Newexten",
							  "Channel: %s\r\n"
							  "Context: %s\r\n"
							  "Extension: %s\r\n"
							  "Priority: %d\r\n"
							  "Application: %s\r\n"
							  "AppData: %s\r\n"
							  "Uniqueid: %s\r\n",
							  chan->name, chan->context, chan->exten, chan->priority, app, !ast_strlen_zero(appdata) ? appdata : "(NULL)", chan->uniqueid);
				
				res = pbx_exec(chan, a, appdata);
			} else
				ast_log(LOG_NOTICE, "No such application '%s' for extension '%s' in context '%s'\n", app, exten, context);
		} else {
			ast_log(LOG_WARNING, "No application specified for realtime extension '%s' in context '%s'\n", exten, context);
		}
	}
	return res;
}

static int realtime_matchmore(struct ast_channel *chan, const char *context, const char *exten, int priority, const char *callerid, const char *data)
{
	struct ast_variable *var = realtime_common(context, exten, priority, data, MODE_MATCHMORE);
	if (var) {
		ast_variables_destroy(var);
		return 1;
	}
	return 0;
}

static struct ast_switch realtime_switch =
{
        name:                   "Realtime",
        description:   		"Realtime Dialplan Switch",
        exists:                 realtime_exists,
        canmatch:               realtime_canmatch,
        exec:                   realtime_exec,
        matchmore:              realtime_matchmore,
};

static int unload_module(void)
{
	ast_unregister_switch(&realtime_switch);
	return 0;
}

static int load_module(void)
{
	ast_register_switch(&realtime_switch);
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Realtime Switch");
