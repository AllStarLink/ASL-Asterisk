/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright 2004 - 2005, Anthony Minessale <anthmct@yahoo.com>
 *
 * Anthony Minessale <anthmct@yahoo.com>
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
 * \brief While Loop Implementation
 *
 * \author Anthony Minessale <anthmct@yahoo.com>
 * 
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 156755 $")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/utils.h"
#include "asterisk/config.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/options.h"

#define ALL_DONE(u,ret) {ast_module_user_remove(u); return ret;}


static char *start_app = "While";
static char *start_desc = 
"Usage:  While(<expr>)\n"
"Start a While Loop.  Execution will return to this point when\n"
"EndWhile is called until expr is no longer true.\n";

static char *start_synopsis = "Start a while loop";


static char *stop_app = "EndWhile";
static char *stop_desc = 
"Usage:  EndWhile()\n"
"Return to the previous called While\n";

static char *stop_synopsis = "End a while loop";

static char *exit_app = "ExitWhile";
static char *exit_desc =
"Usage:  ExitWhile()\n"
"Exits a While loop, whether or not the conditional has been satisfied.\n";
static char *exit_synopsis = "End a While loop";

static char *continue_app = "ContinueWhile";
static char *continue_desc =
"Usage:  ContinueWhile()\n"
"Returns to the top of the while loop and re-evaluates the conditional.\n";
static char *continue_synopsis = "Restart a While loop";

#define VAR_SIZE 64


static const char *get_index(struct ast_channel *chan, const char *prefix, int index) {
	char varname[VAR_SIZE];

	snprintf(varname, VAR_SIZE, "%s_%d", prefix, index);
	return pbx_builtin_getvar_helper(chan, varname);
}

static struct ast_exten *find_matching_priority(struct ast_context *c, const char *exten, int priority, const char *callerid)
{
	struct ast_exten *e;
	struct ast_include *i;
	struct ast_context *c2;

	for (e=ast_walk_context_extensions(c, NULL); e; e=ast_walk_context_extensions(c, e)) {
		if (ast_extension_match(ast_get_extension_name(e), exten)) {
			int needmatch = ast_get_extension_matchcid(e);
			if ((needmatch && ast_extension_match(ast_get_extension_cidmatch(e), callerid)) ||
				(!needmatch)) {
				/* This is the matching extension we want */
				struct ast_exten *p;
				for (p=ast_walk_extension_priorities(e, NULL); p; p=ast_walk_extension_priorities(e, p)) {
					if (priority != ast_get_extension_priority(p))
						continue;
					return p;
				}
			}
		}
	}

	/* No match; run through includes */
	for (i=ast_walk_context_includes(c, NULL); i; i=ast_walk_context_includes(c, i)) {
		for (c2=ast_walk_contexts(NULL); c2; c2=ast_walk_contexts(c2)) {
			if (!strcmp(ast_get_context_name(c2), ast_get_include_name(i))) {
				e = find_matching_priority(c2, exten, priority, callerid);
				if (e)
					return e;
			}
		}
	}
	return NULL;
}

static int find_matching_endwhile(struct ast_channel *chan)
{
	struct ast_context *c;
	int res=-1;

	if (ast_lock_contexts()) {
		ast_log(LOG_ERROR, "Failed to lock contexts list\n");
		return -1;
	}

	for (c=ast_walk_contexts(NULL); c; c=ast_walk_contexts(c)) {
		struct ast_exten *e;

		if (!ast_lock_context(c)) {
			if (!strcmp(ast_get_context_name(c), chan->context)) {
				/* This is the matching context we want */
				int cur_priority = chan->priority + 1, level=1;

				for (e = find_matching_priority(c, chan->exten, cur_priority, chan->cid.cid_num); e; e = find_matching_priority(c, chan->exten, ++cur_priority, chan->cid.cid_num)) {
					if (!strcasecmp(ast_get_extension_app(e), "WHILE")) {
						level++;
					} else if (!strcasecmp(ast_get_extension_app(e), "ENDWHILE")) {
						level--;
					}

					if (level == 0) {
						res = cur_priority;
						break;
					}
				}
			}
			ast_unlock_context(c);
			if (res > 0) {
				break;
			}
		}
	}
	ast_unlock_contexts();
	return res;
}

static int _while_exec(struct ast_channel *chan, void *data, int end)
{
	int res=0;
	struct ast_module_user *u;
	const char *while_pri = NULL;
	char *my_name = NULL;
	const char *condition = NULL, *label = NULL;
	char varname[VAR_SIZE], end_varname[VAR_SIZE];
	const char *prefix = "WHILE";
	size_t size=0;
	int used_index_i = -1, x=0;
	char used_index[VAR_SIZE] = "0", new_index[VAR_SIZE] = "0";

	if (!chan) {
		/* huh ? */
		return -1;
	}

	u = ast_module_user_add(chan);

#if 0
	/* dont want run away loops if the chan isn't even up
	   this is up for debate since it slows things down a tad ......

	   Debate is over... this prevents While/EndWhile from working
	   within the "h" extension.  Not good.
	*/
	if (ast_waitfordigit(chan,1) < 0)
		ALL_DONE(u,-1);
#endif

	for (x=0;;x++) {
		if (get_index(chan, prefix, x)) {
			used_index_i = x;
		} else 
			break;
	}
	
	snprintf(used_index, VAR_SIZE, "%d", used_index_i);
	snprintf(new_index, VAR_SIZE, "%d", used_index_i + 1);
	
	if (!end)
		condition = ast_strdupa(data);

	size = strlen(chan->context) + strlen(chan->exten) + 32;
	my_name = alloca(size);
	memset(my_name, 0, size);
	snprintf(my_name, size, "%s_%s_%d", chan->context, chan->exten, chan->priority);
	
	if (ast_strlen_zero(label)) {
		if (end) 
			label = used_index;
		else if (!(label = pbx_builtin_getvar_helper(chan, my_name))) {
			label = new_index;
			pbx_builtin_setvar_helper(chan, my_name, label);
		}
		
	}
	
	snprintf(varname, VAR_SIZE, "%s_%s", prefix, label);
	while_pri = pbx_builtin_getvar_helper(chan, varname);
	
	if ((while_pri = pbx_builtin_getvar_helper(chan, varname)) && !end) {
		snprintf(end_varname,VAR_SIZE,"END_%s",varname);
	}
	

	if ((!end && !pbx_checkcondition(condition)) || (end == 2)) {
		/* Condition Met (clean up helper vars) */
		const char *goto_str;
		pbx_builtin_setvar_helper(chan, varname, NULL);
		pbx_builtin_setvar_helper(chan, my_name, NULL);
		snprintf(end_varname,VAR_SIZE,"END_%s",varname);
		if ((goto_str=pbx_builtin_getvar_helper(chan, end_varname))) {
			ast_parseable_goto(chan, goto_str);
			pbx_builtin_setvar_helper(chan, end_varname, NULL);
		} else {
			int pri = find_matching_endwhile(chan);
			if (pri > 0) {
				if (option_verbose > 2)
					ast_verbose(VERBOSE_PREFIX_3 "Jumping to priority %d\n", pri);
				chan->priority = pri;
			} else {
				ast_log(LOG_WARNING, "Couldn't find matching EndWhile? (While at %s@%s priority %d)\n", chan->context, chan->exten, chan->priority);
			}
		}
		ALL_DONE(u,res);
	}

	if (!end && !while_pri) {
		char *goto_str;
		size = strlen(chan->context) + strlen(chan->exten) + 32;
		goto_str = alloca(size);
		memset(goto_str, 0, size);
		snprintf(goto_str, size, "%s|%s|%d", chan->context, chan->exten, chan->priority);
		pbx_builtin_setvar_helper(chan, varname, goto_str);
	} 

	else if (end && while_pri) {
		/* END of loop */
		snprintf(end_varname, VAR_SIZE, "END_%s", varname);
		if (! pbx_builtin_getvar_helper(chan, end_varname)) {
			char *goto_str;
			size = strlen(chan->context) + strlen(chan->exten) + 32;
			goto_str = alloca(size);
			memset(goto_str, 0, size);
			snprintf(goto_str, size, "%s|%s|%d", chan->context, chan->exten, chan->priority+1);
			pbx_builtin_setvar_helper(chan, end_varname, goto_str);
		}
		ast_parseable_goto(chan, while_pri);
	}
	



	ALL_DONE(u, res);
}

static int while_start_exec(struct ast_channel *chan, void *data) {
	return _while_exec(chan, data, 0);
}

static int while_end_exec(struct ast_channel *chan, void *data) {
	return _while_exec(chan, data, 1);
}

static int while_exit_exec(struct ast_channel *chan, void *data) {
	return _while_exec(chan, data, 2);
}

static int while_continue_exec(struct ast_channel *chan, void *data)
{
	int x;
	const char *prefix = "WHILE", *while_pri=NULL;

	for (x = 0; ; x++) {
		const char *tmp = get_index(chan, prefix, x);
		if (tmp)
			while_pri = tmp;
		else
			break;
	}

	if (while_pri)
		ast_parseable_goto(chan, while_pri);

	return 0;
}

static int unload_module(void)
{
	int res;
	
	res = ast_unregister_application(start_app);
	res |= ast_unregister_application(stop_app);
	res |= ast_unregister_application(exit_app);
	res |= ast_unregister_application(continue_app);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application(start_app, while_start_exec, start_synopsis, start_desc);
	res |= ast_register_application(stop_app, while_end_exec, stop_synopsis, stop_desc);
	res |= ast_register_application(exit_app, while_exit_exec, exit_synopsis, exit_desc);
	res |= ast_register_application(continue_app, while_continue_exec, continue_synopsis, continue_desc);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "While Loops and Conditional Execution");
