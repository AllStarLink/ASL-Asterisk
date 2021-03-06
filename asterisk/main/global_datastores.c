/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2007, Digium, Inc.
 *
 * Mark Michelson <mmichelson@digium.com>
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
 * \brief globally-accessible datastore information and callbacks
 *
 * \author Mark Michelson <mmichelson@digium.com>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__,"$Revision$")

#include "asterisk/global_datastores.h"
#include "asterisk/linkedlists.h"

static void dialed_interface_destroy(void *data)
{
	struct ast_dialed_interface *di = NULL;
	AST_LIST_HEAD(, ast_dialed_interface) *dialed_interface_list = data;
	
	if (!dialed_interface_list) {
		return;
	}

	AST_LIST_LOCK(dialed_interface_list);
	while ((di = AST_LIST_REMOVE_HEAD(dialed_interface_list, list)))
		ast_free(di);
	AST_LIST_UNLOCK(dialed_interface_list);

	AST_LIST_HEAD_DESTROY(dialed_interface_list);
	ast_free(dialed_interface_list);
}

static void *dialed_interface_duplicate(void *data)
{
	struct ast_dialed_interface *di = NULL;
	AST_LIST_HEAD(, ast_dialed_interface) *old_list;
	AST_LIST_HEAD(, ast_dialed_interface) *new_list = NULL;

	if(!(old_list = data)) {
		return NULL;
	}

	if(!(new_list = ast_calloc(1, sizeof(*new_list)))) {
		return NULL;
	}

	AST_LIST_HEAD_INIT(new_list);
	AST_LIST_LOCK(old_list);
	AST_LIST_TRAVERSE(old_list, di, list) {
		struct ast_dialed_interface *di2 = ast_calloc(1, sizeof(*di2) + strlen(di->interface));
		if(!di2) {
			AST_LIST_UNLOCK(old_list);
			dialed_interface_destroy(new_list);
			return NULL;
		}
		strcpy(di2->interface, di->interface);
		AST_LIST_INSERT_TAIL(new_list, di2, list);
	}
	AST_LIST_UNLOCK(old_list);

	return new_list;
}


static void *dial_features_duplicate(void *data)
{
	struct ast_dial_features *df = data, *df_copy;

	if (!(df_copy = ast_calloc(1, sizeof(*df)))) {
		return NULL;
	}

	memcpy(df_copy, df, sizeof(*df));

	return df_copy;
}

static void dial_features_destroy(void *data) {
	struct ast_dial_features *df = data;
	if (df) {
		ast_free(df);
	}
}

const struct ast_datastore_info dialed_interface_info = {
	.type = "dialed-interface",
	.destroy = dialed_interface_destroy,
	.duplicate = dialed_interface_duplicate,
};

const struct ast_datastore_info dial_features_info = {
	.type = "dial-features",
	.destroy = dial_features_destroy,
	.duplicate = dial_features_duplicate,
};
