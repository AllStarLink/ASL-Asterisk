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
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#  warning "This module is tested only with 2.6 kernels"
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include "xbus-pcm.h"
#include "xbus-core.h"
#include "xpp_zap.h"
#include "zap_debug.h"
#include "parport_debug.h"

static const char rcsid[] = "$Id: xbus-pcm.c 4474 2008-08-11 14:00:30Z tzafrir $";

extern int debug;
#ifdef	XPP_EC_CHUNK
#include "supress/ec_xpp.h"
DEF_PARM_BOOL(xpp_ec, 0, 0444, "Do we use our own (1) or Zaptel's (0) echo canceller");
#endif
#ifdef	OPTIMIZE_CHANMUTE
static DEF_PARM_BOOL(optimize_chanmute, 1, 0644, "Optimize by muting inactive channels");
#endif

static DEF_PARM(int, disable_pcm, 0, 0644, "Disable all PCM transmissions");
#ifdef	DEBUG_PCMTX
DEF_PARM(int, pcmtx, -1, 0644, "Forced PCM value to transmit (negative to disable)");
DEF_PARM(int, pcmtx_chan, 0, 0644, "channel to force PCM value");
#endif
static DEF_PARM_BOOL(disable_pll_sync, 0, 0644, "Disable automatic adjustment of AB clocks");

static xbus_t			*syncer;		/* current syncer */
static atomic_t			xpp_tick_counter = ATOMIC_INIT(0);
static struct xpp_ticker	zaptel_ticker;
/*
 * The ref_ticker points to the current referece tick source.
 * I.e: one of our AB or zaptel_ticker
 */
static struct xpp_ticker	*ref_ticker = NULL;
static spinlock_t		ref_ticker_lock = SPIN_LOCK_UNLOCKED;
static bool			force_zaptel_sync = 0;	/* from "/proc/xpp/sync" */
static xbus_t			*global_ticker;
static struct xpp_ticker	global_ticks_series;

#define	PROC_SYNC		"sync"
#define	BIG_TICK_INTERVAL	1000
#define	SYNC_ADJ_MAX		63	/* maximal firmware drift unit (63) */
/*
 * The USB bulk endpoints have a large jitter in the timing of frames
 * from the AB to the ehci-hcd. This is because we cannot predict
 * in which USB micro-frame our data passes. Each micro-frame is
 * A 125 usec.
 */
#define	SYNC_ADJ_QUICK	1000
#define	SYNC_ADJ_SLOW	10000

#ifdef	ZAPTEL_SYNC_TICK
static unsigned int		zaptel_tick_count = 0;
#endif

/*------------------------- SYNC Handling --------------------------*/

static void send_drift(xbus_t *xbus, int drift);

static void xpp_ticker_init(struct xpp_ticker *ticker)
{
	memset(ticker, 0, sizeof(*ticker));
	do_gettimeofday(&ticker->last_sample.tv);
	ticker->first_sample = ticker->last_sample;
	ticker->cycle = SYNC_ADJ_QUICK;
	spin_lock_init(&ticker->lock);
}

static int xpp_ticker_step(struct xpp_ticker *ticker, const struct timeval *t)
{
	unsigned long	flags;
	long		usec;
	bool		cycled = 0;

	spin_lock_irqsave(&ticker->lock, flags);
	ticker->last_sample.tv = *t;
	if((ticker->count % ticker->cycle) == ticker->cycle - 1) {	/* rate adjust */
		usec = (long)usec_diff(
				&ticker->last_sample.tv,
				&ticker->first_sample.tv);
		ticker->first_sample = ticker->last_sample;
		ticker->tick_period = usec / ticker->cycle;
		cycled = 1;
	}
	ticker->count++;
	spin_unlock_irqrestore(&ticker->lock, flags);
	return cycled;
}

static inline void driftinfo_recalc(struct xpp_drift *driftinfo)
{
	driftinfo->delta_max = INT_MIN;
	driftinfo->delta_min = INT_MAX;
}

/*
 * No locking. It is called only from:
 *   - update_sync_master() in a globall spinlock protected code.
 *   - initalization.
 */
static inline void xbus_drift_clear(xbus_t *xbus)
{
	struct xpp_drift	*driftinfo = &xbus->drift;

	driftinfo_recalc(driftinfo);
	driftinfo->calc_drift = 0;
	xbus->ticker.cycle = SYNC_ADJ_QUICK;
}

void xpp_drift_init(xbus_t *xbus)
{
	memset(&xbus->drift, 0, sizeof(xbus->drift));
	spin_lock_init(&xbus->drift.lock);
	xpp_ticker_init(&xbus->ticker);
	xbus->drift.wanted_offset = 500;
	xbus_drift_clear(xbus);
}

#ifdef	SAMPLE_TICKS
static void sample_tick(xbus_t *xbus, int sample)
{
	if(!xbus->sample_running)
		return;
	if(xbus->sample_pos < SAMPLE_SIZE)
		xbus->sample_ticks[xbus->sample_pos++] = sample;
	else {
		xbus->sample_running = 0;
		xbus->sample_pos = 0;
	}
}
#else
#define	sample_tick(x,y)
#endif

static void xpp_drift_step(xbus_t *xbus, const struct timeval *tv)
{
	struct xpp_drift	*driftinfo = &xbus->drift;
	struct xpp_ticker	*ticker = &xbus->ticker;
	unsigned long		flags;
	bool			cycled;

	spin_lock_irqsave(&driftinfo->lock, flags);
	cycled = xpp_ticker_step(&xbus->ticker, tv);
	if(ref_ticker && syncer && xbus->sync_mode == SYNC_MODE_PLL) {
		int	new_delta_tick = ticker->count - ref_ticker->count;
		int	lost_ticks = new_delta_tick - driftinfo->delta_tick;

		driftinfo->delta_tick = new_delta_tick;
		if(lost_ticks) {
			driftinfo->lost_ticks++;
			driftinfo->lost_tick_count += abs(lost_ticks);
			XBUS_DBG(SYNC, xbus, "Lost %d tick%s\n",
				lost_ticks,
				(abs(lost_ticks) > 1) ? "s": "");
			ticker->cycle = SYNC_ADJ_QUICK;
			if(abs(lost_ticks) > 100)
				ticker->count = ref_ticker->count;
		} else {
			long	usec_delta;
			bool	nofix = 0;

			usec_delta = (long)usec_diff(
					&ticker->last_sample.tv,
					&ref_ticker->last_sample.tv);
			usec_delta -= driftinfo->wanted_offset;
			sample_tick(xbus, usec_delta);
			if(abs(usec_delta) > 300) {
				/*
				 * We are close to the edge, send a brutal
				 * fix, and skip calculation until next time.
				 */
				if(usec_delta > 0 && xbus->sync_adjustment > -SYNC_ADJ_MAX) {
					XBUS_DBG(SYNC, xbus, "Pullback usec_delta=%ld\n", usec_delta);
					send_drift(xbus, -SYNC_ADJ_MAX);	/* emergency push */
				}
				if(usec_delta < 0 && xbus->sync_adjustment < SYNC_ADJ_MAX) {
					XBUS_DBG(SYNC, xbus, "Pushback usec_delta=%ld\n", usec_delta);
					send_drift(xbus, SYNC_ADJ_MAX);		/* emergency push */
				}
				ticker->cycle = SYNC_ADJ_QUICK;
				nofix = 1;
			} else {
				/* good data, use it */
				if(usec_delta > driftinfo->delta_max)
					driftinfo->delta_max = usec_delta;
				if(usec_delta < driftinfo->delta_min)
					driftinfo->delta_min = usec_delta;
			}
			if(!nofix && cycled) {
				int	offset = 0;

				driftinfo->median = (driftinfo->delta_max + driftinfo->delta_min) / 2;
				driftinfo->jitter = driftinfo->delta_max - driftinfo->delta_min;
				if(abs(driftinfo->median) >= 150) {	/* more than 1 usb uframe */
					int	factor = abs(driftinfo->median) / 125;

					factor = 1 + (factor * 8000) / ticker->cycle;
					if(driftinfo->median > 0)
						offset = driftinfo->calc_drift - factor;
					else
						offset = driftinfo->calc_drift + factor;
					/* for large median, push some more */
					if(abs(driftinfo->median) >= 300) {	/* more than 2 usb uframes */
						ticker->cycle = SYNC_ADJ_QUICK;
						XBUS_NOTICE(xbus,
								"Back to quick: median=%d\n",
								driftinfo->median);
					}
				} else {
					ticker->cycle += 500;
					if(ticker->cycle >= SYNC_ADJ_SLOW)
						ticker->cycle = SYNC_ADJ_SLOW;
				}
				driftinfo->calc_drift = offset;
				XBUS_DBG(SYNC, xbus,
						"ADJ: min=%d max=%d jitter=%d median=%d offset=%d\n",
						driftinfo->delta_min,
						driftinfo->delta_max,
						driftinfo->jitter,
						driftinfo->median,
						offset);
				if(offset < -SYNC_ADJ_MAX)
					offset = -SYNC_ADJ_MAX;
				if(offset > SYNC_ADJ_MAX)
					offset = SYNC_ADJ_MAX;
				xbus->sync_adjustment_offset = offset;
				if(xbus != syncer && xbus->sync_adjustment != offset)
					send_drift(xbus, offset);
				driftinfo_recalc(driftinfo);
			}
		}
	}
	spin_unlock_irqrestore(&driftinfo->lock, flags);
}

const char *sync_mode_name(enum sync_mode mode)
{
	static const char	*sync_mode_names[] = {
		[SYNC_MODE_AB]		= "AB",
		[SYNC_MODE_NONE]	= "NONE",
		[SYNC_MODE_PLL]		= "PLL",
		[SYNC_MODE_QUERY]	= "QUERY",
	};
	if(mode >= ARRAY_SIZE(sync_mode_names))
		return NULL;
	return sync_mode_names[mode];
}

static void xpp_set_syncer(xbus_t *xbus, bool on)
{
	if(syncer != xbus && on) {
		XBUS_DBG(SYNC, xbus, "New syncer\n");
		syncer = xbus;
	} else if(syncer == xbus && !on) {
		XBUS_DBG(SYNC, xbus, "Lost syncer\n");
		syncer = NULL;
	} else
		XBUS_DBG(SYNC, xbus, "ignore %s (current syncer: %s)\n",
			(on)?"ON":"OFF",
			(syncer) ? syncer->busname : "NO-SYNC");
}

static void xbus_command_timer(unsigned long param)
{
	xbus_t		*xbus = (xbus_t *)param;
	struct timeval	now;

	BUG_ON(!xbus);
	do_gettimeofday(&now);
	xbus_command_queue_tick(xbus);
	if(!xbus->self_ticking)
		mod_timer(&xbus->command_timer, jiffies + 1);	/* Must be 1KHz rate */
}

void xbus_set_command_timer(xbus_t *xbus, bool on)
{
	XBUS_DBG(SYNC, xbus, "%s\n", (on)?"ON":"OFF");
	if(on) {
		if(!timer_pending(&xbus->command_timer)) {
			XBUS_DBG(SYNC, xbus, "add_timer\n");
			xbus->command_timer.function = xbus_command_timer;
			xbus->command_timer.data = (unsigned long)xbus;
			xbus->command_timer.expires = jiffies + 1;
			add_timer(&xbus->command_timer);
		}
	} else if(timer_pending(&xbus->command_timer)) {
		XBUS_DBG(SYNC, xbus, "del_timer\n");
		del_timer(&xbus->command_timer);
	}
}

/*
 * Called when the Astribank replies to a sync change request
 */
void got_new_syncer(xbus_t *xbus, enum sync_mode mode, int drift)
{
	unsigned long	flags;

	XBUS_DBG(SYNC, xbus, "Mode %s (%d), drift=%d (pcm_rx_counter=%d)\n",
		sync_mode_name(mode), mode, drift, atomic_read(&xbus->pcm_rx_counter));
	spin_lock_irqsave(&xbus->lock, flags);
	xbus->sync_adjustment = (signed char)drift;
	if(xbus->sync_mode == mode) {
		XBUS_DBG(SYNC, xbus, "Already in mode '%s'. Ignored\n", sync_mode_name(mode));
		goto out;
	}
	switch(mode) {
	case SYNC_MODE_AB:
		xbus->sync_mode = mode;
		xbus_set_command_timer(xbus, 0);
		xbus->self_ticking = 1;
		xpp_set_syncer(xbus, 1);
		global_ticker = xbus;
		break;
	case SYNC_MODE_PLL:
		xbus->sync_mode = mode;
		xbus_set_command_timer(xbus, 0);
		xbus->self_ticking = 1;
		xpp_set_syncer(xbus, 0);
		global_ticker = xbus;
		break;
	case SYNC_MODE_NONE:		/* lost sync source */
		xbus->sync_mode = mode;
		xbus_set_command_timer(xbus, 1);
		xbus->self_ticking = 0;
		xpp_set_syncer(xbus, 0);
		break;
	case SYNC_MODE_QUERY:		/* ignore           */
		break;
	default:
		XBUS_ERR(xbus, "%s: unknown mode=0x%X\n", __FUNCTION__, mode);
	}
out:
	spin_unlock_irqrestore(&xbus->lock, flags);
}

void xbus_request_sync(xbus_t *xbus, enum sync_mode mode)
{
	BUG_ON(!xbus);
	XBUS_DBG(SYNC, xbus, "sent request (mode=%d)\n", mode);
	CALL_PROTO(GLOBAL, SYNC_SOURCE, xbus, NULL, mode, 0);
}

static void reset_sync_counters(void)
{
	int	i;

	//DBG(SYNC, "%d\n", atomic_read(&xpp_tick_counter));
	for(i = 0; i < MAX_BUSES; i++) {
		xbus_t	*xbus = get_xbus(i);

		if(!xbus)
			continue;
		/*
		 * Don't send to non self_ticking Astribanks:
		 *  - Maybe they didn't finish initialization
		 *  - Or maybe they didn't answer us in the first place
		      (e.g: wrong firmware version, etc).
		 */
		if (TRANSPORT_RUNNING(xbus) && xbus->self_ticking) {
			if(XBUS_GET(xbus)) {
				/* Reset sync LEDs once in a while */
				CALL_PROTO(GLOBAL, RESET_SYNC_COUNTERS, xbus, NULL);
				XBUS_PUT(xbus);
			} else {
				static int	rate_limit;

				if((rate_limit++ % 1003) == 0)
					XBUS_DBG(GENERAL, xbus,
						"Dropped packet. Is shutting down. (%d)\n", rate_limit);
			}
		}
		put_xbus(xbus);
	}
}

static void send_drift(xbus_t *xbus, int drift)
{
	struct timeval          now;
	const char              *msg;

	BUG_ON(abs(drift) > SYNC_ADJ_MAX);
	do_gettimeofday(&now);
	if(drift > xbus->sync_adjustment)
		msg = "up";
	else
		msg = "down";
	XBUS_DBG(SYNC, xbus, "%sDRIFT adjust %s (%d) (last update %ld seconds ago)\n",
		(disable_pll_sync) ? "Fake " : "",
		msg, drift, now.tv_sec - xbus->pll_updated_at);
	if(!disable_pll_sync)
		CALL_PROTO(GLOBAL, SYNC_SOURCE, xbus, NULL, SYNC_MODE_PLL, drift);
	xbus->pll_updated_at = now.tv_sec;
}

static void global_tick(void)
{
	struct timeval	now;

	do_gettimeofday(&now);
	atomic_inc(&xpp_tick_counter);
	if((atomic_read(&xpp_tick_counter) % BIG_TICK_INTERVAL) == 0)
		reset_sync_counters();
	xpp_ticker_step(&global_ticks_series, &now);
}

#ifdef	ZAPTEL_SYNC_TICK
int zaptel_sync_tick(struct zt_span *span, int is_master)
{
	xpd_t		*xpd = span->pvt;
	static int	redundant_ticks;	/* for extra spans */
	struct timeval	now;

	if(!force_zaptel_sync)
		goto noop;
	do_gettimeofday(&now);
	BUG_ON(!xpd);
	/*
	 * Detect if any of our spans is zaptel sync master
	 */
	if(is_master) {
		static int	rate_limit;

		if(xpd->xbus != syncer && ((rate_limit % 1003) == 0)) {
			XPD_ERR(xpd,
				"Zaptel master, but syncer=%s\n",
				xpd->xbus->busname);
		}
		if((rate_limit % 5003) == 0)
			XPD_NOTICE(xpd, "Zaptel master: ignore ZAPTEL sync\n");
		rate_limit++;
		goto noop;
	}
	/* Now we know for sure someone else is zaptel sync master */
	if(syncer) {
		static int	rate_limit;

		if((rate_limit++ % 5003) == 0)
			XBUS_DBG(SYNC, syncer,
				"Already a syncer, ignore ZAPTEL sync\n");
		goto noop;
	}
	/* ignore duplicate calls from all our registered spans */
	if((redundant_ticks++ % total_registered_spans()) != 0) {
#if 0
		static int	rate_limit;

		if((rate_limit++ % 1003) < 16)
			XPD_NOTICE(xpd, "boop (%d)\n", zaptel_tick_count);
#endif
		goto noop;
	}
	xpp_ticker_step(&zaptel_ticker, &now);
	zaptel_tick_count++;
	//flip_parport_bit(1);
	return 0;
noop:
	return 0;	/* No auto sync from zaptel */
}
#endif

/*
 * called from elect_syncer()
 * if new_syncer is NULL, than we move all to SYNC_MODE_PLL
 * for ZAPTEL sync.
 */
static void update_sync_master(xbus_t *new_syncer)
{
	const char	*msg = (force_zaptel_sync) ? "ZAPTEL" : "NO-SYNC";
	int		i;
	unsigned long	flags;

	DBG(SYNC, "%s => %s\n",
		(syncer) ? syncer->busname : msg,
		(new_syncer) ? new_syncer->busname : msg);
	/*
	 * This global locking protects:
	 *   - The ref_ticker so it won't be used while we change it.
	 *   - The xbus_drift_clear() from corrupting driftinfo data.
	 */
	spin_lock_irqsave(&ref_ticker_lock, flags);
	if(syncer)
		xbus_drift_clear(syncer);	/* Clean old data */
	if(new_syncer) {
		XBUS_DBG(SYNC, new_syncer, "pcm_rx_counter=%d\n",
			atomic_read(&new_syncer->pcm_rx_counter));
		force_zaptel_sync = 0;
		ref_ticker = &new_syncer->ticker;
		xbus_drift_clear(new_syncer);	/* Clean new data */
		xbus_request_sync(new_syncer, SYNC_MODE_AB);
	} else if(force_zaptel_sync) {
		ref_ticker = &zaptel_ticker;
	} else {
		ref_ticker = NULL;
	}
	spin_unlock_irqrestore(&ref_ticker_lock, flags);
	DBG(SYNC, "stop unwanted syncers\n");
	/* Shut all down except the wanted sync master */
	for(i = 0; i < MAX_BUSES; i++) {
		xbus_t	*xbus = get_xbus(i);
		if(!xbus)
			continue;
		if(TRANSPORT_RUNNING(xbus) && xbus != new_syncer) {
			if(xbus->self_ticking)
				xbus_request_sync(xbus, SYNC_MODE_PLL);
			else
				XBUS_DBG(SYNC, xbus, "Not self_ticking yet. Ignore\n");
		}
		put_xbus(xbus);
	}
}

void elect_syncer(const char *msg)
{
	int	i;
	int	j;
	uint	timing_priority = 0;
	xpd_t	*best_xpd = NULL;
	xbus_t	*the_xbus = NULL;

	for(i = 0; i < MAX_BUSES; i++) {
		xbus_t	*xbus = get_xbus(i);
		if(!xbus)
			continue;
		if(!the_xbus)
			the_xbus = xbus;
		if (TRANSPORT_RUNNING(xbus)) {
			for(j = 0; j < MAX_XPDS; j++) {
				xpd_t	*xpd = xpd_of(xbus, j);

				if(!xpd || !xpd->card_present)
					continue;
				if(xpd->timing_priority > timing_priority) {
					timing_priority = xpd->timing_priority;
					best_xpd = xpd;
				}
			}
		}
		put_xbus(xbus);
	}
	if(best_xpd) {
		the_xbus = best_xpd->xbus;
		XPD_DBG(SYNC, best_xpd, "%s: elected with priority %d\n", msg, timing_priority);
	} else if(the_xbus) {
		XBUS_DBG(SYNC, the_xbus, "%s: elected\n", msg);
	} else
		DBG(SYNC, "%s: No more syncers\n", msg);
	if(the_xbus != syncer)
		update_sync_master(the_xbus);
}

/*
 * This function is used by FXS/FXO. The pcm_mask argument signifies
 * channels which should be *added* to the automatic calculation.
 * Normally, this argument is 0.
 *
 * The caller should spinlock the XPD before calling it.
 */
void __pcm_recompute(xpd_t *xpd, xpp_line_t pcm_mask)
{
	int		i;
	int		line_count = 0;

	XPD_DBG(SIGNAL, xpd, "pcm_mask=0x%X\n", pcm_mask);
	/* Add/remove all the trivial cases */
	pcm_mask |= xpd->offhook;
	pcm_mask |= xpd->cid_on;
	pcm_mask &= ~xpd->digital_signalling;	/* No PCM in D-Channels */
	pcm_mask &= ~xpd->digital_inputs;
	pcm_mask &= ~xpd->digital_outputs;
	for_each_line(xpd, i)
		if(IS_SET(pcm_mask, i))
			line_count++;
	/*
	 * FIXME: Workaround a bug in sync code of the Astribank.
	 *        Send dummy PCM for sync.
	 */
	if(xpd->addr.unit == 0 && pcm_mask == 0) {
		pcm_mask = BIT(0);
		line_count = 1;
	}
	xpd->pcm_len = (line_count)
		? RPACKET_HEADERSIZE + sizeof(xpp_line_t) + line_count * ZT_CHUNKSIZE
		: 0L;
	xpd->wanted_pcm_mask = pcm_mask;
}

/*
 * A spinlocked version of __pcm_recompute()
 */
void pcm_recompute(xpd_t *xpd, xpp_line_t pcm_mask)
{
	unsigned long	flags;

	spin_lock_irqsave(&xpd->lock, flags);
	__pcm_recompute(xpd, pcm_mask);
	spin_unlock_irqrestore(&xpd->lock, flags);
}

void fill_beep(u_char *buf, int num, int duration)
{
	bool	alternate = (duration) ? (jiffies/(duration*1000)) & 0x1 : 0;
	int	which;
	u_char	*snd;

	/*
	 * debug tones
	 */
	static u_char beep[] = {
		0x7F, 0xBE, 0xD8, 0xBE, 0x80, 0x41, 0x24, 0x41,	/* Dima */
		0x67, 0x90, 0x89, 0x90, 0xFF, 0x10, 0x09, 0x10,	/* Izzy */
	};
	static u_char beep_alt[] = {
		0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,	/* silence */
	};
	if(alternate) {
		which = num % ARRAY_SIZE(beep_alt);
		snd = &beep_alt[which];
	} else {
		which = num % ARRAY_SIZE(beep);
		snd = &beep[which];
	}
	memcpy(buf, snd, ZT_CHUNKSIZE);
}

#ifdef	XPP_EC_CHUNK
/*
 * Taken from zaptel.c
 */
static inline void xpp_ec_chunk(struct zt_chan *chan, unsigned char *rxchunk, const unsigned char *txchunk)
{
	int16_t		rxlin;
	int		x;
	unsigned long	flags;

	/* Perform echo cancellation on a chunk if necessary */
	if (!chan->ec)
		return;
	spin_lock_irqsave(&chan->lock, flags);
	for (x=0;x<ZT_CHUNKSIZE;x++) {
		rxlin = ZT_XLAW(rxchunk[x], chan);
		rxlin = xpp_echo_can_update(chan->ec, ZT_XLAW(txchunk[x], chan), rxlin);
		rxchunk[x] = ZT_LIN2X((int)rxlin, chan);
	}
	spin_unlock_irqrestore(&chan->lock, flags);
}
#endif

static void do_ec(xpd_t *xpd)
{
#ifdef WITH_ECHO_SUPPRESSION
	struct zt_chan	*chans = xpd->span.chans;
	int		i;

	/* FIXME: need to Echo cancel double buffered data */
	for (i = 0;i < xpd->span.channels; i++) {
		if(unlikely(IS_SET(xpd->digital_signalling, i)))	/* Don't echo cancel BRI D-chans */
			continue;
		if(!IS_SET(xpd->wanted_pcm_mask, i))			/* No ec for unwanted PCM */
			continue;
#ifdef XPP_EC_CHUNK 
		/* even if defined, parameterr xpp_ec can override at run-time */
		if (xpp_ec)
			xpp_ec_chunk(&chans[i], chans[i].readchunk, xpd->ec_chunk2[i]);
		else
#endif
			zt_ec_chunk(&chans[i], chans[i].readchunk, xpd->ec_chunk2[i]);
		memcpy(xpd->ec_chunk2[i], xpd->ec_chunk1[i], ZT_CHUNKSIZE);
		memcpy(xpd->ec_chunk1[i], chans[i].writechunk, ZT_CHUNKSIZE);
	}
#endif
}

#if 0
/* Okay, now we get to the signalling.  You have several options: */

/* Option 1: If you're a T1 like interface, you can just provide a
   rbsbits function and we'll assert robbed bits for you.  Be sure to 
   set the ZT_FLAG_RBS in this case.  */

/* Opt: If the span uses A/B bits, set them here */
int (*rbsbits)(struct zt_chan *chan, int bits);

/* Option 2: If you don't know about sig bits, but do have their
   equivalents (i.e. you can disconnect battery, detect off hook,
   generate ring, etc directly) then you can just specify a
   sethook function, and we'll call you with appropriate hook states
   to set.  Still set the ZT_FLAG_RBS in this case as well */
int (*hooksig)(struct zt_chan *chan, zt_txsig_t hookstate);

/* Option 3: If you can't use sig bits, you can write a function
   which handles the individual hook states  */
int (*sethook)(struct zt_chan *chan, int hookstate);
#endif

int xpp_echocan(struct zt_chan *chan, int len)
{
#ifdef	XPP_EC_CHUNK
	if(len == 0) {	/* shut down */
		/* zaptel calls this also during channel initialization */
		if(chan->ec) {
			xpp_echo_can_free(chan->ec);
		}
		return 0;
	}
	if(chan->ec) {
		ERR("%s: Trying to override an existing EC (%p)\n", __FUNCTION__, chan->ec);
		return -EINVAL;
	}
	chan->ec = xpp_echo_can_create(len, 0);
	if(!chan->ec) {
		ERR("%s: Failed creating xpp EC (len=%d)\n", __FUNCTION__, len);
		return -EINVAL;
	}
#endif
	return 0;
}

static bool pcm_valid(xpd_t *xpd, xpacket_t *pack)
{
	xpp_line_t	lines = RPACKET_FIELD(pack, GLOBAL, PCM_READ, lines);
	int		i;
	int		count = 0;
	uint16_t	good_len;

	BUG_ON(!pack);
	BUG_ON(XPACKET_OP(pack) != XPROTO_NAME(GLOBAL, PCM_READ));
	/*
	 * Don't use for_each_line(xpd, i) here because for BRI it will
	 * ignore the channels of the other xpd's in the same unit.
	 */
	for (i = 0; i < CHANNELS_PERXPD; i++)
		if(IS_SET(lines, i))
			count++;
	/* FRAMES: include opcode in calculation */
	good_len = RPACKET_HEADERSIZE + sizeof(xpp_line_t) + count * 8;
	if(XPACKET_LEN(pack) != good_len) {
		static int rate_limit = 0;

		XPD_COUNTER(xpd, RECV_ERRORS)++;
		if((rate_limit++ % 1000) <= 10) {
			XPD_ERR(xpd, "BAD PCM REPLY: packet_len=%d (should be %d), count=%d\n",
					XPACKET_LEN(pack), good_len, count);
			dump_packet("BAD PCM REPLY", pack, 1);
		}
		return 0;
	}
	return 1;
}



static inline void pcm_frame_out(xbus_t *xbus, xframe_t *xframe)
{
	unsigned long	flags;
	struct timeval	now;
	unsigned long	usec;

	spin_lock_irqsave(&xbus->lock, flags);
	do_gettimeofday(&now);
	if(unlikely(disable_pcm || !TRANSPORT_RUNNING(xbus)))
		goto dropit;
	if(XPACKET_ADDR_SYNC((xpacket_t *)xframe->packets)) {
		usec = usec_diff(&now, &xbus->last_tx_sync);
		xbus->last_tx_sync = now;
		/* ignore startup statistics */
		if(likely(atomic_read(&xbus->pcm_rx_counter) > BIG_TICK_INTERVAL)) {
			if(abs(usec - 1000) > TICK_TOLERANCE) {
				static int	rate_limit;

				if((rate_limit++ % 5003) == 0)
					XBUS_DBG(SYNC, xbus, "Bad PCM TX timing(%d): usec=%ld.\n",
							rate_limit, usec);
			}
			if(usec > xbus->max_tx_sync)
				xbus->max_tx_sync = usec;
			if(usec < xbus->min_tx_sync)
				xbus->min_tx_sync = usec;
		}
	}
	spin_unlock_irqrestore(&xbus->lock, flags);
	/* OK, really send it */
	if(debug & DBG_PCM )
		dump_xframe("TX_XFRAME_PCM", xbus, xframe, debug);
	send_pcm_frame(xbus, xframe);
	XBUS_COUNTER(xbus, TX_XFRAME_PCM)++;
	return;
dropit:
	spin_unlock_irqrestore(&xbus->lock, flags);
	FREE_SEND_XFRAME(xbus, xframe);
}

/*
 * Generic implementations of card_pcmfromspan()/card_pcmtospan()
 * For FXS/FXO
 */
void generic_card_pcm_fromspan(xbus_t *xbus, xpd_t *xpd, xpp_line_t lines, xpacket_t *pack)
{
	byte		*pcm;
	struct zt_chan	*chans;
	unsigned long	flags;
	int		i;

	BUG_ON(!xbus);
	BUG_ON(!xpd);
	BUG_ON(!pack);
	RPACKET_FIELD(pack, GLOBAL, PCM_WRITE, lines) = lines;
	pcm = RPACKET_FIELD(pack, GLOBAL, PCM_WRITE, pcm);
	spin_lock_irqsave(&xpd->lock, flags);
	chans = xpd->span.chans;
	for (i = 0; i < xpd->channels; i++) {
		if(IS_SET(lines, i)) {
			if(SPAN_REGISTERED(xpd)) {
#ifdef	DEBUG_PCMTX
				int     channo = xpd->span.chans[i].channo;

				if(pcmtx >= 0 && pcmtx_chan == channo)
					memset((u_char *)pcm, pcmtx, ZT_CHUNKSIZE);
				else
#endif
					memcpy((u_char *)pcm, chans[i].writechunk, ZT_CHUNKSIZE);
			} else
				memset((u_char *)pcm, 0x7F, ZT_CHUNKSIZE);
			pcm += ZT_CHUNKSIZE;
		}
	}
	XPD_COUNTER(xpd, PCM_WRITE)++;
	spin_unlock_irqrestore(&xpd->lock, flags);
}

void generic_card_pcm_tospan(xbus_t *xbus, xpd_t *xpd, xpacket_t *pack)
{
	byte		*pcm;
	xpp_line_t	pcm_mask;
	xpp_line_t	pcm_mute;
	unsigned long	flags;
	int		i;

	pcm = RPACKET_FIELD(pack, GLOBAL, PCM_READ, pcm);
	pcm_mask = RPACKET_FIELD(pack, GLOBAL, PCM_READ, lines);
	spin_lock_irqsave(&xpd->lock, flags);
	/*
	 * Calculate the channels we want to mute
	 */
	pcm_mute = ~xpd->wanted_pcm_mask;
	pcm_mute |= xpd->mute_dtmf | xpd->silence_pcm;
	if(!SPAN_REGISTERED(xpd))
		goto out;
	for (i = 0; i < xpd->channels; i++) {
		volatile u_char	*r = xpd->span.chans[i].readchunk;
		bool		got_data = IS_SET(pcm_mask, i);

		if(got_data && !IS_SET(pcm_mute, i)) {
			/* We have and want real data */
			// memset((u_char *)r, 0x5A, ZT_CHUNKSIZE);	// DEBUG
			memcpy((u_char *)r, pcm, ZT_CHUNKSIZE);
		} else if(IS_SET(xpd->wanted_pcm_mask | xpd->silence_pcm, i)) {
			/* Inject SILENCE */
			memset((u_char *)r, 0x7F, ZT_CHUNKSIZE);
			if(IS_SET(xpd->silence_pcm, i)) {
				/*
				 * This will clear the EC buffers until next tick
				 * So we don't have noise residues from the past.
				 */
				memset(xpd->ec_chunk2[i], 0x7F, ZT_CHUNKSIZE);
				memset(xpd->ec_chunk1[i], 0x7F, ZT_CHUNKSIZE);
			}
		}
		if(got_data)
			pcm += ZT_CHUNKSIZE;
	}
out:
	XPD_COUNTER(xpd, PCM_READ)++;
	spin_unlock_irqrestore(&xpd->lock, flags);
}

static int copy_pcm_tospan(xbus_t *xbus, xframe_t *xframe)
{
	byte		*xframe_end;
	xpacket_t	*pack;
	byte		*p;
	int		ret = -EPROTO;	/* Assume error */

	if(debug & DBG_PCM)
		dump_xframe("RX_XFRAME_PCM", xbus, xframe, debug);
	/* handle content */

	p = xframe->packets;
	xframe_end = p + XFRAME_LEN(xframe);
	do {
		int		len;
		xpd_t		*xpd;

		pack = (xpacket_t *)p;
		len = XPACKET_LEN(pack);
		/* Sanity checks */
		if(unlikely(XPACKET_OP(pack) != XPROTO_NAME(GLOBAL,PCM_READ))) {
			static int	rate_limit;

			if((rate_limit++ % 1003) == 0) {
				XBUS_NOTICE(xbus,
					"%s: Non-PCM packet within a PCM xframe. (%d)\n",
					__FUNCTION__, rate_limit);
				dump_xframe("In PCM xframe", xbus, xframe, debug);
			}
			goto out;
		}
		p += len;
		if(p > xframe_end || len < RPACKET_HEADERSIZE) {
			static int	rate_limit;

			if((rate_limit++ % 1003) == 0) {
				XBUS_NOTICE(xbus,
					"%s: Invalid packet length %d. (%d)\n",
					__FUNCTION__, len, rate_limit);
				dump_xframe("BAD LENGTH", xbus, xframe, debug);
			}
			goto out;
		}
		xpd = xpd_byaddr(xbus, XPACKET_ADDR_UNIT(pack), XPACKET_ADDR_SUBUNIT(pack));
		if(unlikely(!xpd)) {
			static int	rate_limit;

			if((rate_limit++ % 1003) == 0) {
				notify_bad_xpd(__FUNCTION__, xbus, XPACKET_ADDR(pack), "RECEIVE PCM");
				dump_xframe("Unknown XPD addr", xbus, xframe, debug);
			}
			goto out;
		}
		if(!pcm_valid(xpd, pack))
			goto out;
		if(SPAN_REGISTERED(xpd)) {
			XBUS_COUNTER(xbus, RX_PACK_PCM)++;
			CALL_XMETHOD(card_pcm_tospan, xbus, xpd, pack);
		}
	} while(p < xframe_end);
	ret = 0;	/* all good */
	XBUS_COUNTER(xbus, RX_XFRAME_PCM)++;
out:
	FREE_RECV_XFRAME(xbus, xframe);
	return ret;
}

static void xbus_tick(xbus_t *xbus)
{
	int		i;
	xpd_t		*xpd;
	xframe_t	*xframe = NULL;
	xpacket_t	*pack = NULL;
	size_t		pcm_len;
	bool		sent_sync_bit = 0;

	/*
	 * Update zaptel
	 */
	for(i = 0; i < MAX_XPDS; i++) {
		xpd = xpd_of(xbus, i);
		if(xpd && SPAN_REGISTERED(xpd)) {
#ifdef	OPTIMIZE_CHANMUTE
			int		j;
			xpp_line_t	xmit_mask = xpd->wanted_pcm_mask;
			
			xmit_mask |= xpd->silence_pcm;
			xmit_mask |= xpd->digital_signalling;
			for_each_line(xpd, j) {
				xpd->chans[j].chanmute = (optimize_chanmute)
					? !IS_SET(xmit_mask, j)
					: 0;
			}
#endif
			/*
			 * calls to zt_transmit should be out of spinlocks, as it may call back
			 * our hook setting methods.
			 */
			zt_transmit(&xpd->span);
		}
	}
	/*
	 * Fill xframes
	 */
	for(i = 0; i < MAX_XPDS; i++) {
		if((xpd = xpd_of(xbus, i)) == NULL)
			continue;
		pcm_len = xpd->pcm_len;
		if(SPAN_REGISTERED(xpd)) {
			if(pcm_len && xpd->card_present) {
				do {
					// pack = NULL;		/* FORCE single packet frames */
					if(xframe && !pack) {	/* FULL frame */
						pcm_frame_out(xbus, xframe);
						xframe = NULL;
						XBUS_COUNTER(xbus, TX_PCM_FRAG)++;
					}
					if(!xframe) {		/* Alloc frame */
						xframe = ALLOC_SEND_XFRAME(xbus);
						if (!xframe) {
							static int rate_limit;

							if((rate_limit++ % 3001) == 0)
								XBUS_ERR(xbus,
									"%s: failed to allocate new xframe\n",
									__FUNCTION__);
							return;
						}
					}
					pack = xframe_next_packet(xframe, pcm_len);
				} while(!pack);
				XPACKET_INIT(pack, GLOBAL, PCM_WRITE, xpd->xbus_idx, 1, 0);
				XPACKET_LEN(pack) = pcm_len;
				if(!sent_sync_bit) {
					XPACKET_ADDR_SYNC(pack) = 1;
					sent_sync_bit = 1;
				}
				CALL_XMETHOD(card_pcm_fromspan, xbus, xpd, xpd->wanted_pcm_mask, pack);
				XBUS_COUNTER(xbus, TX_PACK_PCM)++;
			}
		}
	}
	if(xframe)	/* clean any leftovers */
		pcm_frame_out(xbus, xframe);
	/*
	 * Receive PCM
	 */
	while((xframe = xframe_dequeue(&xbus->pcm_tospan)) != NULL) {
		copy_pcm_tospan(xbus, xframe);
		if(XPACKET_ADDR_SYNC((xpacket_t *)xframe->packets)) {
			struct timeval	now;
			unsigned long	usec;

			do_gettimeofday(&now);
			usec = usec_diff(&now, &xbus->last_rx_sync);
			xbus->last_rx_sync = now;
			/* ignore startup statistics */
			if(likely(atomic_read(&xbus->pcm_rx_counter) > BIG_TICK_INTERVAL)) {
				if(abs(usec - 1000) > TICK_TOLERANCE) {
					static int	rate_limit;

					if((rate_limit++ % 5003) == 0)
						XBUS_DBG(SYNC, xbus, "Bad PCM RX timing(%d): usec=%ld.\n",
								rate_limit, usec);
				}
				if(usec > xbus->max_rx_sync)
					xbus->max_rx_sync = usec;
				if(usec < xbus->min_rx_sync)
					xbus->min_rx_sync = usec;
			}
		}
	}
	for(i = 0; i < MAX_XPDS; i++) {
		xpd = xpd_of(xbus, i);
		if(!xpd || !xpd->card_present)
			continue;
		if(SPAN_REGISTERED(xpd)) {
			do_ec(xpd);
			zt_receive(&xpd->span);
		}
		xpd->silence_pcm = 0;	/* silence was injected */
		xpd->timer_count = xbus->global_counter;
		/*
		 * Must be called *after* tx/rx so
		 * D-Chan counters may be cleared
		 */
		CALL_XMETHOD(card_tick, xbus, xpd);
	}
}

static void do_tick(xbus_t *xbus, const struct timeval *tv_received)
{
	int		counter = atomic_read(&xpp_tick_counter);
	unsigned long	flags;

	xbus_command_queue_tick(xbus);
	if(global_ticker == xbus)
		global_tick();	/* called from here or zaptel_sync_tick() */
	spin_lock_irqsave(&ref_ticker_lock, flags);
	xpp_drift_step(xbus, tv_received);
	spin_unlock_irqrestore(&ref_ticker_lock, flags);
	if(likely(xbus->self_ticking))
		xbus_tick(xbus);
	xbus->global_counter = counter;
}

void xframe_receive_pcm(xbus_t *xbus, xframe_t *xframe)
{
	if(!xframe_enqueue(&xbus->pcm_tospan, xframe)) {
		static int	rate_limit;

		if((rate_limit++ % 1003) == 0)
			XBUS_DBG(SYNC, xbus,
					"Failed to enqueue received pcm frame. (%d)\n",
					rate_limit);
		FREE_RECV_XFRAME(xbus, xframe);
	}
	/*
	 * The sync_master bit is marked at the first packet
	 * of the frame, regardless of the XPD that is sync master.
	 * FIXME: what about PRI split?
	 */
	if(XPACKET_ADDR_SYNC((xpacket_t *)xframe->packets)) {
		do_tick(xbus, &xframe->tv_received);
		atomic_inc(&xbus->pcm_rx_counter);
	} else
		xbus->xbus_frag_count++;
}

#ifdef CONFIG_PROC_FS
static int proc_sync_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int		len = 0;
	struct timeval	now;
	unsigned int	counter = atomic_read(&xpp_tick_counter);
	unsigned long	usec;

	do_gettimeofday(&now);
	len += sprintf(page + len, "# To modify sync source write into this file:\n");
	len += sprintf(page + len, "#     ZAPTEL      - Another zaptel device provide sync\n");
	len += sprintf(page + len, "#     SYNC=nn     - XBUS-nn provide sync\n");
	len += sprintf(page + len, "#     QUERY=nn    - Query XBUS-nn for sync information (DEBUG)\n");
	if(!syncer) {
		if(force_zaptel_sync)
			len += sprintf(page + len, "ZAPTEL\n");
		else
			len += sprintf(page + len, "NO-SYNC\n");
	} else
		len += sprintf(page + len, "SYNC=%02d\n", syncer->num);
#ifdef	ZAPTEL_SYNC_TICK
	if(force_zaptel_sync) {
		len += sprintf(page + len,
			"Zaptel Reference Sync (%d registered spans):\n",
			total_registered_spans());
		len += sprintf(page + len, "\tzaptel_tick: #%d\n", zaptel_tick_count);
		len += sprintf(page + len, "\ttick - zaptel_tick = %d\n",
				counter - zaptel_tick_count);
	} else {
		len += sprintf(page + len,
				"Zaptel Reference Sync Not activated\n");
	}
#endif
	usec = usec_diff(&now, &global_ticks_series.last_sample.tv);
	len += sprintf(page + len, "\ntick: #%d\n", counter);
	len += sprintf(page + len,
		"tick duration: %d usec (measured %ld.%ld msec ago)\n",
		global_ticks_series.tick_period,
		usec / 1000, usec % 1000);
	if (len <= off+count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

static int proc_sync_write(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
	char		buf[MAX_PROC_WRITE];
	int		xbus_num;
	int		xpd_num;
	xbus_t		*xbus;
	xpd_t		*xpd;

	// DBG(SYNC, "%s: count=%ld\n", __FUNCTION__, count);
	if(count >= MAX_PROC_WRITE)
		return -EINVAL;
	if(copy_from_user(buf, buffer, count))
		return -EFAULT;
	buf[count] = '\0';
	if(strncmp("ZAPTEL", buf, 6) == 0) {
		DBG(SYNC, "ZAPTEL\n");
		force_zaptel_sync=1;
		update_sync_master(NULL);
	} else if(sscanf(buf, "SYNC=%d", &xbus_num) == 1) {
		DBG(SYNC, "SYNC=%d\n", xbus_num);
		if((xbus = get_xbus(xbus_num)) == NULL) {
			ERR("No bus %d exists\n", xbus_num);
			return -ENXIO;
		}
		update_sync_master(xbus);
		put_xbus(xbus);
	} else if(sscanf(buf, "QUERY=%d", &xbus_num) == 1) {
		DBG(SYNC, "QUERY=%d\n", xbus_num);
		if((xbus = get_xbus(xbus_num)) == NULL) {
			ERR("No bus %d exists\n", xbus_num);
			return -ENXIO;
		}
		CALL_PROTO(GLOBAL, SYNC_SOURCE, xbus, NULL, SYNC_MODE_QUERY, 0);
		put_xbus(xbus);
	} else if(sscanf(buf, "%d %d", &xbus_num, &xpd_num) == 2) {
		NOTICE("Using deprecated syntax to update %s file\n", 
				PROC_SYNC);
		if(xpd_num != 0) {
			ERR("Currently can only set sync for XPD #0\n");
			return -EINVAL;
		}
		if((xbus = get_xbus(xbus_num)) == NULL) {
			ERR("No bus %d exists\n", xbus_num);
			return -ENXIO;
		}
		if((xpd = xpd_of(xbus, xpd_num)) == NULL) {
			XBUS_ERR(xbus, "No xpd %d exists\n", xpd_num);
			put_xbus(xbus);
			return -ENXIO;
		}
		update_sync_master(xbus);
		put_xbus(xbus);
	} else {
		ERR("%s: cannot parse '%s'\n", __FUNCTION__, buf);
		count = -EINVAL;
	}
	return count;
}

static struct proc_dir_entry	*top;

#endif

int xbus_pcm_init(struct proc_dir_entry *toplevel)
{
	int			ret = 0;
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry	*ent;
#endif

#ifdef	OPTIMIZE_CHANMUTE
	INFO("FEATURE: with CHANMUTE optimization (%sactivated)\n",
		(optimize_chanmute)?"":"de");
#endif
#ifdef	WITH_ECHO_SUPPRESSION
	INFO("FEATURE: with ECHO_SUPPRESSION\n");
#else
	INFO("FEATURE: without ECHO_SUPPRESSION\n");
#endif
	if(xpp_ec)
		INFO("FEATURE: with XPP_EC_CHUNK\n");
	else
		INFO("FEATURE: without XPP_EC_CHUNK\n");
#ifdef	ZAPTEL_SYNC_TICK
	INFO("FEATURE: with sync_tick() from ZAPTEL\n");
#else
	INFO("FEATURE: without sync_tick() from ZAPTEL\n");
#endif
	xpp_ticker_init(&global_ticks_series);
	xpp_ticker_init(&zaptel_ticker);
#ifdef CONFIG_PROC_FS
	top = toplevel;
	ent = create_proc_entry(PROC_SYNC, 0644, top);
	if(!ent) {
		ret = -EFAULT;
		goto err;
	}
	ent->read_proc = proc_sync_read;
	ent->write_proc = proc_sync_write;
	ent->data = NULL;
#endif
err:
	return ret;
}

void xbus_pcm_shutdown(void)
{
#ifdef CONFIG_PROC_FS
	DBG(GENERAL, "Removing '%s' from proc\n", PROC_SYNC);
	remove_proc_entry(PROC_SYNC, top);
#endif
}


EXPORT_SYMBOL(xbus_request_sync);
EXPORT_SYMBOL(got_new_syncer);
EXPORT_SYMBOL(elect_syncer);
EXPORT_SYMBOL(xpp_echocan);
#ifdef	ZAPTEL_SYNC_TICK
EXPORT_SYMBOL(zaptel_sync_tick);
#endif
EXPORT_SYMBOL(__pcm_recompute);
EXPORT_SYMBOL(pcm_recompute);
EXPORT_SYMBOL(generic_card_pcm_tospan);
EXPORT_SYMBOL(generic_card_pcm_fromspan);
#ifdef	DEBUG_PCMTX
EXPORT_SYMBOL(pcmtx);
EXPORT_SYMBOL(pcmtx_chan);
#endif

