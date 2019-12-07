/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005-2006, Russell Bryant
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
 *
 * \brief public data structures and defaults for menuselect
 *
 */

#ifndef MENUSELECT_H
#define MENUSELECT_H

#include "linkedlists.h"

#define OUTPUT_MAKEOPTS_DEFAULT "menuselect.makeopts"
#define OUTPUT_MAKEDEPS_DEFAULT "menuselect.makedeps"
#define MENUSELECT_DEPS         "build_tools/menuselect-deps"

struct member;

struct depend {
	/*! the name of the dependency */
	const char *name;
	/*! if this dependency is a member, not an external object */
	struct member *member;
	/*! for linking */
	AST_LIST_ENTRY(depend) list;
};

struct conflict {
	/*! the name of the conflict */
	const char *name;
	/*! if this conflict is a member, not an external object */
	const struct member *member;
	/*! for linking */
	AST_LIST_ENTRY(conflict) list;
};

struct use {
	/*! the name of the used package */
	const char *name;
	/*! if this dependency is a member, not an external object */
	struct member *member;
	/*! for linking */
	AST_LIST_ENTRY(use) list;
};

enum failure_types {
	NO_FAILURE = 0,
	SOFT_FAILURE = 1,
	HARD_FAILURE = 2,
};

struct member {
	/*! What will be sent to the makeopts file */
	const char *name;
	/*! Display name if known */
	const char *displayname;
	/*! Default setting */
	const char *defaultenabled;
	/*! Delete these file(s) if this member changes */
	const char *remove_on_change;
	/*! This module is currently selected */
	unsigned int enabled:1;
	/*! This module was enabled when the config was loaded */
	unsigned int was_enabled:1;
	/*! This module has failed dependencies */
	unsigned int depsfailed:2;
	/*! This module has failed conflicts */
	unsigned int conflictsfailed:2;
	/*! This module's 'enabled' flag was changed by a default only */
	unsigned int was_defaulted:1;
	/*! This module is a dependency, and if it is selected then
	  we have included it in the MENUSELECT_BUILD_DEPS line
	  in the output file */
	unsigned int build_deps_output:1;
	/*! dependencies of this module */
	AST_LIST_HEAD_NOLOCK(, depend) deps;
	/*! conflicts of this module */
	AST_LIST_HEAD_NOLOCK(, conflict) conflicts;
	/*! optional packages used by this module */
	AST_LIST_HEAD_NOLOCK(, use) uses;
	/*! for making a list of modules */
	AST_LIST_ENTRY(member) list;
};

struct category {
	/*! the Makefile variable */
	const char *name;
	/*! the name displayed in the menu */
	const char *displayname;
	/*! Delete these file(s) if anything in this category changes */
	const char *remove_on_change;
	/*! Output what is selected, as opposed to not selected */
	unsigned int positive_output:1;
	/*! All choices in this category are mutually exclusive */
	unsigned int exclusive:1;
	/*! the list of possible values to be set in this variable */
	AST_LIST_HEAD_NOLOCK(, member) members;
	/*! for linking */
	AST_LIST_ENTRY(category) list;
};

extern AST_LIST_HEAD_NOLOCK(categories, category) categories;

extern const char *menu_name;

/*! This is implemented by the frontend */
int run_menu(void);

int count_categories(void);

int count_members(struct category *cat);

/*! \brief Toggle a member of a category at the specified index to enabled/disabled */
void toggle_enabled_index(struct category *cat, int index);

void toggle_enabled(struct member *mem);

/*! \brief Set a member of a category at the specified index to enabled */
void set_enabled(struct category *cat, int index);
/*! \brief Set a member of a category at the specified index to not enabled */
void clear_enabled(struct category *cat, int index);

/*! \brief Enable/Disable all members of a category as long as dependencies have been met and no conflicts are found */
void set_all(struct category *cat, int val);

/*! \brief returns non-zero if the string is not defined, or has zero length */
static inline int strlen_zero(const char *s)
{
	return (!s || (*s == '\0'));
}

#endif /* MENUSELECT_H */
