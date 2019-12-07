/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
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
 * \brief Background DNS update manager
 */

#ifndef _ASTERISK_DNSMGR_H
#define _ASTERISK_DNSMGR_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <netinet/in.h>

struct ast_dnsmgr_entry;

struct ast_dnsmgr_entry *ast_dnsmgr_get(const char *name, struct in_addr *result);

void ast_dnsmgr_release(struct ast_dnsmgr_entry *entry);

int ast_dnsmgr_lookup(const char *name, struct in_addr *result, struct ast_dnsmgr_entry **dnsmgr);

/*!
 * \brief Force a refresh of a dnsmgr entry
 *
 * \retval non-zero if the result is different than the previous result
 * \retval zero if the result is the same as the previous result 
 */
int ast_dnsmgr_refresh(struct ast_dnsmgr_entry *entry);

/*!
 * \brief Check is see if a dnsmgr entry has changed
 *
 * \retval non-zero if the dnsmgr entry has changed since the last call to
 *                  this function
 * \retval zero     if the dnsmgr entry has not changed since the last call to
 *                  this function
 */
int ast_dnsmgr_changed(struct ast_dnsmgr_entry *entry);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif /* c_plusplus */

#endif /* ASTERISK_DNSMGR_H */
