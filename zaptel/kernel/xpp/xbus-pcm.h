/*
 * Written by Oron Peled <oron@actcom.co.il>
 * Copyright (C) 2004-2007, Xorcom
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
 * This source module contains all the PCM and SYNC handling code.
 */
#ifndef	XBUS_PCM_H
#define	XBUS_PCM_H

#include "xdefs.h"
#include <linux/proc_fs.h>
#include <zaptel.h>

#ifdef	__KERNEL__

enum sync_mode {
	SYNC_MODE_NONE	= 0x00,
	SYNC_MODE_AB	= 0x01,		/* Astribank sync */
	SYNC_MODE_PLL	= 0x03,		/* Adjust XPD's PLL according to HOST */
	SYNC_MODE_QUERY	= 0x80,
};

/*
 * Abstract representation of timestamp.
 * It would (eventually) replace the hard-coded
 * timeval structs so we can migrate to better
 * time representations.
 */
struct xpp_timestamp {
	struct timeval	tv;
};

/*
 * A ticker encapsulates the timing information of some
 * abstract tick source. The following tickers are used:
 *   - Each xbus has an embedded ticker.
 *   - There is one global zaptel_ticker to represent ticks
 *     of external zaptel card (in case we want to sync
 *     from other zaptel devices).
 */
struct xpp_ticker {		/* for rate calculation */
	int			count;
	int			cycle;
	struct xpp_timestamp	first_sample;
	struct xpp_timestamp	last_sample;
	int			tick_period;	/* usec/tick */
	spinlock_t		lock;
};

/*
 * xpp_drift represent the measurements of the offset between an
 * xbus ticker to a reference ticker.
 */
struct xpp_drift {
	int			wanted_offset;		/* fixed */
	int			delta_tick;		/* from ref_ticker */
	int			lost_ticks;		/* occurances */
	int			lost_tick_count;
	int			delta_max;
	int			delta_min;
	int			median;			/* (max + min) / 2	*/
	int			jitter;			/* max - min		*/
	int			calc_drift;
	spinlock_t		lock;
};

void xpp_drift_init(xbus_t *xbus);

static inline long usec_diff(const struct timeval *tv1, const struct timeval *tv2)
{
	long			diff_sec;
	long			diff_usec;

	diff_sec = tv1->tv_sec - tv2->tv_sec;
	diff_usec = tv1->tv_usec - tv2->tv_usec;
	return diff_sec * 1000000 + diff_usec;
}


int		xbus_pcm_init(struct proc_dir_entry *top);
void		xbus_pcm_shutdown(void);
int		send_pcm_frame(xbus_t *xbus, xframe_t *xframe);
void		pcm_recompute(xpd_t *xpd, xpp_line_t tmp_pcm_mask);
void		__pcm_recompute(xpd_t *xpd, xpp_line_t tmp_pcm_mask); /* non locking */
void		xframe_receive_pcm(xbus_t *xbus, xframe_t *xframe);
void		generic_card_pcm_fromspan(xbus_t *xbus, xpd_t *xpd, xpp_line_t lines, xpacket_t *pack);
void		generic_card_pcm_tospan(xbus_t *xbus, xpd_t *xpd, xpacket_t *pack);
void		fill_beep(u_char *buf, int num, int duration);
const char	*sync_mode_name(enum sync_mode mode);
void		xbus_set_command_timer(xbus_t *xbus, bool on);
void		xbus_request_sync(xbus_t *xbus, enum sync_mode mode);
void		got_new_syncer(xbus_t *xbus, enum sync_mode mode, int drift);
int		xbus_command_queue_tick(xbus_t *xbus);
void		xbus_reset_counters(xbus_t *xbus);
void		elect_syncer(const char *msg);
int		xpp_echocan(struct zt_chan *chan, int len);
#ifdef	ZAPTEL_SYNC_TICK
int		zaptel_sync_tick(struct zt_span *span, int is_master);
#endif

#ifdef	XPP_EC_CHUNK
extern int xpp_ec;
#else
#define	xpp_ec	0
#endif

#ifdef	DEBUG_PCMTX
extern int	pcmtx;
extern int	pcmtx_chan;
#endif

#endif	/* __KERNEL__ */

#endif	/* XBUS_PCM_H */

