/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 * Copyright (C) 2004 - 2005, Anthony Minessale II
 * Copyright (C) 2006, Tilghman Lesher
 *
 * Mark Spencer <markster@digium.com>
 * Anthony Minessale <anthmct@yahoo.com>
 * Tilghman Lesher <res_odbc_200603@the-tilghman.com>
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
 * \brief ODBC resource manager
 */

#ifndef _ASTERISK_RES_ODBC_H
#define _ASTERISK_RES_ODBC_H

#include <sql.h>
#include <sqlext.h>
#include <sqltypes.h>

typedef enum { ODBC_SUCCESS=0, ODBC_FAIL=-1} odbc_status;

struct odbc_obj {
	ast_mutex_t lock;
	SQLHDBC  con;                   /* ODBC Connection Handle */
	struct odbc_class *parent;      /* Information about the connection is protected */
	struct timeval last_used;
	unsigned int used:1;
	unsigned int up:1;
	AST_LIST_ENTRY(odbc_obj) list;
};

/* functions */

/*! \brief Executes a prepared statement handle
 * \param obj The non-NULL result of odbc_request_obj()
 * \param stmt The prepared statement handle
 * \return Returns 0 on success or -1 on failure
 *
 * This function was originally designed simply to execute a prepared
 * statement handle and to retry if the initial execution failed.
 * Unfortunately, it did this by disconnecting and reconnecting the database
 * handle which on most databases causes the statement handle to become
 * invalid.  Therefore, this method has been deprecated in favor of
 * odbc_prepare_and_execute() which allows the statement to be prepared
 * multiple times, if necessary, in case of a loss of connection.
 *
 * This function really only ever worked with MySQL, where the statement handle is
 * not prepared on the server.  If you are not using MySQL, you should avoid it.
 */
int ast_odbc_smart_execute(struct odbc_obj *obj, SQLHSTMT stmt) __attribute__((deprecated));

/*! \brief Retrieves a connected ODBC object
 * \param name The name of the ODBC class for which a connection is needed.
 * \param check Whether to ensure that a connection is valid before returning the handle.  Usually unnecessary.
 * \return Returns an ODBC object or NULL if there is no connection available with the requested name.
 *
 * Connection classes may, in fact, contain multiple connection handles.  If
 * the connection is pooled, then each connection will be dedicated to the
 * thread which requests it.  Note that all connections should be released
 * when the thread is done by calling odbc_release_obj(), below.
 */
struct odbc_obj *ast_odbc_request_obj(const char *name, int check);

/*! \brief Releases an ODBC object previously allocated by odbc_request_obj()
 * \param obj The ODBC object
 */
void ast_odbc_release_obj(struct odbc_obj *obj);

/*! \brief Checks an ODBC object to ensure it is still connected
 * \param obj The ODBC object
 * \return Returns 0 if connected, -1 otherwise.
 */
int ast_odbc_sanity_check(struct odbc_obj *obj);

/*! \brief Checks if the database natively supports backslash as an escape character.
 * \param obj The ODBC object
 * \return Returns 1 if backslash is a native escape character, 0 if an ESCAPE clause is needed to support '\'
 */
int ast_odbc_backslash_is_escape(struct odbc_obj *obj);

/*! \brief Prepares, executes, and returns the resulting statement handle.
 * \param obj The ODBC object
 * \param prepare_cb A function callback, which, when called, should return a statement handle prepared, with any necessary parameters or result columns bound.
 * \param data A parameter to be passed to the prepare_cb parameter function, indicating which statement handle is to be prepared.
 * \return Returns a statement handle or NULL on error.
 */
SQLHSTMT ast_odbc_prepare_and_execute(struct odbc_obj *obj, SQLHSTMT (*prepare_cb)(struct odbc_obj *obj, void *data), void *data);

#endif /* _ASTERISK_RES_ODBC_H */
