/*
 * libpri: An implementation of Primary Rate ISDN
 *
 * Written by Mark Spencer <markster@digium.com>
 *
 * Copyright (C) 2001, Digium, Inc.
 * All Rights Reserved.
 */

/*
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 *
 * In addition, when this program is distributed with Asterisk in
 * any form that would qualify as a 'combined work' or as a
 * 'derivative work' (but not mere aggregation), you can redistribute
 * and/or modify the combination under the terms of the license
 * provided with that copy of Asterisk, instead of the license
 * terms granted here.
 */
 
#ifndef _PRI_TIMERS_H
#define _PRI_TIMERS_H

/* -1 means we dont currently support the timer/counter */
#define PRI_TIMERS_DEFAULT { \
				3,		/* N200 */ \
				-1,		/* N201 */ \
				3,		/* N202 */ \
				7,		/* K */ \
				1000,	/* T200 */ \
				-1,		/* T201 */ \
				10000,	/* T202 */ \
				10000,	/* T203 */ \
				-1,		/* T300 */ \
				-1,		/* T301 */ \
				-1,		/* T302 */ \
				-1,		/* T303 */ \
				-1,		/* T304 */ \
				30000,	/* T305 */ \
				-1,		/* T306 */ \
				-1,		/* T307 */ \
				4000,	/* T308 */ \
				-1,		/* T309 */ \
				-1,		/* T310 */ \
				4000,	/* T313 */ \
				-1,		/* T314 */ \
				-1,		/* T316 */ \
				-1,		/* T317 */ \
				-1,		/* T318 */ \
				-1,		/* T319 */ \
				-1,		/* T320 */ \
				-1,		/* T321 */ \
				-1,		/* T322 */ \
				2500,	/* TM20 - Q.921 Appendix IV */ \
				3,		/* NM20 - Q.921 Appendix IV */ \
			}

/* XXX Only our default timers are setup now XXX */
#define PRI_TIMERS_UNKNOWN PRI_TIMERS_DEFAULT
#define PRI_TIMERS_NI2 PRI_TIMERS_DEFAULT
#define PRI_TIMERS_DMS100 PRI_TIMERS_DEFAULT
#define PRI_TIMERS_LUCENT5E PRI_TIMERS_DEFAULT
#define PRI_TIMERS_ATT4ESS PRI_TIMERS_DEFAULT
#define PRI_TIMERS_EUROISDN_E1 PRI_TIMERS_DEFAULT
#define PRI_TIMERS_EUROISDN_T1 PRI_TIMERS_DEFAULT
#define PRI_TIMERS_NI1 PRI_TIMERS_DEFAULT
#define PRI_TIMERS_GR303_EOC PRI_TIMERS_DEFAULT
#define PRI_TIMERS_GR303_TMC PRI_TIMERS_DEFAULT
#define PRI_TIMERS_QSIG PRI_TIMERS_DEFAULT
#define __PRI_TIMERS_GR303_EOC_INT PRI_TIMERS_DEFAULT
#define __PRI_TIMERS_GR303_TMC_INT PRI_TIMERS_DEFAULT

#define PRI_TIMERS_ALL {	PRI_TIMERS_UNKNOWN, \
				PRI_TIMERS_NI2, \
				PRI_TIMERS_DMS100, \
				PRI_TIMERS_LUCENT5E, \
				PRI_TIMERS_ATT4ESS, \
				PRI_TIMERS_EUROISDN_E1, \
				PRI_TIMERS_EUROISDN_T1, \
				PRI_TIMERS_NI1, \
				PRI_TIMERS_QSIG, \
				PRI_TIMERS_GR303_EOC, \
				PRI_TIMERS_GR303_TMC, \
				__PRI_TIMERS_GR303_EOC_INT, \
				__PRI_TIMERS_GR303_TMC_INT, \
			}

#endif
