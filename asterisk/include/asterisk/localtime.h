/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Tilghman Lesher <tlesher@vcch.com>
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
 * \brief Custom localtime functions for multiple timezones
 */

#ifndef _ASTERISK_LOCALTIME_H
#define _ASTERISK_LOCALTIME_H

struct tm *ast_localtime(const time_t *timep, struct tm *p_tm, const char *zone);
time_t ast_mktime(struct tm * const tmp, const char *zone);

#endif /* _ASTERISK_LOCALTIME_H */
