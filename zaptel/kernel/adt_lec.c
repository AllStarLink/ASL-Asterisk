/*
 * ADT Line Echo Canceller Parameter Parsing
 *
 * Copyright (C) 2008 Digium, Inc.
 *
 * Kevin P. Fleming <kpfleming@digium.com>
 *
 * All rights reserved.
 *
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
 */

#ifndef _ADT_LEC_C
#define _ADT_LEC_C

#include <linux/ctype.h>

static inline void adt_lec_init_defaults(struct adt_lec_params *params, __u32 tap_length)
{
	memset(params, 0, sizeof(*params));
	params->tap_length = tap_length;
}

static int adt_lec_parse_params(struct adt_lec_params *params, struct zt_echocanparams *ecp, struct zt_echocanparam *p)
{
	unsigned int x;
	char *c;

	for (x = 0; x < ecp->param_count; x++) {
		for (c = p[x].name; *c; c++)
			*c = tolower(*c);
		if (!strcmp(p[x].name, "nlp_type")) {
			switch (p[x].value) {
			case ADT_LEC_NLP_OFF:
			case ADT_LEC_NLP_MUTE:
			case ADT_LEC_RANDOM_NOISE:
			case ADT_LEC_HOTH_NOISE:
			case ADT_LEC_SUPPRESS:
				params->nlp_type = p[x].value;
				break;
			default:
				return -EINVAL;
			}
		} else if (!strcmp(p[x].name, "nlp_thresh")) {
			params->nlp_threshold = p[x].value;
		} else if (!strcmp(p[x].name, "nlp_suppress")) {
			params->nlp_max_suppress = p[x].value;
		} else {
			return -EINVAL;
		}
	}

	return 0;
}

#endif /* _ADT_LEC_C */
