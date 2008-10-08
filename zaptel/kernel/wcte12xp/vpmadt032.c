/*
 * Digium, Inc.  Wildcard TE12xP T1/E1 card Driver
 *
 * Written by Michael Spiceland <mspiceland@digium.com>
 *
 * Adapted from the wctdm24xxp and wcte11xp drivers originally
 * written by Mark Spencer <markster@digium.com>
 *            Matthew Fredrickson <creslin@digium.com>
 *            William Meadows <wmeadows@digium.com>
 *
 * Copyright (C) 2007, Digium, Inc.
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

#include <linux/delay.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
#include <linux/semaphore.h>
#else
#include <asm/semaphore.h>
#endif
#include <linux/pci.h> 
#include <linux/firmware.h> 
#include <linux/list.h> 

#include "zaptel.h"
#include "voicebus.h"

#include "wcte12xp.h"
#include "vpmadt032.h"
#include "GpakApi.h"

extern struct t1 *ifaces[WC_MAX_IFACES];

extern int vpmnlptype;
extern int vpmnlpthresh;
extern int vpmnlpmaxsupp;

#ifdef VPM_SUPPORT

inline void vpm150m_cmd_dequeue(struct t1 *wc, volatile unsigned char *writechunk, int whichframe)
{
	struct vpm150m_cmd *curcmd = NULL;
	struct vpm150m *vpm150m = wc->vpm150m;
	int x;
	unsigned char leds = ~((wc->intcount / 1000) % 8) & 0x7;

	/* Skip audio */
	writechunk += 66;

	if (test_bit(VPM150M_SPIRESET, &vpm150m->control) || test_bit(VPM150M_HPIRESET, &vpm150m->control)) {
		debug_printk(1, "HW Resetting VPMADT032 ...\n");
		for (x = 0; x < 4; x++) {
			if (!x) {
				if (test_and_clear_bit(VPM150M_SPIRESET, &vpm150m->control))
					writechunk[CMD_BYTE(x, 0, 1)] = 0x08;
				else if (test_and_clear_bit(VPM150M_HPIRESET, &vpm150m->control))
					writechunk[CMD_BYTE(x, 0, 1)] = 0x0b;
			} else
				writechunk[CMD_BYTE(x, 0, 1)] = 0x00 | leds;
			writechunk[CMD_BYTE(x, 1, 1)] = 0;
			writechunk[CMD_BYTE(x, 2, 1)] = 0x00;
		}
		return;
	}

	/* Search for something waiting to transmit */
	for (x = 0; x < VPM150M_MAX_COMMANDS; x++) {
		if ((vpm150m->cmdq[x].flags & (__VPM150M_RD | __VPM150M_WR)) && 
		   !(vpm150m->cmdq[x].flags & (__VPM150M_FIN | __VPM150M_TX))) {
		   	curcmd = &vpm150m->cmdq[x];
			curcmd->ident = wc->txident;
			curcmd->flags |= __VPM150M_TX;
			break;
		}
	}
	if (curcmd) {
#if 0
		printk("Found command txident = %d, desc = 0x%x, addr = 0x%x, data = 0x%x\n", curcmd->txident, curcmd->desc, curcmd->addr, curcmd->data);
#endif
		if (curcmd->flags & __VPM150M_RWPAGE) {
			/* Set CTRL access to page*/
			writechunk[CMD_BYTE(0, 0, 1)] = (0x8 << 4);
			writechunk[CMD_BYTE(0, 1, 1)] = 0;
			writechunk[CMD_BYTE(0, 2, 1)] = 0x20;

			/* Do a page write */
			if (curcmd->flags & __VPM150M_WR)
				writechunk[CMD_BYTE(1, 0, 1)] = ((0x8 | 0x4) << 4);
			else
				writechunk[CMD_BYTE(1, 0, 1)] = ((0x8 | 0x4 | 0x1) << 4);
			writechunk[CMD_BYTE(1, 1, 1)] = 0;
			if (curcmd->flags & __VPM150M_WR)
				writechunk[CMD_BYTE(1, 2, 1)] = curcmd->data[0] & 0xf;
			else
				writechunk[CMD_BYTE(1, 2, 1)] = 0;

			if (curcmd->flags & __VPM150M_WR) {
				/* Fill in buffer to size */
				writechunk[CMD_BYTE(2, 0, 1)] = 0;
				writechunk[CMD_BYTE(2, 1, 1)] = 0;
				writechunk[CMD_BYTE(2, 2, 1)] = 0;
			} else {
				/* Do reads twice b/c of vpmadt032 bug */
				writechunk[CMD_BYTE(2, 0, 1)] = ((0x8 | 0x4 | 0x1) << 4);
				writechunk[CMD_BYTE(2, 1, 1)] = 0;
				writechunk[CMD_BYTE(2, 2, 1)] = 0;
			}

			/* Clear XADD */
			writechunk[CMD_BYTE(3, 0, 1)] = (0x8 << 4);
			writechunk[CMD_BYTE(3, 1, 1)] = 0;
			writechunk[CMD_BYTE(3, 2, 1)] = 0;

			/* Fill in buffer to size */
			writechunk[CMD_BYTE(4, 0, 1)] = 0;
			writechunk[CMD_BYTE(4, 1, 1)] = 0;
			writechunk[CMD_BYTE(4, 2, 1)] = 0;

		} else {
			/* Set address */
			writechunk[CMD_BYTE(0, 0, 1)] = ((0x8 | 0x4) << 4);
			writechunk[CMD_BYTE(0, 1, 1)] = (curcmd->address >> 8) & 0xff;
			writechunk[CMD_BYTE(0, 2, 1)] = curcmd->address & 0xff;

			/* Send/Get our data */
			if (curcmd->flags & __VPM150M_WR) {
				if (curcmd->datalen > 1)
					writechunk[CMD_BYTE(1, 0, 1)] = ((0x8 | (0x1 << 1)) << 4);
				else
					writechunk[CMD_BYTE(1, 0, 1)] = ((0x8 | (0x3 << 1)) << 4);
			} else
				if (curcmd->datalen > 1)
					writechunk[CMD_BYTE(1, 0, 1)] = ((0x8 | (0x1 << 1) | 0x1) << 4);
				else
					writechunk[CMD_BYTE(1, 0, 1)] = ((0x8 | (0x3 << 1) | 0x1) << 4);
			writechunk[CMD_BYTE(1, 1, 1)] = (curcmd->data[0] >> 8) & 0xff;
			writechunk[CMD_BYTE(1, 2, 1)] = curcmd->data[0] & 0xff;

			if (curcmd->flags & __VPM150M_WR) {
				/* Fill in */
				writechunk[CMD_BYTE(2, 0, 1)] = 0;
				writechunk[CMD_BYTE(2, 1, 1)] = 0;
				writechunk[CMD_BYTE(2, 2, 1)] = 0;
			} else {
				/* Do this again for reads b/c of the bug in vpmadt032 */
				writechunk[CMD_BYTE(2, 0, 1)] = ((0x8 | (0x3 << 1) | 0x1) << 4);
				writechunk[CMD_BYTE(2, 1, 1)] = (curcmd->data[0] >> 8) & 0xff;
				writechunk[CMD_BYTE(2, 2, 1)] = curcmd->data[0] & 0xff;
			}
			
			if (curcmd->datalen > 1) {
				if (curcmd->flags & __VPM150M_WR)
					writechunk[CMD_BYTE(3, 0, 1)] = ((0x8 | (0x1 << 1)) << 4);
				else
					writechunk[CMD_BYTE(3, 0, 1)] = ((0x8 | (0x1 << 1) | 0x1) << 4);
				writechunk[CMD_BYTE(3, 1, 1)] = (curcmd->data[1] >> 8) & 0xff;
				writechunk[CMD_BYTE(3, 2, 1)] = curcmd->data[1] & 0xff;
			} else {
				/* Fill in the rest */
				writechunk[CMD_BYTE(3, 0, 1)] = 0;
				writechunk[CMD_BYTE(3, 1, 1)] = 0;
				writechunk[CMD_BYTE(3, 2, 1)] = 0;
			}

			if (curcmd->datalen > 2) {
				if (curcmd->flags & __VPM150M_WR)
					writechunk[CMD_BYTE(4, 0, 1)] = ((0x8 | (0x1 << 1)) << 4);
				else
					writechunk[CMD_BYTE(4, 0, 1)] = ((0x8 | (0x1 << 1) | 0x1) << 4);
				writechunk[CMD_BYTE(4, 1, 1)] = (curcmd->data[2] >> 8) & 0xff;
				writechunk[CMD_BYTE(4, 2, 1)] = curcmd->data[2] & 0xff;
			} else {
				/* Fill in the rest */
				writechunk[CMD_BYTE(4, 0, 1)] = 0;
				writechunk[CMD_BYTE(4, 1, 1)] = 0;
				writechunk[CMD_BYTE(4, 2, 1)] = 0;
			}
		}
	} else if (test_and_clear_bit(VPM150M_SWRESET, &vpm150m->control)) {
		debug_printk(1, "Booting  VPMADT032\n");
		for (x = 0; x < 7; x++) {
			if (x == 0)
				writechunk[CMD_BYTE(x, 0, 1)] = (0x8 << 4);
			else
				writechunk[CMD_BYTE(x, 0, 1)] = 0x00;
			writechunk[CMD_BYTE(x, 1, 1)] = 0;
			if (x == 0)
				writechunk[CMD_BYTE(x, 2, 1)] = 0x01;
			else
				writechunk[CMD_BYTE(x, 2, 1)] = 0x00;
		}
	} else {
		for (x = 0; x < 7; x++) {
			writechunk[CMD_BYTE(x, 0, 1)] = 0x00;
			writechunk[CMD_BYTE(x, 1, 1)] = 0x00;
			writechunk[CMD_BYTE(x, 2, 1)] = 0x00;
		}
	}

	/* Add our leds in */
	for (x = 0; x < 7; x++)
		writechunk[CMD_BYTE(x, 0, 1)] |= leds;

#if 0
	int y;
	for (x = 0; x < 7; x++) {
		for (y = 0; y < 3; y++) {
			if (writechunk[CMD_BYTE(x, y, 1)] & 0x2) {
				module_printk("the test bit is high for byte %d\n", y);
			}
		}
	}
#endif

	/* Now let's figure out if we need to check for DTMF */
	/* polling */
	if (test_bit(VPM150M_ACTIVE, &vpm150m->control) && !whichframe && !(wc->intcount % 100))
		queue_work(vpm150m->wq, &vpm150m->work_dtmf);

#if 0
	/* This may be needed sometime in the future to troubleshoot ADT related issues. */
	if (test_bit(VPM150M_ACTIVE, &vpm150m->control) && !whichframe && !(wc->intcount % 10000))
		queue_work(vpm150m->wq, &vpm150m->work_debug);
#endif 
}

inline void vpm150m_cmd_decipher(struct t1 *wc, volatile unsigned char *readchunk)
{
	unsigned char ident;
	int x, i;

	/* Skip audio */
	readchunk += 66;
	/* Search for any pending results */
	for (x = 0; x < VPM150M_MAX_COMMANDS; x++) {
		if ((wc->vpm150m->cmdq[x].flags & (__VPM150M_RD | __VPM150M_WR)) && 
		    (wc->vpm150m->cmdq[x].flags & (__VPM150M_TX)) && 
		   !(wc->vpm150m->cmdq[x].flags & (__VPM150M_FIN))) {
		   	ident = wc->vpm150m->cmdq[x].ident;
		   	if (ident == wc->rxident) {
				/* Store result */
				for (i = 0; i < wc->vpm150m->cmdq[x].datalen; i++) {
					wc->vpm150m->cmdq[x].data[i] = (0xff & readchunk[CMD_BYTE((2 + i), 1, 1)]) << 8;
					wc->vpm150m->cmdq[x].data[i] |= readchunk[CMD_BYTE((2 + i), 2, 1)];
				}
				if (wc->vpm150m->cmdq[x].flags & __VPM150M_WR) {
					/* Go ahead and clear out writes since they need no acknowledgement */
					wc->vpm150m->cmdq[x].flags = 0;
				} else
					wc->vpm150m->cmdq[x].flags |= __VPM150M_FIN;
				break;
			}
		}
	}
}

static inline struct t1 * wc_find_iface(unsigned short dspid)
{
	int i;
	struct t1 *ret = NULL;
	unsigned long flags;

	spin_lock_irqsave(&ifacelock, flags);
	for (i = 0; i < WC_MAX_IFACES; i++)
		if (ifaces[i] && ifaces[i]->vpm150m && (ifaces[i]->vpm150m->dspid == dspid))
			ret = ifaces[i];
	spin_unlock_irqrestore(&ifacelock, flags);

	return ret;
}

static struct vpm150m_cmd * vpm150m_empty_slot(struct t1 *wc)
{
	unsigned int x;

	for (x = 0; x < VPM150M_MAX_COMMANDS; x++) {
		if (!wc->vpm150m->cmdq[x].flags) {
			return &wc->vpm150m->cmdq[x];
		}
	}
	return NULL;
}

/* Wait for any outstanding commands to be completed. */
static inline int vpm150m_io_wait(struct t1 *wc)
{
	int x;
	int ret=0;
	for (x=0; x < VPM150M_MAX_COMMANDS;) {
		if (wc->vpm150m->cmdq[x].flags) {
			if ((ret=schluffen(&wc->regq))) {
				return ret;
			}
			x=0;
		}
		else {
			++x;
		}
	}
	return ret;
}

int t1_vpm150m_getreg_full_async(struct t1 *wc, int pagechange, unsigned int len, 
	unsigned short addr, unsigned short *outbuf, struct vpm150m_cmd **hit_p)
{
	int ret=0;
	unsigned long flags;
	BUG_ON(!hit_p);
	spin_lock_irqsave(&wc->reglock, flags);
	(*hit_p) = vpm150m_empty_slot(wc);
	if (*hit_p) {
		(*hit_p)->flags = __VPM150M_RD;
		if (pagechange) {
			(*hit_p)->flags |= __VPM150M_RWPAGE;
		}
		(*hit_p)->datalen = len;
		(*hit_p)->address = addr;
		memset((*hit_p)->data, 0, len*sizeof(outbuf[0]));
	}
	else {
		ret = -EBUSY;
	}
	spin_unlock_irqrestore(&wc->reglock, flags);
	return ret;
}

int t1_vpm150m_getreg_full_return(struct t1 *wc, int pagechange, unsigned int len,
	unsigned short addr, unsigned short *outbuf, struct vpm150m_cmd **hit_p)
{
	int ret = 0;
	unsigned long flags;
	BUG_ON(!hit_p);
	spin_lock_irqsave(&wc->reglock, flags);
	do {
		if ((*hit_p)->flags & __VPM150M_FIN) {
			memcpy(outbuf, (*hit_p)->data, len*(sizeof(outbuf[0])));
			(*hit_p)->flags = 0;
			(*hit_p) = NULL;
			ret = 0;
		}
		else {
			spin_unlock_irqrestore(&wc->reglock, flags);
			if ((ret=schluffen(&wc->regq))) {
				return ret;
			}
			spin_lock_irqsave(&wc->reglock, flags);
			ret = -EBUSY;
		}
	} while (-EBUSY == ret);
	spin_unlock_irqrestore(&wc->reglock, flags);
	return ret;
}

int t1_vpm150m_getreg_full(struct t1 *wc, int pagechange, unsigned int len, unsigned short addr, unsigned short *outbuf)
{
	struct vpm150m_cmd *hit = 0;
	int ret = 0;
	do {
		ret = t1_vpm150m_getreg_full_async(wc, pagechange, len, addr, outbuf, &hit);
		if (!hit) {
			if ( -EBUSY == ret ) {
				if ((ret = schluffen(&wc->regq))) 
					return ret;
			}
			BUG_ON( 0 != ret);
		}
	} while (!hit);

	ret = t1_vpm150m_getreg_full_return(wc, pagechange, len, addr, outbuf, &hit);
	return ret;
}

int t1_vpm150m_setreg_full(struct t1 *wc, int pagechange, unsigned int len, unsigned int addr, unsigned short *data)
{
	unsigned long flags;
	struct vpm150m_cmd *hit;
	int ret, i;
	do {
		spin_lock_irqsave(&wc->reglock, flags);
		hit = vpm150m_empty_slot(wc);
		if (hit) {
			hit->flags = __VPM150M_WR;
			if (pagechange)
				hit->flags |= __VPM150M_RWPAGE;
			hit->address = addr;
			hit->datalen = len;
			for (i = 0; i < len; i++)
				hit->data[i] = data[i];
		}
		spin_unlock_irqrestore(&wc->reglock, flags);
		if (!hit) {
			if ((ret = schluffen(&wc->regq)))
				return ret;
		}
	} while (!hit);
	return (hit) ? 0 : -1;
}

int t1_vpm150m_setpage(struct t1 *wc, unsigned short addr)
{
	addr &= 0xf;
	/* Let's optimize this a little bit */
	if (wc->vpm150m->curpage == addr)
		return 0;
	else {
		wc->vpm150m->curpage = addr;
	}

	return t1_vpm150m_setreg_full(wc, 1, 1, 0, &addr);
}

unsigned char t1_vpm150m_getpage(struct t1 *wc)
{
	unsigned short res;
	t1_vpm150m_getreg_full(wc, 1, 1, 0, &res);
	return res;
}

int t1_vpm150m_setreg(struct t1 *wc, unsigned int len, unsigned int addr, unsigned short *data)
{
	int res;
	t1_vpm150m_setpage(wc, addr >> 16);
	if ((addr >> 16) != ((addr + len) >> 16))
		module_printk("setreg: You found it!\n");
	res = t1_vpm150m_setreg_full(wc, 0, len, addr & 0xffff, data);
	return res;
}

unsigned short t1_vpm150m_getreg(struct t1 *wc, unsigned int len, unsigned int addr, unsigned short *data)
{
	unsigned short res;
	t1_vpm150m_setpage(wc, addr >> 16);
	if ((addr >> 16) != ((addr + len) >> 16))
		module_printk("getreg: You found it!\n");
	res = t1_vpm150m_getreg_full(wc, 0, len, addr & 0xffff, data);
	return res;
}

static char vpm150mtone_to_zaptone(GpakToneCodes_t tone)
{
	switch (tone) {
		case DtmfDigit0:
			return '0';
		case DtmfDigit1:
			return '1';
		case DtmfDigit2:
			return '2';
		case DtmfDigit3:
			return '3';
		case DtmfDigit4:
			return '4';
		case DtmfDigit5:
			return '5';
		case DtmfDigit6:
			return '6';
		case DtmfDigit7:
			return '7';
		case DtmfDigit8:
			return '8';
		case DtmfDigit9:
			return '9';
		case DtmfDigitPnd:
			return '#';
		case DtmfDigitSt:
			return '*';
		case DtmfDigitA:
			return 'A';
		case DtmfDigitB:
			return 'B';
		case DtmfDigitC:
			return 'C';
		case DtmfDigitD:
			return 'D';
		case EndofCngDigit:
			return 'f';
		default:
			return 0;
	}
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void vpm150m_echocan_bh(void *data)
{
	struct vpm150m *vpm150m = data;
#else
static void vpm150m_echocan_bh(struct work_struct *data)
{
	struct vpm150m *vpm150m = container_of(data, struct vpm150m, work_echocan);
#endif
	struct t1 *wc = vpm150m->wc;
	struct list_head *task;
	struct list_head *next_task;
	unsigned long flags;

	list_for_each_safe(task, next_task, &vpm150m->worklist) {
		struct vpm150m_workentry *we = list_entry(task, struct vpm150m_workentry, list);
		struct zt_chan *chan = we->chan;
		int deflaw;
		int res;
		GPAK_AlgControlStat_t pstatus;

		if (we->params.tap_length) {
			/* configure channel for the ulaw/alaw */
			unsigned int start = wc->intcount;

			if (memcmp(&we->params, &vpm150m->chan_params[chan->chanpos - 1], sizeof(we->params))) {
				/* set parameters */
				vpm150m->chan_params[chan->chanpos - 1] = we->params;
			}

			deflaw = chan->span->deflaw;
			debug_printk(1, "Enabling EC on channel %d (law %d)\n", chan->chanpos, deflaw);
			if (deflaw == 2) /* alaw */
				res = gpakAlgControl(vpm150m->dspid, chan->chanpos - 1, EnableALawSwCompanding, &pstatus);
			else if (deflaw == 1) /* alaw */
				res = gpakAlgControl(vpm150m->dspid, chan->chanpos - 1, EnableMuLawSwCompanding, &pstatus);
			else {
				module_printk("Undefined law for channel %d.\n", chan->chanpos);
				res = -1;
			}

			if (res) {
				module_printk("Unable to set SW Companding on channel %d (reason %d)\n", chan->chanpos, res);
			}

			res = gpakAlgControl(vpm150m->dspid, chan->chanpos - 1, EnableEcanA, &pstatus);
			debug_printk(2, "Echo can enable took %d ms\n", wc->intcount - start);
		} else {
			unsigned int start = wc->intcount;
			debug_printk(1, "Disabling EC on channel %d\n", chan->chanpos);
			res = gpakAlgControl(vpm150m->dspid, chan->chanpos - 1, BypassSwCompanding, &pstatus);
			if (res)
				module_printk("Unable to disable sw companding on echo cancellation  channel %d (reason %d)\n", chan->chanpos, res);
			res = gpakAlgControl(vpm150m->dspid, chan->chanpos - 1, BypassEcanA, &pstatus);
			if (res)
				module_printk("Unable to disable echo can on channel %d (reason %d)\n", chan->chanpos, res);
			debug_printk(2, "Echocan disable took %d ms\n", wc->intcount - start);
		}
		if (res) {
			module_printk("Unable to toggle echo cancellation on channel %d (reason %d)\n", chan->chanpos, res);
		}

		spin_lock_irqsave(&vpm150m->lock, flags);
		list_del(task);
		spin_unlock_irqrestore(&vpm150m->lock, flags);
		kfree(we);
	}
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void vpm150m_debug_bh(void *data)
{
	struct vpm150m *vpm150m = data;
#else
static void vpm150m_debug_bh(struct work_struct *data)
{
	struct vpm150m *vpm150m = container_of(data, struct vpm150m, work_debug);
#endif
	unsigned short int FrammingError1Count, FramingError2Count, FramingError3Count,
				   		DmaStopErrorCount, DmaSlipStatsBuffer;

	if (gpakReadFramingStats(vpm150m->dspid, &FrammingError1Count, &FramingError2Count, &FramingError3Count,
				   		&DmaStopErrorCount, &DmaSlipStatsBuffer)) 
	{
		module_printk("There was an error getting framing stats.\n");
	}
	if (FrammingError1Count||FramingError2Count||FramingError3Count||DmaStopErrorCount||DmaSlipStatsBuffer)
	{
		module_printk("FramingStats Error: %d %d %d %d %d\n",
		FrammingError1Count, FramingError2Count, FramingError3Count, DmaStopErrorCount, DmaSlipStatsBuffer);
	}
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void vpm150m_dtmf_bh(void *data)
{
	struct vpm150m *vpm150m = data;
#else
static void vpm150m_dtmf_bh(struct work_struct *data)
{
	struct vpm150m *vpm150m = container_of(data, struct vpm150m, work_dtmf);
#endif
	struct t1 *wc = vpm150m->wc;
	int i;

	for (i = 0; i < wc->span.channels; i++) {
		int enable = -1;
		if (test_bit(i, &vpm150m->desireddtmfmutestate)) {
			if (!test_bit(i, &vpm150m->curdtmfmutestate)) {
				enable = 1;
			}
		} else {
			if (test_bit(i, &vpm150m->curdtmfmutestate)) {
				enable = 0;
			}
		}
		if (enable > -1) {
			unsigned int start = wc->intcount;
			GPAK_AlgControlStat_t pstatus;
			int res;

			if (enable) {
				res = gpakAlgControl(vpm150m->dspid, i, EnableDTMFMuteA, &pstatus);
				debug_printk(2, "DTMF mute enable took %d ms\n", wc->intcount - start);
			} else {
				res = gpakAlgControl(vpm150m->dspid, i, DisableDTMFMuteA, &pstatus);
				debug_printk(2, "DTMF mute disable took %d ms\n", wc->intcount - start);
			}
			if (!res)
				change_bit(i, &vpm150m->curdtmfmutestate);
		}
	}

	if (test_bit(VPM150M_DTMFDETECT, &vpm150m->control)) {
		unsigned short channel;
		GpakAsyncEventCode_t eventcode;
		GpakAsyncEventData_t eventdata;
		gpakReadEventFIFOMessageStat_t  res;
		unsigned int start = wc->intcount;

		do {
			res = gpakReadEventFIFOMessage(vpm150m->dspid, &channel, &eventcode, &eventdata);
			debug_printk(3, "ReadEventFIFOMessage took %d ms\n", wc->intcount - start);

			if (res == RefInvalidEvent || res == RefDspCommFailure) {
				module_printk("Uh oh (%d)\n", res);
				continue;
			}

			if (eventcode == EventToneDetect) {
				GpakToneCodes_t tone = eventdata.toneEvent.ToneCode;
				int duration = eventdata.toneEvent.ToneDuration;
				char zaptone = vpm150mtone_to_zaptone(tone);

				debug_printk(1, "Channel %d: Detected DTMF tone %d of duration %d\n", channel + 1, tone, duration);

				if (test_bit(channel, &wc->dtmfmask) && (eventdata.toneEvent.ToneDuration > 0)) {
					struct zt_chan *chan = &wc->chans[channel];

					module_printk("DTMF detected channel=%d tone=%d duration=%d\n", channel + 1, tone, duration);

					if ((tone != EndofMFDigit) && (zaptone != 0)) {
						vpm150m->curtone[channel] = tone;

						if (test_bit(channel, &vpm150m->curdtmfmutestate)) {
							unsigned long flags;
							int y;

							/* Mute the audio data buffers */
							spin_lock_irqsave(&chan->lock, flags);
							for (y = 0; y < chan->numbufs; y++) {
								if ((chan->inreadbuf > -1) && (chan->readidx[y]))
									memset(chan->readbuf[chan->inreadbuf], ZT_XLAW(0, chan), chan->readidx[y]);
							}
							spin_unlock_irqrestore(&chan->lock, flags);
						}
						if (!test_bit(channel, &wc->dtmfactive)) {
							debug_printk(1,"Queuing DTMFDOWN %c\n", zaptone);
							set_bit(channel, &wc->dtmfactive);
							zt_qevent_lock(chan, (ZT_EVENT_DTMFDOWN | zaptone));
						}
					} else if ((tone == EndofMFDigit) && test_bit(channel, &wc->dtmfactive)) {
						debug_printk(1,"Queuing DTMFUP %c\n", vpm150mtone_to_zaptone(vpm150m->curtone[channel]));
						zt_qevent_lock(chan, (ZT_EVENT_DTMFUP | vpm150mtone_to_zaptone(vpm150m->curtone[channel])));
						clear_bit(channel, &wc->dtmfactive);
					}
				}
			}
		} while ((res == RefEventAvail));
	}
	
	return;
}

void t1_vpm150m_init(struct t1 *wc) {	
	struct vpm150m *vpm150m;
	unsigned short i;
	unsigned short reg;
	unsigned long flags;
	gpakPingDspStat_t pingstatus;
	gpakDownloadStatus_t downloadstatus;
	struct t1_firmware fw;
	struct firmware embedded_firmware;
	const struct firmware *firmware = &embedded_firmware;
#if !defined(HOTPLUG_FIRMWARE)
	extern void _binary_zaptel_fw_vpmadt032_bin_size;
	extern u8 _binary_zaptel_fw_vpmadt032_bin_start[];
#else
	static const char vpmadt032_firmware[] = "zaptel-fw-vpmadt032.bin";
	struct pci_dev* pdev = voicebus_get_pci_dev(wc->vb);
#endif

#if 0
	unsigned short omsg[4] = { 0xdead, 0xbeef, 0x1111, 0x2222};
	unsigned short imsg[4];
#endif

	if (!vpmsupport) {
		module_printk("VPM Support Disabled\n");
		wc->vpm150m = NULL;
		return;
	}

	vpm150m = kmalloc(sizeof(struct vpm150m), GFP_KERNEL);

	if (!vpm150m) {
		module_printk("Unable to allocate VPMADT032!\n");
		return;
	}
	memset(vpm150m, 0, sizeof(struct vpm150m));
	
	/* Init our vpm150m struct */
	sema_init(&vpm150m->sem, 1);
	vpm150m->curpage = 0x80;

	for (i = 0; i < sizeof(ifaces) / sizeof(ifaces[0]); i++) {
		if (ifaces[i] == wc)
			vpm150m->dspid = i;
	}

	debug_printk(1, "Setting VPMADT032 DSP ID to %d\n", vpm150m->dspid);
	spin_lock_irqsave(&wc->reglock, flags);
	wc->vpm150m = vpm150m;
	spin_unlock_irqrestore(&wc->reglock, flags);

	for (i = 0; i < 10; i++)
		schluffen(&wc->regq);

	debug_printk(1, "Looking for VPMADT032 by testing page access: ");
	for (i = 0; i < 0xf; i++) {
		int x;
		for (x = 0; x < 3; x++) {
			t1_vpm150m_setpage(wc, i);
			reg = t1_vpm150m_getpage(wc);
			if (reg != i) {
				/* If they have debug turned on we want them to be able to 
				 * report where in the code the module failed to come up. */
				debug_printk(1, "Either no VPMADT032 module present or the module failed VPM page access test (%x != %x)\n", i, reg);
				goto failed_exit;
			}
		}
	}
	debug_printk(1, "Passed\n");

	set_bit(VPM150M_HPIRESET, &vpm150m->control);
	msleep(2000);

	/* Set us up to page 0 */
	t1_vpm150m_setpage(wc, 0);
	debug_printk(1, "VPMADT032 now doing address test: ");
	for (i = 0; i < 16; i++) {
		int x;
		for (x = 0; x < 2; x++) {
			t1_vpm150m_setreg(wc, 1, 0x1000, &i);
			t1_vpm150m_getreg(wc, 1, 0x1000, &reg);
			if (reg != i) { 
				module_printk("VPMADT032 Failed address test: sent %x != %x on try %d\n", i, reg, x);
				goto failed_exit;
			}
		}
	}
	debug_printk(1, "Passed\n");
	
#define TEST_SIZE 2
	if (debug) {
		int i;
		unsigned short msg[TEST_SIZE];

		set_bit(VPM150M_HPIRESET, &vpm150m->control);
		msleep(2000);
		gpakReadDspMemory(vpm150m->dspid, 0x1000, TEST_SIZE, msg);
		for (i = 0; i< TEST_SIZE; i++)
			printk("%x ", msg[i]);
		printk("\n");
		for (i = 0; i< TEST_SIZE; i++)
			msg[i] = 0xdead;
		gpakWriteDspMemory(vpm150m->dspid, 0x1000, TEST_SIZE, msg);
		gpakWriteDspMemory(vpm150m->dspid, 0x1000, TEST_SIZE, msg);
		gpakReadDspMemory(vpm150m->dspid, 0x1000, TEST_SIZE, msg);
		for (i = 0; i< TEST_SIZE; i++)
			printk("%x ", msg[i]);
		printk("\n");
		gpakReadDspMemory(vpm150m->dspid, 0x1000, TEST_SIZE, msg);
		for (i = 0; i< TEST_SIZE; i++)
			printk("%x ", msg[i]);
		printk("\n");
		for (i = 0; i< TEST_SIZE; i++)
			msg[i] = 0xbeef;
		gpakWriteDspMemory(vpm150m->dspid, 0x1000, TEST_SIZE, msg);
		gpakReadDspMemory(vpm150m->dspid, 0x1000, TEST_SIZE, msg);
		for (i = 0; i< TEST_SIZE; i++)
			printk("%x ", msg[i]);
		printk("\n");
		gpakReadDspMemory(vpm150m->dspid, 0x1000, TEST_SIZE, msg);
		for (i = 0; i< TEST_SIZE; i++)
			printk("%x ", msg[i]);
		printk("\n");
		for (i = 0; i< TEST_SIZE; i++)
			msg[i] = 0x1111;
		gpakWriteDspMemory(vpm150m->dspid, 0x1000, TEST_SIZE, msg);
		gpakReadDspMemory(vpm150m->dspid, 0x1000, TEST_SIZE, msg);
		for (i = 0; i< TEST_SIZE; i++)
			printk("%x ", msg[i]);
		printk("\n");
		gpakReadDspMemory(vpm150m->dspid, 0x1000, TEST_SIZE, msg);
		for (i = 0; i< TEST_SIZE; i++)
			printk("%x ", msg[i]);
		printk("\n");
		for (i = 0; i< TEST_SIZE; i++)
			msg[i] = 0x2222;
		gpakWriteDspMemory(vpm150m->dspid, 0x1000, TEST_SIZE, msg);
		gpakReadDspMemory(vpm150m->dspid, 0x1000, TEST_SIZE, msg);
		for (i = 0; i< TEST_SIZE; i++)
			printk("%x ", msg[i]);
		printk("\n");
		gpakReadDspMemory(vpm150m->dspid, 0x1000, TEST_SIZE, msg);
		for (i = 0; i< TEST_SIZE; i++)
			printk("%x ", msg[i]);
		printk("\n");
	}

#if defined(HOTPLUG_FIRMWARE)
		if ((request_firmware(&firmware, vpmadt032_firmware, &pdev->dev) != 0) ||
		    !firmware) {
			printk("VPMADT032: firmware %s not available from userspace\n", vpmadt032_firmware);
			goto failed_exit;
		}
#else
		embedded_firmware.data = _binary_zaptel_fw_vpmadt032_bin_start;
		embedded_firmware.size = (size_t) &_binary_zaptel_fw_vpmadt032_bin_size;
#endif
		fw.fw = firmware;
		fw.offset = 0;

		set_bit(VPM150M_HPIRESET, &vpm150m->control);

		while (test_bit(VPM150M_HPIRESET, &vpm150m->control))
			schluffen(&wc->regq);

		module_printk("VPMADT032 Loading firwmare... ");
		downloadstatus = gpakDownloadDsp(vpm150m->dspid, &fw);

		if (firmware != &embedded_firmware)
			release_firmware(firmware);

		if (downloadstatus != 0) {
			module_printk("Unable to download firmware to VPMADT032 with cause %d\n", downloadstatus);
			goto failed_exit;
		} else {
			module_printk("Success\n");
		}

		set_bit(VPM150M_SWRESET, &vpm150m->control);

		while (test_bit(VPM150M_SWRESET, &vpm150m->control))
			schluffen(&wc->regq);

		msleep(700);
#if 0
	}
#endif

	pingstatus = gpakPingDsp(vpm150m->dspid, &vpm150m->version);

	if (!pingstatus) {
		debug_printk(1, "Version of DSP is %x\n", vpm150m->version);
	} else {
		module_printk("Unable to ping the DSP (%d)!\n", pingstatus);
		goto failed_exit;
	}

	/* workqueue for DTMF and wc->span functions that cannot sleep */
	spin_lock_init(&vpm150m->lock);
	vpm150m->wq = create_singlethread_workqueue("wcte12xp");
	vpm150m->wc = wc;
	if (!vpm150m->wq) {
		module_printk("Unable to create work queue!\n");
		goto failed_exit;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	INIT_WORK(&vpm150m->work_echocan, vpm150m_echocan_bh, vpm150m);
	INIT_WORK(&vpm150m->work_dtmf, vpm150m_dtmf_bh, vpm150m);
	INIT_WORK(&vpm150m->work_debug, vpm150m_debug_bh, vpm150m);
#else
	INIT_WORK(&vpm150m->work_echocan, vpm150m_echocan_bh);
	INIT_WORK(&vpm150m->work_dtmf, vpm150m_dtmf_bh);
	INIT_WORK(&vpm150m->work_debug, vpm150m_debug_bh);
#endif
	INIT_LIST_HEAD(&wc->vpm150m->worklist); /* list of echocan tasks */

	if (vpm150m_config_hw(wc)) {
		goto failed_exit;
	}

	return;

failed_exit:
	if (vpm150m->wq) {
		destroy_workqueue(vpm150m->wq);
	}
	spin_lock_irqsave(&wc->reglock, flags);
	wc->vpm150m = NULL;
	spin_unlock_irqrestore(&wc->reglock, flags);
	kfree(vpm150m);

	return;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * gpakReadDspMemory - Read DSP memory.
 *
 * FUNCTION
 *  This function reads a contiguous block of words from DSP memory starting at
 *  the specified address.
 *
 * RETURNS
 *  nothing
 *
 */
void gpakReadDspMemory(
    unsigned short int DspId,   /* DSP Identifier (0 to MAX_DSP_CORES-1) */
    DSP_ADDRESS DspAddress,     /* DSP's memory address of first word */
    unsigned int NumWords,      /* number of contiguous words to read */
    DSP_WORD *pWordValues       /* pointer to array of word values variable */
    )
{
	struct t1 *wc = wc_find_iface(DspId);
	int i;
	int transcount;
	int ret;

	vpm150m_io_wait(wc);
	if ( NumWords < VPM150M_MAX_COMMANDS ) {
		struct vpm150m_cmd* cmds[VPM150M_MAX_COMMANDS] = {0};
		t1_vpm150m_setpage(wc, DspAddress >> 16);
		DspAddress &= 0xffff;
		for (i=0; i < NumWords; ++i) {
			ret = t1_vpm150m_getreg_full_async(wc,0,1,DspAddress+i,&pWordValues[i],
				&cmds[i]);
			if (0 != ret) {
				return;
			}
		}
		for (i=NumWords-1; i >=0; --i) {
			ret = t1_vpm150m_getreg_full_return(wc,0,1,DspAddress+i,&pWordValues[i],
				&cmds[i]);
			if (0 != ret) {
				return;
			}
		}
	}
	else {
		for (i = 0; i < NumWords;) {
			if ((NumWords - i) > VPM150M_MAX_DATA)
				transcount = VPM150M_MAX_DATA;
			else
				transcount = NumWords - i;
			t1_vpm150m_getreg(wc, transcount, DspAddress + i, &pWordValues[i]);
			i += transcount;
		}
	}
	return;
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * gpakWriteDspMemory - Write DSP memory.
 *
 * FUNCTION
 *  This function writes a contiguous block of words to DSP memory starting at
 *  the specified address.
 *
 * RETURNS
 *  nothing
 *
 */
void gpakWriteDspMemory(
    unsigned short int DspId,   /* DSP Identifier (0 to MAX_DSP_CORES-1) */
    DSP_ADDRESS DspAddress,     /* DSP's memory address of first word */
    unsigned int NumWords,      /* number of contiguous words to write */
    DSP_WORD *pWordValues       /* pointer to array of word values to write */
    )
{

	struct t1 *wc = wc_find_iface(DspId);
	int i;
	int transcount;

	if (wc && wc->vpm150m) {
		for (i = 0; i < NumWords;) {
			if ((NumWords - i) > VPM150M_MAX_DATA)
					transcount = VPM150M_MAX_DATA;
			else
					transcount = NumWords - i;
			t1_vpm150m_setreg(wc, transcount, DspAddress + i, &pWordValues[i]);
			i += transcount;
		}
	}
	return;

}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * gpakHostDelay - Delay for a fixed time interval.
 *
 * FUNCTION
 *  This function delays for a fixed time interval before returning. The time
 *  interval is the Host Port Interface sampling period when polling a DSP for
 *  replies to command messages.
 *
 * RETURNS
 *  nothing
 *
 */
void gpakHostDelay(void)
{
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * gpakLockAccess - Lock access to the specified DSP.
 *
 * FUNCTION
 *  This function aquires exclusive access to the specified DSP.
 *
 * RETURNS
 *  nothing
 *
 */
void gpakLockAccess(unsigned short DspId)
{
	struct t1 *wc;

	wc = wc_find_iface(DspId);

	if (wc) {
		struct vpm150m *vpm = wc->vpm150m;

		if (vpm)
			down_interruptible(&vpm->sem);
	}
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * gpakUnlockAccess - Unlock access to the specified DSP.
 *
 * FUNCTION
 *  This function releases exclusive access to the specified DSP.
 *
 * RETURNS
 *  nothing
 *
 */
void gpakUnlockAccess(unsigned short DspId)
{
	struct t1 *wc;

	wc = wc_find_iface(DspId);

	if (wc) {
		struct vpm150m *vpm = wc->vpm150m;

		if (vpm)
			up(&vpm->sem);
	}
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
 * gpakReadFile - Read a block of bytes from a G.PAK Download file.
 *
 * FUNCTION
 *  This function reads a contiguous block of bytes from a G.PAK Download file
 *  starting at the current file position.
 *
 * RETURNS
 *  The number of bytes read from the file.
 *   -1 indicates an error occurred.
 *    0 indicates all bytes have been read (end of file)
 *
 */
int gpakReadFile(
    GPAK_FILE_ID FileId,        /* G.PAK Download File Identifier */
    unsigned char *pBuffer,	/* pointer to buffer for storing bytes */
    unsigned int NumBytes       /* number of bytes to read */
    )
{
	struct t1_firmware *fw = FileId;
	unsigned int i, count;

	if (!fw || !fw->fw)
		return -1;

	if (NumBytes > (fw->fw->size - fw->offset))
		count = fw->fw->size - fw->offset;
	else
		count = NumBytes;

	for (i = 0; i < count; i++)
		pBuffer[i] = fw->fw->data[fw->offset + i];

	fw->offset += count;

	return count;
}

int vpm150m_config_hw(struct t1 *wc)
{
	struct vpm150m *vpm150m = wc->vpm150m;
	gpakConfigPortStatus_t configportstatus;
	GpakPortConfig_t portconfig;
	GPAK_PortConfigStat_t pstatus;
	GpakChannelConfig_t chanconfig;
	GPAK_ChannelConfigStat_t cstatus;
	GPAK_AlgControlStat_t algstatus;

	int res, i;

	memset(&portconfig, 0, sizeof(GpakPortConfig_t));

	/* First Serial Port config */
	portconfig.SlotsSelect1 = SlotCfgNone;
	portconfig.FirstBlockNum1 = 0;
	portconfig.FirstSlotMask1 = 0x0000;
	portconfig.SecBlockNum1 = 1;
	portconfig.SecSlotMask1 = 0x0000;
	portconfig.SerialWordSize1 = SerWordSize8;
	portconfig.CompandingMode1 = cmpNone;
	portconfig.TxFrameSyncPolarity1 = FrameSyncActHigh;
	portconfig.RxFrameSyncPolarity1 = FrameSyncActHigh;
	portconfig.TxClockPolarity1 = SerClockActHigh;
	portconfig.RxClockPolarity1 = SerClockActHigh;
	portconfig.TxDataDelay1 = DataDelay0;
	portconfig.RxDataDelay1 = DataDelay0;
	portconfig.DxDelay1 = Disabled;
	portconfig.ThirdSlotMask1 = 0x0000;
	portconfig.FouthSlotMask1 = 0x0000;
	portconfig.FifthSlotMask1 = 0x0000;
	portconfig.SixthSlotMask1 = 0x0000;
	portconfig.SevenSlotMask1 = 0x0000;
	portconfig.EightSlotMask1 = 0x0000;

	/* Second Serial Port config */
	portconfig.SlotsSelect2 = SlotCfg8Groups;
	portconfig.FirstBlockNum2 = 0;
	portconfig.FirstSlotMask2 = 0x5554;
	portconfig.SecBlockNum2 = 1;
	portconfig.SecSlotMask2 = 0x5555;
	portconfig.ThirdSlotMask2 = 0x5555;
	portconfig.FouthSlotMask2 = 0x5555;
	portconfig.SerialWordSize2 = SerWordSize8;
	portconfig.CompandingMode2 = cmpNone;
	portconfig.TxFrameSyncPolarity2 = FrameSyncActHigh;
	portconfig.RxFrameSyncPolarity2 = FrameSyncActHigh;
	portconfig.TxClockPolarity2 = SerClockActHigh;
	portconfig.RxClockPolarity2 = SerClockActHigh;
	portconfig.TxDataDelay2 = DataDelay0;
	portconfig.RxDataDelay2 = DataDelay0;
	portconfig.DxDelay2 = Disabled;
	portconfig.FifthSlotMask2 = 0x0001;
	portconfig.SixthSlotMask2 = 0x0000;
	portconfig.SevenSlotMask2 = 0x0000;
	portconfig.EightSlotMask2 = 0x0000;

	/* Third Serial Port Config */
	portconfig.SlotsSelect3 = SlotCfg8Groups;
	portconfig.FirstBlockNum3 = 0;
	portconfig.FirstSlotMask3 = 0x5554;
	portconfig.SecBlockNum3 = 1;
	portconfig.SecSlotMask3 = 0x5555;
	portconfig.SerialWordSize3 = SerWordSize8;
	portconfig.CompandingMode3 = cmpNone;
	portconfig.TxFrameSyncPolarity3 = FrameSyncActHigh;
	portconfig.RxFrameSyncPolarity3 = FrameSyncActHigh;
	portconfig.TxClockPolarity3 = SerClockActHigh;
	portconfig.RxClockPolarity3 = SerClockActLow;
	portconfig.TxDataDelay3 = DataDelay0;
	portconfig.RxDataDelay3 = DataDelay0;
	portconfig.DxDelay3 = Disabled;
	portconfig.ThirdSlotMask3 = 0x5555;
	portconfig.FouthSlotMask3 = 0x5555;
	portconfig.FifthSlotMask3 = 0x0001;
	portconfig.SixthSlotMask3 = 0x0000;
	portconfig.SevenSlotMask3 = 0x0000;
	portconfig.EightSlotMask3 = 0x0000;

	if ((configportstatus = gpakConfigurePorts(vpm150m->dspid, &portconfig, &pstatus))) {
		module_printk("Configuration of ports failed (%d)!\n", configportstatus);
		return -1;
	} else {
		debug_printk(1, "Configured McBSP ports successfully\n");
	}

	if ((res = gpakPingDsp(vpm150m->dspid, &vpm150m->version))) {
		module_printk("Error pinging DSP (%d)\n", res);
		return -1;
	}

	for (i = 0; i < 32; i++) {
		/* Let's configure a channel */
		chanconfig.PcmInPortA = 3;
		chanconfig.PcmInSlotA = (i + 1) * 2;
		chanconfig.PcmOutPortA = 2;
		chanconfig.PcmOutSlotA = (i + 1) * 2;
		chanconfig.PcmInPortB = 2;
		chanconfig.PcmInSlotB = (i + 1) * 2;
		chanconfig.PcmOutPortB = 3;
		chanconfig.PcmOutSlotB = (i + 1) * 2;
		if (vpmdtmfsupport) {
			chanconfig.ToneTypesA = DTMF_tone;
			chanconfig.MuteToneA = Enabled;
			chanconfig.FaxCngDetA = Enabled;
		} else {
			chanconfig.ToneTypesA = Null_tone;
			chanconfig.MuteToneA = Disabled;
			chanconfig.FaxCngDetA = Disabled;
		}
		chanconfig.ToneTypesB = Null_tone;
		chanconfig.EcanEnableA = Enabled;
		chanconfig.EcanEnableB = Disabled;
		chanconfig.MuteToneB = Disabled;
		chanconfig.FaxCngDetB = Disabled;

		chanconfig.SoftwareCompand = cmpNone;

		chanconfig.FrameRate = rate10ms;
	
		chanconfig.EcanParametersA.EcanTapLength = 1024;
		chanconfig.EcanParametersA.EcanNlpType = vpmnlptype;
		chanconfig.EcanParametersA.EcanAdaptEnable = 1;
		chanconfig.EcanParametersA.EcanG165DetEnable = 1;
		chanconfig.EcanParametersA.EcanDblTalkThresh = 6;
		chanconfig.EcanParametersA.EcanNlpThreshold = vpmnlpthresh;
		chanconfig.EcanParametersA.EcanNlpConv = 0;
		chanconfig.EcanParametersA.EcanNlpUnConv = 0;
		chanconfig.EcanParametersA.EcanNlpMaxSuppress = vpmnlpmaxsupp;
		chanconfig.EcanParametersA.EcanCngThreshold = 43;
		chanconfig.EcanParametersA.EcanAdaptLimit = 50;
		chanconfig.EcanParametersA.EcanCrossCorrLimit = 15;
		chanconfig.EcanParametersA.EcanNumFirSegments = 3;
		chanconfig.EcanParametersA.EcanFirSegmentLen = 64;
	
		chanconfig.EcanParametersB.EcanTapLength = 1024;
		chanconfig.EcanParametersB.EcanNlpType = vpmnlptype;
		chanconfig.EcanParametersB.EcanAdaptEnable = 1;
		chanconfig.EcanParametersB.EcanG165DetEnable = 1;
		chanconfig.EcanParametersB.EcanDblTalkThresh = 6;
		chanconfig.EcanParametersB.EcanNlpThreshold = vpmnlpthresh;
		chanconfig.EcanParametersB.EcanNlpConv = 0;
		chanconfig.EcanParametersB.EcanNlpUnConv = 0;
		chanconfig.EcanParametersB.EcanNlpMaxSuppress = vpmnlpmaxsupp;
		chanconfig.EcanParametersB.EcanCngThreshold = 43;
		chanconfig.EcanParametersB.EcanAdaptLimit = 50;
		chanconfig.EcanParametersB.EcanCrossCorrLimit = 15;
		chanconfig.EcanParametersB.EcanNumFirSegments = 3;
		chanconfig.EcanParametersB.EcanFirSegmentLen = 64;
	
		if ((res = gpakConfigureChannel(vpm150m->dspid, i, tdmToTdm, &chanconfig, &cstatus))) {
			module_printk("Unable to configure channel (%d)\n", res);
			if (res == 1) {
				module_printk("Reason %d\n", cstatus);
			}
	
			return -1;
		}

		if ((res = gpakAlgControl(vpm150m->dspid, i, BypassEcanA, &algstatus))) {
			module_printk("Unable to disable echo can on channel %d (reason %d:%d)\n", i + 1, res, algstatus);
			return -1;
		}

		if (vpmdtmfsupport) {
			if ((res = gpakAlgControl(vpm150m->dspid, i, DisableDTMFMuteA, &algstatus))) {
				module_printk("Unable to disable dtmf muting  on channel %d (reason %d:%d)\n", i + 1, res, algstatus);
				return -1;
			}
		}
	}

	if ((res = gpakPingDsp(vpm150m->dspid, &vpm150m->version))) {
		module_printk("Error pinging DSP (%d)\n", res);
		return -1;
	}

	/* Turn on DTMF detection */
	if (vpmdtmfsupport)
		set_bit(VPM150M_DTMFDETECT, &vpm150m->control);
	set_bit(VPM150M_ACTIVE, &vpm150m->control);

	return 0;
}

#endif
