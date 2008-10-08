/*
 * ECHO_CAN_JP1
 *
 * by Jason Parker
 *
 * Based upon mg2ec.h - sort of.
 * This "echo can" will completely hose your audio.
 * Don't use it unless you're absolutely sure you know what you're doing.
 * 
 * Copyright (C) 2007, Digium, Inc.
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

#ifndef _JP_ECHO_H
#define _JP_ECHO_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/slab.h>
#define MALLOC(a) kmalloc((a), GFP_KERNEL)
#define FREE(a) kfree(a)
#else
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#define MALLOC(a) malloc(a)
#define FREE(a) free(a)
#endif

/* Echo canceller definition */
struct echo_can_state {
	/* an arbitrary ID for this echo can - this really should be settable from the calling channel... */
	int id;

	/* absolute time - aka. sample number index - essentially the number of samples since this can was init'ed */
	int i_d;
};

static void echo_can_init(void)
{
	printk("Zaptel Audio Hoser: JP1\n");
}

static void echo_can_identify(char *buf, size_t len)
{
	zap_copy_string(buf, "JP1", len);
}

static void echo_can_shutdown(void)
{
}

static inline void init_cc(struct echo_can_state *ec)
{
	void *ptr = ec;
	unsigned long tmp;
	/* Double-word align past end of state */
	ptr += sizeof(struct echo_can_state);
	tmp = (unsigned long)ptr;
	tmp += 3;
	tmp &= ~3L;
	ptr = (void *)tmp;
}

static inline void echo_can_free(struct echo_can_state *ec)
{
	FREE(ec);
}

static inline short echo_can_update(struct echo_can_state *ec, short iref, short isig) 
{
	static int blah = 0;

	if (blah < 2) {
		blah++;
		return 0;
	} else {
		blah = (blah + 1) % 3;
		return isig;
	}
}

static inline struct echo_can_state *echo_can_create(int len, int adaption_mode)
{
	struct echo_can_state *ec;
	ec = (struct echo_can_state *)MALLOC(sizeof(struct echo_can_state) + 4); /* align */
	if (ec) {
		memset(ec, 0, sizeof(struct echo_can_state) + 4); /* align */
		init_cc(ec);
	}
	return ec;
}

static inline int echo_can_traintap(struct echo_can_state *ec, int pos, short val)
{
	return 0;
}
#endif
