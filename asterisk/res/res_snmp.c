/*
 * Copyright (C) 2006 Voop as
 * Thorsten Lockert <tholo@voop.as>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief SNMP Agent / SubAgent support for Asterisk
 *
 * \author Thorsten Lockert <tholo@voop.as>
 */

/*** MODULEINFO
	<depend>netsnmp</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 182808 $")

#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"

#include "snmp/agent.h"

#define	MODULE_DESCRIPTION	"SNMP [Sub]Agent for Asterisk"

int res_snmp_agentx_subagent;
int res_snmp_dont_stop;
int res_snmp_enabled;

static pthread_t thread = AST_PTHREADT_NULL;

static int load_config(void)
{
	struct ast_variable *var;
	struct ast_config *cfg;
	char *cat;

	res_snmp_enabled = 0;
	res_snmp_agentx_subagent = 1;
	cfg = ast_config_load("res_snmp.conf");
	if (!cfg) {
		ast_log(LOG_WARNING, "Could not load res_snmp.conf\n");
		return 0;
	}
	cat = ast_category_browse(cfg, NULL);
	while (cat) {
		var = ast_variable_browse(cfg, cat);

		if (strcasecmp(cat, "general") == 0) {
			while (var) {
				if (strcasecmp(var->name, "subagent") == 0) {
					if (ast_true(var->value))
						res_snmp_agentx_subagent = 1;
					else if (ast_false(var->value))
						res_snmp_agentx_subagent = 0;
					else {
						ast_log(LOG_ERROR, "Value '%s' does not evaluate to true or false.\n", var->value);
						ast_config_destroy(cfg);
						return 1;
					}
				} else if (strcasecmp(var->name, "enabled") == 0) {
					res_snmp_enabled = ast_true(var->value);
				} else {
					ast_log(LOG_ERROR, "Unrecognized variable '%s' in category '%s'\n", var->name, cat);
					ast_config_destroy(cfg);
					return 1;
				}
				var = var->next;
			}
		} else {
			ast_log(LOG_ERROR, "Unrecognized category '%s'\n", cat);
			ast_config_destroy(cfg);
			return 1;
		}

		cat = ast_category_browse(cfg, cat);
	}
	ast_config_destroy(cfg);
	return 1;
}

static int load_module(void)
{
	if(!load_config())
		return AST_MODULE_LOAD_DECLINE;

	ast_verbose(VERBOSE_PREFIX_1 "Loading [Sub]Agent Module\n");

	res_snmp_dont_stop = 1;
	if (res_snmp_enabled)
		return ast_pthread_create_background(&thread, NULL, agent_thread, NULL);
	else
		return 0;
}

static int unload_module(void)
{
	ast_verbose(VERBOSE_PREFIX_1 "Unloading [Sub]Agent Module\n");

	res_snmp_dont_stop = 0;
	return ((thread != AST_PTHREADT_NULL) ? pthread_join(thread, NULL) : 0);
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "SNMP [Sub]Agent for Asterisk",
		.load = load_module,
		.unload = unload_module,
		);
