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
 * \brief ASTdb Management
 *
 * \author Mark Spencer <markster@digium.com> 
 *
 * \note DB3 is licensed under Sleepycat Public License and is thus incompatible
 * with GPL.  To avoid having to make another exception (and complicate 
 * licensing even further) we elect to use DB1 which is BSD licensed 
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 182449 $")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>

#include "asterisk/channel.h"
#include "asterisk/file.h"
#include "asterisk/app.h"
#include "asterisk/dsp.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/astdb.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/manager.h"
#include "db1-ast/include/db.h"

#ifdef __CYGWIN__
#define dbopen __dbopen
#endif

static DB *astdb;
AST_MUTEX_DEFINE_STATIC(dblock);

static int dbinit(void) 
{
	if (!astdb && !(astdb = dbopen((char *)ast_config_AST_DB, O_CREAT | O_RDWR, 0664, DB_BTREE, NULL))) {
		ast_log(LOG_WARNING, "Unable to open Asterisk database '%s': %s\n", ast_config_AST_DB, strerror(errno));
		return -1;
	}
	return 0;
}


static inline int keymatch(const char *key, const char *prefix)
{
	int preflen = strlen(prefix);
	if (!preflen)
		return 1;
	if (!strcasecmp(key, prefix))
		return 1;
	if ((strlen(key) > preflen) && !strncasecmp(key, prefix, preflen)) {
		if (key[preflen] == '/')
			return 1;
	}
	return 0;
}

static inline int subkeymatch(const char *key, const char *suffix)
{
	int suffixlen = strlen(suffix);
	if (suffixlen) {
		const char *subkey = key + strlen(key) - suffixlen;
		if (subkey < key)
			return 0;
		if (!strcasecmp(subkey, suffix))
			return 1;
	}
	return 0;
}

int ast_db_deltree(const char *family, const char *keytree)
{
	char prefix[256];
	DBT key, data;
	char *keys;
	int res;
	int pass;
	
	if (family) {
		if (keytree) {
			snprintf(prefix, sizeof(prefix), "/%s/%s", family, keytree);
		} else {
			snprintf(prefix, sizeof(prefix), "/%s", family);
		}
	} else if (keytree) {
		return -1;
	} else {
		prefix[0] = '\0';
	}
	
	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		return -1;
	}
	
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	pass = 0;
	while (!(res = astdb->seq(astdb, &key, &data, pass++ ? R_NEXT : R_FIRST))) {
		if (key.size) {
			keys = key.data;
			keys[key.size - 1] = '\0';
		} else {
			keys = "<bad key>";
		}
		if (keymatch(keys, prefix)) {
			astdb->del(astdb, &key, 0);
		}
	}
	astdb->sync(astdb, 0);
	ast_mutex_unlock(&dblock);
	return 0;
}

int ast_db_put(const char *family, const char *keys, char *value)
{
	char fullkey[256];
	DBT key, data;
	int res, fullkeylen;

	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		return -1;
	}

	fullkeylen = snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, keys);
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	key.data = fullkey;
	key.size = fullkeylen + 1;
	data.data = value;
	data.size = strlen(value) + 1;
	res = astdb->put(astdb, &key, &data, 0);
	astdb->sync(astdb, 0);
	ast_mutex_unlock(&dblock);
	if (res)
		ast_log(LOG_WARNING, "Unable to put value '%s' for key '%s' in family '%s'\n", value, keys, family);
	return res;
}

int ast_db_get(const char *family, const char *keys, char *value, int valuelen)
{
	char fullkey[256] = "";
	DBT key, data;
	int res, fullkeylen;

	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		return -1;
	}

	fullkeylen = snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, keys);
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	memset(value, 0, valuelen);
	key.data = fullkey;
	key.size = fullkeylen + 1;

	res = astdb->get(astdb, &key, &data, 0);

	/* Be sure to NULL terminate our data either way */
	if (res) {
		if (option_debug)
			ast_log(LOG_DEBUG, "Unable to find key '%s' in family '%s'\n", keys, family);
	} else {
#if 0
		printf("Got value of size %d\n", data.size);
#endif
		if (data.size) {
			((char *)data.data)[data.size - 1] = '\0';
			/* Make sure that we don't write too much to the dst pointer or we don't read too much from the source pointer */
			ast_copy_string(value, data.data, (valuelen > data.size) ? data.size : valuelen);
		} else {
			ast_log(LOG_NOTICE, "Strange, empty value for /%s/%s\n", family, keys);
		}
	}

	/* Data is not fully isolated for concurrency, so the lock must be extended
	 * to after the copy to the output buffer. */
	ast_mutex_unlock(&dblock);

	return res;
}

int ast_db_del(const char *family, const char *keys)
{
	char fullkey[256];
	DBT key;
	int res, fullkeylen;

	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		return -1;
	}
	
	fullkeylen = snprintf(fullkey, sizeof(fullkey), "/%s/%s", family, keys);
	memset(&key, 0, sizeof(key));
	key.data = fullkey;
	key.size = fullkeylen + 1;
	
	res = astdb->del(astdb, &key, 0);
	astdb->sync(astdb, 0);
	
	ast_mutex_unlock(&dblock);

	if (res && option_debug)
		ast_log(LOG_DEBUG, "Unable to find key '%s' in family '%s'\n", keys, family);
	return res;
}

static int database_put(int fd, int argc, char *argv[])
{
	int res;
	if (argc != 5)
		return RESULT_SHOWUSAGE;
	res = ast_db_put(argv[2], argv[3], argv[4]);
	if (res)  {
		ast_cli(fd, "Failed to update entry\n");
	} else {
		ast_cli(fd, "Updated database successfully\n");
	}
	return RESULT_SUCCESS;
}

static int database_get(int fd, int argc, char *argv[])
{
	int res;
	char tmp[256];
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	res = ast_db_get(argv[2], argv[3], tmp, sizeof(tmp));
	if (res) {
		ast_cli(fd, "Database entry not found.\n");
	} else {
		ast_cli(fd, "Value: %s\n", tmp);
	}
	return RESULT_SUCCESS;
}

static int database_del(int fd, int argc, char *argv[])
{
	int res;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	res = ast_db_del(argv[2], argv[3]);
	if (res) {
		ast_cli(fd, "Database entry does not exist.\n");
	} else {
		ast_cli(fd, "Database entry removed.\n");
	}
	return RESULT_SUCCESS;
}

static int database_deltree(int fd, int argc, char *argv[])
{
	int res;
	if ((argc < 3) || (argc > 4))
		return RESULT_SHOWUSAGE;
	if (argc == 4) {
		res = ast_db_deltree(argv[2], argv[3]);
	} else {
		res = ast_db_deltree(argv[2], NULL);
	}
	if (res) {
		ast_cli(fd, "Database entries do not exist.\n");
	} else {
		ast_cli(fd, "Database entries removed.\n");
	}
	return RESULT_SUCCESS;
}

static int database_show(int fd, int argc, char *argv[])
{
	char prefix[256];
	DBT key, data;
	char *keys, *values;
	int res;
	int pass;

	if (argc == 4) {
		/* Family and key tree */
		snprintf(prefix, sizeof(prefix), "/%s/%s", argv[2], argv[3]);
	} else if (argc == 3) {
		/* Family only */
		snprintf(prefix, sizeof(prefix), "/%s", argv[2]);
	} else if (argc == 2) {
		/* Neither */
		prefix[0] = '\0';
	} else {
		return RESULT_SHOWUSAGE;
	}
	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		ast_cli(fd, "Database unavailable\n");
		return RESULT_SUCCESS;	
	}
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	pass = 0;
	while (!(res = astdb->seq(astdb, &key, &data, pass++ ? R_NEXT : R_FIRST))) {
		if (key.size) {
			keys = key.data;
			keys[key.size - 1] = '\0';
		} else {
			keys = "<bad key>";
		}
		if (data.size) {
			values = data.data;
			values[data.size - 1]='\0';
		} else {
			values = "<bad value>";
		}
		if (keymatch(keys, prefix)) {
				ast_cli(fd, "%-50s: %-25s\n", keys, values);
		}
	}
	ast_mutex_unlock(&dblock);
	return RESULT_SUCCESS;	
}

static int database_showkey(int fd, int argc, char *argv[])
{
	char suffix[256];
	DBT key, data;
	char *keys, *values;
	int res;
	int pass;

	if (argc == 3) {
		/* Key only */
		snprintf(suffix, sizeof(suffix), "/%s", argv[2]);
	} else {
		return RESULT_SHOWUSAGE;
	}
	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		ast_cli(fd, "Database unavailable\n");
		return RESULT_SUCCESS;	
	}
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	pass = 0;
	while (!(res = astdb->seq(astdb, &key, &data, pass++ ? R_NEXT : R_FIRST))) {
		if (key.size) {
			keys = key.data;
			keys[key.size - 1] = '\0';
		} else {
			keys = "<bad key>";
		}
		if (data.size) {
			values = data.data;
			values[data.size - 1]='\0';
		} else {
			values = "<bad value>";
		}
		if (subkeymatch(keys, suffix)) {
				ast_cli(fd, "%-50s: %-25s\n", keys, values);
		}
	}
	ast_mutex_unlock(&dblock);
	return RESULT_SUCCESS;	
}

struct ast_db_entry *ast_db_gettree(const char *family, const char *keytree)
{
	char prefix[256];
	DBT key, data;
	char *keys, *values;
	int values_len;
	int res;
	int pass;
	struct ast_db_entry *last = NULL;
	struct ast_db_entry *cur, *ret=NULL;

	if (!ast_strlen_zero(family)) {
		if (!ast_strlen_zero(keytree)) {
			/* Family and key tree */
			snprintf(prefix, sizeof(prefix), "/%s/%s", family, keytree);
		} else {
			/* Family only */
			snprintf(prefix, sizeof(prefix), "/%s", family);
		}
	} else {
		prefix[0] = '\0';
	}
	ast_mutex_lock(&dblock);
	if (dbinit()) {
		ast_mutex_unlock(&dblock);
		ast_log(LOG_WARNING, "Database unavailable\n");
		return NULL;	
	}
	memset(&key, 0, sizeof(key));
	memset(&data, 0, sizeof(data));
	pass = 0;
	while (!(res = astdb->seq(astdb, &key, &data, pass++ ? R_NEXT : R_FIRST))) {
		if (key.size) {
			keys = key.data;
			keys[key.size - 1] = '\0';
		} else {
			keys = "<bad key>";
		}
		if (data.size) {
			values = data.data;
			values[data.size - 1] = '\0';
		} else {
			values = "<bad value>";
		}
		values_len = strlen(values) + 1;
		if (keymatch(keys, prefix) && (cur = ast_malloc(sizeof(*cur) + strlen(keys) + 1 + values_len))) {
			cur->next = NULL;
			cur->key = cur->data + values_len;
			strcpy(cur->data, values);
			strcpy(cur->key, keys);
			if (last) {
				last->next = cur;
			} else {
				ret = cur;
			}
			last = cur;
		}
	}
	ast_mutex_unlock(&dblock);
	return ret;	
}

void ast_db_freetree(struct ast_db_entry *dbe)
{
	struct ast_db_entry *last;
	while (dbe) {
		last = dbe;
		dbe = dbe->next;
		free(last);
	}
}

static char database_show_usage[] =
"Usage: database show [family [keytree]]\n"
"       Shows Asterisk database contents, optionally restricted\n"
"to a given family, or family and keytree.\n";

static char database_showkey_usage[] =
"Usage: database showkey <keytree>\n"
"       Shows Asterisk database contents, restricted to a given key.\n";

static char database_put_usage[] =
"Usage: database put <family> <key> <value>\n"
"       Adds or updates an entry in the Asterisk database for\n"
"a given family, key, and value.\n";

static char database_get_usage[] =
"Usage: database get <family> <key>\n"
"       Retrieves an entry in the Asterisk database for a given\n"
"family and key.\n";

static char database_del_usage[] =
"Usage: database del <family> <key>\n"
"       Deletes an entry in the Asterisk database for a given\n"
"family and key.\n";

static char database_deltree_usage[] =
"Usage: database deltree <family> [keytree]\n"
"       Deletes a family or specific keytree within a family\n"
"in the Asterisk database.\n";

struct ast_cli_entry cli_database[] = {
	{ { "database", "show", NULL },
	database_show, "Shows database contents",
	database_show_usage },

	{ { "database", "showkey", NULL },
	database_showkey, "Shows database contents",
	database_showkey_usage },

	{ { "database", "get", NULL },
	database_get, "Gets database value",
	database_get_usage },

	{ { "database", "put", NULL },
	database_put, "Adds/updates database value",
	database_put_usage },

	{ { "database", "del", NULL },
	database_del, "Removes database key/value",
	database_del_usage },

	{ { "database", "deltree", NULL },
	database_deltree, "Removes database keytree/values",
	database_deltree_usage },
};

static int manager_dbput(struct mansession *s, const struct message *m)
{
	const char *family = astman_get_header(m, "Family");
	const char *key = astman_get_header(m, "Key");
	const char *val = astman_get_header(m, "Val");
	int res;

	if (ast_strlen_zero(family)) {
		astman_send_error(s, m, "No family specified");
		return 0;
	}
	if (ast_strlen_zero(key)) {
		astman_send_error(s, m, "No key specified");
		return 0;
	}

	res = ast_db_put(family, key, (char *) S_OR(val, ""));
	if (res) {
		astman_send_error(s, m, "Failed to update entry");
	} else {
		astman_send_ack(s, m, "Updated database successfully");
	}
	return 0;
}

static int manager_dbget(struct mansession *s, const struct message *m)
{
	const char *id = astman_get_header(m,"ActionID");
	char idText[256] = "";
	const char *family = astman_get_header(m, "Family");
	const char *key = astman_get_header(m, "Key");
	char tmp[256];
	int res;

	if (ast_strlen_zero(family)) {
		astman_send_error(s, m, "No family specified.");
		return 0;
	}
	if (ast_strlen_zero(key)) {
		astman_send_error(s, m, "No key specified.");
		return 0;
	}

	if (!ast_strlen_zero(id))
		snprintf(idText, sizeof(idText) ,"ActionID: %s\r\n", id);

	res = ast_db_get(family, key, tmp, sizeof(tmp));
	if (res) {
		astman_send_error(s, m, "Database entry not found");
	} else {
		astman_send_ack(s, m, "Result will follow");
		astman_append(s, "Event: DBGetResponse\r\n"
				"Family: %s\r\n"
				"Key: %s\r\n"
				"Val: %s\r\n"
				"%s"
				"\r\n",
				family, key, tmp, idText);
	}
	return 0;
}

int astdb_init(void)
{
	dbinit();
	ast_cli_register_multiple(cli_database, sizeof(cli_database) / sizeof(struct ast_cli_entry));
	ast_manager_register("DBGet", EVENT_FLAG_SYSTEM, manager_dbget, "Get DB Entry");
	ast_manager_register("DBPut", EVENT_FLAG_SYSTEM, manager_dbput, "Put DB Entry");
	return 0;
}
