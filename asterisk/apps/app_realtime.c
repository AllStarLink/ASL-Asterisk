/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Anthony Minessale <anthmct@yahoo.com>
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
 * \brief RealTime App
 *
 * \author Anthony Minessale <anthmct@yahoo.com>
 * \author Mark Spencer <markster@digium.com>
 * 
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 165255 $")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/lock.h"
#include "asterisk/cli.h"

#define next_one(var) var = var->next
#define crop_data(str) { *(str) = '\0' ; (str)++; }

static char *app = "RealTime";
static char *uapp = "RealTimeUpdate";
static char *synopsis = "Realtime Data Lookup";
static char *usynopsis = "Realtime Data Rewrite";
static char *USAGE = "RealTime(<family>|<colmatch>|<value>[|<prefix>])";
static char *UUSAGE = "RealTimeUpdate(<family>|<colmatch>|<value>|<newcol>|<newval>)";
static char *desc =
"Use the RealTime config handler system to read data into channel variables.\n"
"RealTime(<family>|<colmatch>|<value>[|<prefix>])\n\n"
"All unique column names will be set as channel variables with optional prefix\n"
"to the name.  For example, a prefix of 'var_' would make the column 'name'\n"
"become the variable ${var_name}.  REALTIMECOUNT will be set with the number\n"
"of values read.\n";
static char *udesc = "Use the RealTime config handler system to update a value\n"
"RealTimeUpdate(<family>|<colmatch>|<value>|<newcol>|<newval>)\n\n"
"The column <newcol> in 'family' matching column <colmatch>=<value> will be\n"
"updated to <newval>.  REALTIMECOUNT will be set with the number of rows\n"
"updated or -1 if an error occurs.\n";


static int cli_realtime_load(int fd, int argc, char **argv) 
{
	char *header_format = "%30s  %-30s\n";
	struct ast_variable *var = NULL, *save = NULL;

	if (argc < 5) {
		ast_cli(fd, "You must supply a family name, a column to match on, and a value to match to.\n");
		return RESULT_FAILURE;
	}

	var = ast_load_realtime(argv[2], argv[3], argv[4], NULL);

	if (var) {
		save = var;
		ast_cli(fd, header_format, "Column Name", "Column Value");
		ast_cli(fd, header_format, "--------------------", "--------------------");
		while (var) {
			ast_cli(fd, header_format, var->name, var->value);
			var = var->next;
		}
		ast_variables_destroy(save);
	} else {
		ast_cli(fd, "No rows found matching search criteria.\n");
	}
	return RESULT_SUCCESS;
}

static int cli_realtime_update(int fd, int argc, char **argv) {
	int res = 0;

	if(argc<7) {
		ast_cli(fd, "You must supply a family name, a column to update on, a new value, column to match, and value to to match.\n");
		ast_cli(fd, "Ex: realtime update sipfriends name bobsphone port 4343\n will execute SQL as UPDATE sipfriends SET port = 4343 WHERE name = bobsphone\n");
		return RESULT_FAILURE;
	}

	res = ast_update_realtime(argv[2], argv[3], argv[4], argv[5], argv[6], NULL);

	if(res < 0) {
		ast_cli(fd, "Failed to update. Check the debug log for possible SQL related entries.\n");
		return RESULT_SUCCESS;
	}

       ast_cli(fd, "Updated %d RealTime record%s.\n", res, (res != 1) ? "s" : "");

	return RESULT_SUCCESS;
}

static char cli_realtime_load_usage[] =
"Usage: realtime load <family> <colmatch> <value>\n"
"       Prints out a list of variables using the RealTime driver.\n";

static char cli_realtime_update_usage[] =
"Usage: realtime update <family> <colmatch> <value>\n"
"       Update a single variable using the RealTime driver.\n";

static struct ast_cli_entry cli_realtime[] = {
	{ { "realtime", "load", NULL, NULL },
	cli_realtime_load, "Used to print out RealTime variables.",
	cli_realtime_load_usage, NULL },

	{ { "realtime", "update", NULL, NULL },
	cli_realtime_update, "Used to update RealTime variables.",
	cli_realtime_update_usage, NULL },
};

static int realtime_update_exec(struct ast_channel *chan, void *data) 
{
	char *family=NULL, *colmatch=NULL, *value=NULL, *newcol=NULL, *newval=NULL;
	struct ast_module_user *u;
	int res = 0, count = 0;
	char countc[13];

        ast_log(LOG_WARNING, "The RealTimeUpdate application has been deprecated in favor of the REALTIME dialplan function.\n");

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR,"Invalid input: usage %s\n",UUSAGE);
		return -1;
	}
	
	u = ast_module_user_add(chan);

	family = ast_strdupa(data);
	if ((colmatch = strchr(family,'|'))) {
		crop_data(colmatch);
		if ((value = strchr(colmatch,'|'))) {
			crop_data(value);
			if ((newcol = strchr(value,'|'))) {
				crop_data(newcol);
				if ((newval = strchr(newcol,'|'))) 
					crop_data(newval);
			}
		}
	}
	if (! (family && value && colmatch && newcol && newval) ) {
		ast_log(LOG_ERROR,"Invalid input: usage %s\n",UUSAGE);
		res = -1;
	} else {
		count = ast_update_realtime(family,colmatch,value,newcol,newval,NULL);
	}

	snprintf(countc, sizeof(countc), "%d", count);
	pbx_builtin_setvar_helper(chan, "REALTIMECOUNT", countc);

	ast_module_user_remove(u);
	
	return res;
}


static int realtime_exec(struct ast_channel *chan, void *data)
{
	int res=0, count=0;
	struct ast_module_user *u;
	struct ast_variable *var, *itt;
	char *family=NULL, *colmatch=NULL, *value=NULL, *prefix=NULL, *vname=NULL;
	char countc[13];
	size_t len;
		
        ast_log(LOG_WARNING, "The RealTime application has been deprecated in favor of the REALTIME dialplan function.\n");

	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR,"Invalid input: usage %s\n",USAGE);
		return -1;
	}
	
	u = ast_module_user_add(chan);

	family = ast_strdupa(data);
	if ((colmatch = strchr(family,'|'))) {
		crop_data(colmatch);
		if ((value = strchr(colmatch,'|'))) {
			crop_data(value);
			if ((prefix = strchr(value,'|')))
				crop_data(prefix);
		}
	}
	if (! (family && value && colmatch) ) {
		ast_log(LOG_ERROR,"Invalid input: usage %s\n",USAGE);
		res = -1;
	} else {
		if (option_verbose > 3)
			ast_verbose(VERBOSE_PREFIX_4"Realtime Lookup: family:'%s' colmatch:'%s' value:'%s'\n",family,colmatch,value);
		if ((var = ast_load_realtime(family, colmatch, value, NULL))) {
			for (itt = var; itt; itt = itt->next) {
				if(prefix) {
					len = strlen(prefix) + strlen(itt->name) + 2;
					vname = alloca(len);
					snprintf(vname,len,"%s%s",prefix,itt->name);
					
				} else 
					vname = itt->name;

				pbx_builtin_setvar_helper(chan, vname, itt->value);
				count++;
			}
			ast_variables_destroy(var);
		} else if (option_verbose > 3)
			ast_verbose(VERBOSE_PREFIX_4"No Realtime Matches Found.\n");
	}
	snprintf(countc, sizeof(countc), "%d", count);
	pbx_builtin_setvar_helper(chan, "REALTIMECOUNT", countc);
	
	ast_module_user_remove(u);
	return res;
}

static int unload_module(void)
{
	int res;

	ast_cli_unregister_multiple(cli_realtime, sizeof(cli_realtime) / sizeof(struct ast_cli_entry));
	res = ast_unregister_application(uapp);
	res |= ast_unregister_application(app);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	int res;

	ast_cli_register_multiple(cli_realtime, sizeof(cli_realtime) / sizeof(struct ast_cli_entry));
	res = ast_register_application(uapp, realtime_update_exec, usynopsis, udesc);
	res |= ast_register_application(app, realtime_exec, synopsis, desc);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Realtime Data Lookup/Rewrite");
