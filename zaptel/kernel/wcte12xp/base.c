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
 * Copyright (C) 2007-2008, Digium, Inc.
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

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>

#ifdef LINUX26
#include <linux/moduleparam.h>
#endif

#include "zaptel.h"

#include "../wct4xxp/wct4xxp.h"	/* For certain definitions */

#include "../voicebus.h"
#include "wcte12xp.h"

#if defined(VPM_SUPPORT)
#include "vpmadt032.h"
#include "GpakApi.h"
#endif

struct pci_driver te12xp_driver;

static int chanmap_t1[] = 
{ 2,1,0,
  6,5,4,
  10,9,8,
  14,13,12,
  18,17,16,
  22,21,20,
  26,25,24,
  30,29,28 };

static int chanmap_e1[] = 
{ 2,1,0,
  7,6,5,4,
  11,10,9,8,
  15,14,13,12,
  19,18,17,16,
  23,22,21,20,
  27,26,25,24,
  31,30,29,28 };

static int chanmap_e1uc[] =
{ 3,2,1,0,
  7,6,5,4,
  11,10,9,8,
  15,14,13,12,
  19,18,17,16,
  23,22,21,20,
  27,26,25,24,
  31,30,29,28 };

int debug = 0;
static int j1mode = 0;
static int alarmdebounce = 0;
static int loopback = 0;
static int t1e1override = -1;
static int unchannelized = 0;
static int latency = VOICEBUS_DEFAULT_LATENCY;
#ifdef VPM_SUPPORT
int vpmsupport = 1;
int vpmdtmfsupport = 0;
int vpmtsisupport = 0;
int vpmnlptype = 1;
int vpmnlpthresh = 24;
int vpmnlpmaxsupp = 0;
#endif

struct t1 *ifaces[WC_MAX_IFACES];
spinlock_t ifacelock = SPIN_LOCK_UNLOCKED;

struct t1_desc {
	char *name;
	int flags;
};

static struct t1_desc te120p = { "Wildcard TE120P", 0 };
static struct t1_desc te122 = { "Wildcard TE122", 0 };
static struct t1_desc te121 = { "Wildcard TE121", 0 };

int schluffen(wait_queue_head_t *q)
{
	DECLARE_WAITQUEUE(wait, current);
	add_wait_queue(q, &wait);
	current->state = TASK_INTERRUPTIBLE;
	if (!signal_pending(current)) schedule();
	current->state = TASK_RUNNING;
	remove_wait_queue(q, &wait);
	if (signal_pending(current)) return -ERESTARTSYS;
	return(0);
}

static inline int empty_slot(struct t1 *wc)
{
	unsigned int x;

	for (x = 0; x < sizeof(wc->cmdq.cmds) / sizeof(wc->cmdq.cmds[0]); x++) {
		if (!wc->cmdq.cmds[x].flags && !wc->cmdq.cmds[x].address)
			return x;
	}
	return -1;
}

static inline void cmd_dequeue(struct t1 *wc, volatile unsigned char *writechunk, int eframe, int slot)
{
	struct command *curcmd=NULL;
	unsigned int x;

	/* Skip audio */
	writechunk += 66;
	/* Search for something waiting to transmit */
	if ((slot < 6) && (eframe) && (eframe < ZT_CHUNKSIZE - 1)) { 
		/* only 6 useable cs slots per */

		/* framer */
		for (x = 0; x < sizeof(wc->cmdq.cmds) / sizeof(wc->cmdq.cmds[0]); x++) {
			if ((wc->cmdq.cmds[x].flags & (__CMD_RD | __CMD_WR | __CMD_LEDS | __CMD_PINS)) && 
			    !(wc->cmdq.cmds[x].flags & (__CMD_TX | __CMD_FIN))) {
			   	curcmd = &wc->cmdq.cmds[x];
				wc->cmdq.cmds[x].flags |= __CMD_TX;
				wc->cmdq.cmds[x].ident = wc->txident;
				break;
			}
		}
		if (!curcmd) {
			curcmd = &wc->dummy;
			/* If nothing else, use filler */
			curcmd->address = 0x4a;
			curcmd->data = 0x00;
			curcmd->flags = __CMD_RD;
		}
		curcmd->cs_slot = slot; 
		if (curcmd->flags & __CMD_WR)
			writechunk[CMD_BYTE(slot,0,0)] = 0x0c; /* 0c write command */
		else if (curcmd->flags & __CMD_LEDS)
			writechunk[CMD_BYTE(slot,0,0)] = 0x10 | ((curcmd->address) & 0x0E); /* led set command */ 
		else if (curcmd->flags & __CMD_PINS)
			writechunk[CMD_BYTE(slot,0,0)] = 0x30; /* CPLD2 pin state */
		else
			writechunk[CMD_BYTE(slot,0,0)] = 0x0a; /* read command */ 
		writechunk[CMD_BYTE(slot,1,0)] = curcmd->address;
		writechunk[CMD_BYTE(slot,2,0)] = curcmd->data;
	} 

}

static inline void cmd_decipher(struct t1 *wc, volatile unsigned char *readchunk)
{
	unsigned char ident, cs_slot;
	unsigned int x;
	unsigned int is_vpm = 0;

	/* Skip audio */
	readchunk += 66;
	/* Search for any pending results */
	for (x = 0; x < sizeof(wc->cmdq.cmds) / sizeof(wc->cmdq.cmds[0]); x++) {
		if ((wc->cmdq.cmds[x].flags & (__CMD_RD | __CMD_WR | __CMD_LEDS | __CMD_PINS)) && 
		    (wc->cmdq.cmds[x].flags & (__CMD_TX)) && 
		    !(wc->cmdq.cmds[x].flags & (__CMD_FIN))) {
		   	ident = wc->cmdq.cmds[x].ident;
		   	cs_slot = wc->cmdq.cmds[x].cs_slot;

		   	if (ident == wc->rxident) {
				/* Store result */
				wc->cmdq.cmds[x].data |= readchunk[CMD_BYTE(cs_slot,2,is_vpm)];
				/*printk("answer in rxident=%d cs_slot=%d is %d CMD_BYTE=%d jiffies=%d\n", ident, cs_slot, last_read_command, CMD_BYTE(cs_slot, 2), jiffies); */
				wc->cmdq.cmds[x].flags |= __CMD_FIN;
				if (wc->cmdq.cmds[x].flags & (__CMD_WR | __CMD_LEDS))
					/* clear out writes (and leds) since they need no ack */
					memset(&wc->cmdq.cmds[x], 0, sizeof(wc->cmdq.cmds[x]));
			}
		}
	}
}

static inline int t1_setreg_full(struct t1 *wc, int addr, int val, int inisr, int vpm_num)
{
	unsigned long flags;
	int hit;
	int ret;


	do {
		if (!inisr)
			spin_lock_irqsave(&wc->reglock, flags);
		hit = empty_slot(wc);
		if (hit > -1) {
			wc->cmdq.cmds[hit].address = addr;
			wc->cmdq.cmds[hit].data = val;
			wc->cmdq.cmds[hit].flags |= __CMD_WR;
			if(vpm_num >= 0) {
				wc->cmdq.cmds[hit].flags |= __CMD_VPM;
				wc->cmdq.cmds[hit].vpm_num = vpm_num;
			}
		}
		if (inisr)
			break;
	
		spin_unlock_irqrestore(&wc->reglock, flags);
		if (hit < 0) {
			if ((ret = schluffen(&wc->regq)))
				return ret;
		}
	} while (hit < 0);

	return (hit > -1) ? 0 : -1;
}

static inline int t1_setreg(struct t1 *wc, int addr, int val)
{
	return t1_setreg_full(wc, addr, val, 0, NOT_VPM);
}

/***************************************************************************
 * clean_leftovers()
 *
 * Check for unconsumed isr register reads and clean them up.
 **************************************************************************/
static inline void clean_leftovers(struct t1 *wc)
{
	unsigned int x;
	int count = 0;
	
	/* find our requested command */
	for (x = 0; x < sizeof(wc->cmdq.cmds) / sizeof(wc->cmdq.cmds[0]); x++) {
		if ((wc->cmdq.cmds[x].flags & __CMD_RD) &&
		    (wc->cmdq.cmds[x].flags & __CMD_ISR) &&
		    !(wc->cmdq.cmds[x].flags & __CMD_FIN)) {
			debug_printk(1,"leftover isr read! %d", count);
			memset(&wc->cmdq.cmds[x], 0, sizeof(wc->cmdq.cmds[x]));
		}
	}
}

/********************************************************************
 * t1_getreg_isr()
 *
 * Called in interrupt context to retrieve a value already requested
 * by the normal t1_getreg().
 *******************************************************************/
static inline int t1_getreg_isr(struct t1 *wc, int addr)
{
	int hit=-1;
	int ret;
	unsigned int x;
	
	/* find our requested command */
	for (x = 0;x < sizeof(wc->cmdq.cmds) / sizeof(wc->cmdq.cmds[0]); x++) {
		if ((wc->cmdq.cmds[x].flags & __CMD_RD) &&
		    (wc->cmdq.cmds[x].address==addr)) 
		{
			if (wc->cmdq.cmds[x].flags & __CMD_FIN) {
				hit = x;
				break;
			}
			else {
				/* still in progress. */
				return -1;
			}
		}
	}

	if (hit < 0) {
		debug_printk(2, "t1_getreg_isr() no addr=%02x\n", addr);
		return -1; /* oops, couldn't find it */
	}

	ret = wc->cmdq.cmds[hit].data;
	memset(&wc->cmdq.cmds[hit], 0, sizeof(struct command));

	return ret;
}

static inline int t1_getreg_full(struct t1 *wc, int addr, int inisr, int vpm_num)
{
	unsigned long flags;
	int hit;
	int ret = 0;

	do {
		if (!inisr) {
			spin_lock_irqsave(&wc->reglock, flags);
		}
		hit = empty_slot(wc);
		if (hit > -1) {
			wc->cmdq.cmds[hit].address = addr;
			wc->cmdq.cmds[hit].data = 0x00;
			wc->cmdq.cmds[hit].flags |= __CMD_RD;
			if(vpm_num >= 0) {
				wc->cmdq.cmds[hit].flags |= __CMD_VPM;
				wc->cmdq.cmds[hit].vpm_num = vpm_num;
			}
			if (inisr)
				wc->cmdq.cmds[hit].flags |= __CMD_ISR;
		}
		if (inisr) /* must be requested in t1_getreg_isr() */
			return (hit > -1) ? 0 : -1;
		else {
			spin_unlock_irqrestore(&wc->reglock, flags);
		}
		if (hit < 0) {
			if ((ret = schluffen(&wc->regq)))
				return ret;
		}
	} while (hit < 0);

	do {
		spin_lock_irqsave(&wc->reglock, flags);
		if (wc->cmdq.cmds[hit].flags & __CMD_FIN) {
			ret = wc->cmdq.cmds[hit].data;
			memset(&wc->cmdq.cmds[hit], 0, sizeof(wc->cmdq.cmds[hit]));
			hit = -1;
		}
		spin_unlock_irqrestore(&wc->reglock, flags);
		if (hit > -1) {
			if ((ret = schluffen(&wc->regq)))
				return ret;
		}
	} while (hit > -1);

	return ret;
}

static inline int t1_getreg(struct t1 *wc, int addr, int inisr)
{
	return t1_getreg_full(wc, addr, inisr, NOT_VPM);
}

static inline int t1_setleds(struct t1 *wc, int leds, int inisr)
{
	unsigned long flags;
	int hit;
	int ret = 0;

	leds = ~leds & 0x0E; /* invert the LED bits (3 downto 1)*/

	do {
		if (!inisr) {
			spin_lock_irqsave(&wc->reglock, flags);
		}
		hit = empty_slot(wc);
		if (hit > -1) {
			wc->cmdq.cmds[hit].flags |= __CMD_LEDS;
			wc->cmdq.cmds[hit].address = leds;
		}
		if (inisr) {
			break;
		} else {
			spin_unlock_irqrestore(&wc->reglock, flags);
		}
		if (hit < 0) {
			if ((ret = schluffen(&wc->regq)))
				return ret;
		}
	} while (hit < 0);

	return (hit > -1) ? 0 : -1;
}

static inline int t1_getpins(struct t1 *wc, int inisr)
{
	unsigned long flags;
	int hit;
	int ret = 0;

	do {
		spin_lock_irqsave(&wc->reglock, flags);
		hit = empty_slot(wc);
		if (hit > -1) {
			wc->cmdq.cmds[hit].address = 0x00;
			wc->cmdq.cmds[hit].data = 0x00;
			wc->cmdq.cmds[hit].flags |= __CMD_PINS;
		}
		spin_unlock_irqrestore(&wc->reglock, flags);
		if (inisr)
			return (hit > -1) ? 0 : -1;
		if (hit < 0) {
			if ((ret = schluffen(&wc->regq)))
				return ret;
		}
	} while (hit < 0);

	do {
		spin_lock_irqsave(&wc->reglock, flags);
		if (wc->cmdq.cmds[hit].flags & __CMD_FIN) {
			ret = wc->cmdq.cmds[hit].data;
			memset(&wc->cmdq.cmds[hit], 0, sizeof(wc->cmdq.cmds[hit]));
			hit = -1;
		}
		spin_unlock_irqrestore(&wc->reglock, flags);
		if (hit > -1) {
			if ((ret = schluffen(&wc->regq)))
				return ret;
		}
	} while (hit > -1);

	return ret;
}

static void __t1xxp_set_clear(struct t1 *wc, int channo)
{
	int i,j;
	int ret;
	unsigned short val=0;
	
	for (i = 0; i < 24; i++) {
		j = (i / 8);
		if (wc->span.chans[i].flags & ZT_FLAG_CLEAR) 
			val |= 1 << (7 - (i % 8));
		if (((i % 8)==7) &&  /* write byte every 8 channels */
		    ((channo < 0) ||    /* channo=-1 means all channels */ 
		     (j == (channo-1)/8) )) { /* only the register for this channo */    
			ret = t1_setreg_full(wc, 0x2f + j, val, 1, NOT_VPM);
			if (ret < 0)
				module_printk("set_clear failed for chan %d!\n",i); 
			val = 0;
		}
	}
}

static void t1_release(struct t1 *wc)
{
	zt_unregister(&wc->span);
	kfree(wc);
	printk("Freed a Wildcard TE12xP\n");
}

static void t4_serial_setup(struct t1 *wc)
{
	module_printk("Setting up global serial parameters for %s\n", 
		      wc->spantype == TYPE_E1 ? (unchannelized ? "Unchannelized E1" : "E1") : "T1");

	t1_setreg(wc, 0x85, 0xe0);	/* GPC1: Multiplex mode enabled, FSC is output, active low, RCLK from channel 0 */
	t1_setreg(wc, 0x08, 0x05);	/* IPC: Interrupt push/pull active low */

	/* Global clocks (8.192 Mhz CLK) */
	t1_setreg(wc, 0x92, 0x00);	
	t1_setreg(wc, 0x93, 0x18);
	t1_setreg(wc, 0x94, 0xfb);
	t1_setreg(wc, 0x95, 0x0b);
	t1_setreg(wc, 0x96, 0x00);
	t1_setreg(wc, 0x97, 0x0b);
	t1_setreg(wc, 0x98, 0xdb);
	t1_setreg(wc, 0x99, 0xdf);

	/* Configure interrupts */	
	t1_setreg(wc, 0x46, 0xc0);	/* GCR: Interrupt on Activation/Deactivation of AIX, LOS */

	/* Configure system interface */
	t1_setreg(wc, 0x3e, 0x0a /* 0x02 */);	/* SIC1: 4.096 Mhz clock/bus, double buffer receive / transmit, byte interleaved */
	t1_setreg(wc, 0x3f, 0x00); 	/* SIC2: No FFS, no center receive eliastic buffer, phase 0 */
	t1_setreg(wc, 0x40, 0x04);	/* SIC3: Edges for capture */
	t1_setreg(wc, 0x44, 0x30);	/* CMR1: RCLK is at 8.192 Mhz dejittered */
	t1_setreg(wc, 0x45, 0x00);	/* CMR2: We provide sync and clock for tx and rx. */
	t1_setreg(wc, 0x22, 0x00);	/* XC0: Normal operation of Sa-bits */
	t1_setreg(wc, 0x23, 0x04);	/* XC1: 0 offset */
	t1_setreg(wc, 0x24, 0x00);	/* RC0: Just shy of 255 */
	t1_setreg(wc, 0x25, 0x05);	/* RC1: The rest of RC0 */
	
	/* Configure ports */
	t1_setreg(wc, 0x80, 0x00);	/* PC1: SPYR/SPYX input on RPA/XPA */
	t1_setreg(wc, 0x81, 0x22);	/* PC2: RMFB/XSIG output/input on RPB/XPB */
	t1_setreg(wc, 0x82, 0x65);	/* PC3: Some unused stuff */
	t1_setreg(wc, 0x83, 0x35);	/* PC4: Some more unused stuff */
	t1_setreg(wc, 0x84, 0x31);	/* PC5: XMFS active low, SCLKR is input, RCLK is output */
	t1_setreg(wc, 0x86, 0x03);	/* PC6: CLK1 is Tx Clock output, CLK2 is 8.192 Mhz from DCO-R */
	t1_setreg(wc, 0x3b, 0x00);	/* Clear LCR1 */
}

static void t1_configure_t1(struct t1 *wc, int lineconfig, int txlevel)
{
	unsigned int fmr4, fmr2, fmr1, fmr0, lim2;
	char *framing, *line;
	int mytxlevel;

	if ((txlevel > 7) || (txlevel < 4))
		mytxlevel = 0;
	else
		mytxlevel = txlevel - 4;
	fmr1 = 0x9e; /* FMR1: Mode 0, T1 mode, CRC on for ESF, 2.048 Mhz system data rate, no XAIS */
	fmr2 = 0x22; /* FMR2: no payload loopback, auto send yellow alarm */
	if (loopback)
		fmr2 |= 0x4;

	if (j1mode)
		fmr4 = 0x1c;
	else
		fmr4 = 0x0c; /* FMR4: Lose sync on 2 out of 5 framing bits, auto resync */

	lim2 = 0x21; /* LIM2: 50% peak is a "1", Advanced Loss recovery */
	lim2 |= (mytxlevel << 6);	/* LIM2: Add line buildout */
	t1_setreg(wc, 0x1d, fmr1);
	t1_setreg(wc, 0x1e, fmr2);

	/* Configure line interface */
	if (lineconfig & ZT_CONFIG_AMI) {
		line = "AMI";
		fmr0 = 0xa0;
	} else {
		line = "B8ZS";
		fmr0 = 0xf0;
	}
	if (lineconfig & ZT_CONFIG_D4) {
		framing = "D4";
	} else {
		framing = "ESF";
		fmr4 |= 0x2;
		fmr2 |= 0xc0;
	}
	t1_setreg(wc, 0x1c, fmr0);

	t1_setreg(wc, 0x20, fmr4);
	t1_setreg(wc, 0x21, 0x40);	/* FMR5: Enable RBS mode */

	t1_setreg(wc, 0x37, 0xf8);	/* LIM1: Clear data in case of LOS, Set receiver threshold (0.5V), No remote loop, no DRS */
	t1_setreg(wc, 0x36, 0x08);	/* LIM0: Enable auto long haul mode, no local loop (must be after LIM1) */

	t1_setreg(wc, 0x02, 0x50);	/* CMDR: Reset the receiver and transmitter line interface */
	t1_setreg(wc, 0x02, 0x00);	/* CMDR: Reset the receiver and transmitter line interface */

	t1_setreg(wc, 0x3a, lim2);	/* LIM2: 50% peak amplitude is a "1" */
	t1_setreg(wc, 0x38, 0x0a);	/* PCD: LOS after 176 consecutive "zeros" */
	t1_setreg(wc, 0x39, 0x15);	/* PCR: 22 "ones" clear LOS */

	if (j1mode)
		t1_setreg(wc, 0x24, 0x80); /* J1 overide */
		
	/* Generate pulse mask for T1 */
	switch (mytxlevel) {
	case 3:
		t1_setreg(wc, 0x26, 0x07);	/* XPM0 */
		t1_setreg(wc, 0x27, 0x01);	/* XPM1 */
		t1_setreg(wc, 0x28, 0x00);	/* XPM2 */
		break;
	case 2:
		t1_setreg(wc, 0x26, 0x8c);	/* XPM0 */
		t1_setreg(wc, 0x27, 0x11);	/* XPM1 */
		t1_setreg(wc, 0x28, 0x01);	/* XPM2 */
		break;
	case 1:
		t1_setreg(wc, 0x26, 0x8c);	/* XPM0 */
		t1_setreg(wc, 0x27, 0x01);	/* XPM1 */
		t1_setreg(wc, 0x28, 0x00);	/* XPM2 */
		break;
	case 0:
	default:
		t1_setreg(wc, 0x26, 0xd7);	/* XPM0 */
		t1_setreg(wc, 0x27, 0x22);	/* XPM1 */
		t1_setreg(wc, 0x28, 0x01);	/* XPM2 */
		break;
	}

	module_printk("Span configured for %s/%s\n", framing, line);
}

static void t1_configure_e1(struct t1 *wc, int lineconfig)
{
	unsigned int fmr2, fmr1, fmr0;
	unsigned int cas = 0;
	char *crc4 = "";
	char *framing, *line;

	fmr1 = 0x46; /* FMR1: E1 mode, Automatic force resync, PCM30 mode, 8.192 Mhz backplane, no XAIS */
	fmr2 = 0x03; /* FMR2: Auto transmit remote alarm, auto loss of multiframe recovery, no payload loopback */
	if (unchannelized)
		fmr2 |= 0x30;
	if (loopback)
		fmr2 |= 0x4;
	if (lineconfig & ZT_CONFIG_CRC4) {
		fmr1 |= 0x08;	/* CRC4 transmit */
		fmr2 |= 0xc0;	/* CRC4 receive */
		crc4 = "/CRC4";
	}
	t1_setreg(wc, 0x1d, fmr1);
	t1_setreg(wc, 0x1e, fmr2);

	/* Configure line interface */
	if (lineconfig & ZT_CONFIG_AMI) {
		line = "AMI";
		fmr0 = 0xa0;
	} else {
		line = "HDB3";
		fmr0 = 0xf0;
	}
	if (lineconfig & ZT_CONFIG_CCS) {
		framing = "CCS";
	} else {
		framing = "CAS";
		cas = 0x40;
	}
	t1_setreg(wc, 0x1c, fmr0);

	if (unchannelized)
		t1_setreg(wc, 0x1f, 0x40);

	t1_setreg(wc, 0x37, 0xf0 /*| 0x6 */ );	/* LIM1: Clear data in case of LOS, Set receiver threshold (0.5V), No remote loop, no DRS */
	t1_setreg(wc, 0x36, 0x08);	/* LIM0: Enable auto long haul mode, no local loop (must be after LIM1) */

	t1_setreg(wc, 0x02, 0x50);	/* CMDR: Reset the receiver and transmitter line interface */
	t1_setreg(wc, 0x02, 0x00);	/* CMDR: Reset the receiver and transmitter line interface */

	/* Condition receive line interface for E1 after reset */
	t1_setreg(wc, 0xbb, 0x17);
	t1_setreg(wc, 0xbc, 0x55);
	t1_setreg(wc, 0xbb, 0x97);
	t1_setreg(wc, 0xbb, 0x11);
	t1_setreg(wc, 0xbc, 0xaa);
	t1_setreg(wc, 0xbb, 0x91);
	t1_setreg(wc, 0xbb, 0x12);
	t1_setreg(wc, 0xbc, 0x55);
	t1_setreg(wc, 0xbb, 0x92);
	t1_setreg(wc, 0xbb, 0x0c);
	t1_setreg(wc, 0xbb, 0x00);
	t1_setreg(wc, 0xbb, 0x8c);
	
	t1_setreg(wc, 0x3a, 0x20);	/* LIM2: 50% peak amplitude is a "1" */
	t1_setreg(wc, 0x38, 0x0a);	/* PCD: LOS after 176 consecutive "zeros" */
	t1_setreg(wc, 0x39, 0x15);	/* PCR: 22 "ones" clear LOS */
	
	t1_setreg(wc, 0x20, 0x9f);	/* XSW: Spare bits all to 1 */
	if (unchannelized)
		t1_setreg(wc, 0x21, 0x3c);
	else
		t1_setreg(wc, 0x21, 0x1c|cas);	/* XSP: E-bit set when async. AXS auto, XSIF to 1 */
	
	
	/* Generate pulse mask for E1 */
	t1_setreg(wc, 0x26, 0x54);	/* XPM0 */
	t1_setreg(wc, 0x27, 0x02);	/* XPM1 */
	t1_setreg(wc, 0x28, 0x00);	/* XPM2 */
	module_printk("Span configured for %s/%s%s\n", framing, line, crc4);
}

static void t1xxp_framer_start(struct t1 *wc, struct zt_span *span)
{
	int alreadyrunning = wc->span.flags & ZT_FLAG_RUNNING;
	unsigned long flags;

	if (wc->spantype == TYPE_E1) { /* if this is an E1 card */
		t1_configure_e1(wc, span->lineconfig);
	} else { /* is a T1 card */
		t1_configure_t1(wc, span->lineconfig, span->txlevel);
		__t1xxp_set_clear(wc, -1);
	}

	spin_lock_irqsave(&wc->reglock, flags);
	if (!alreadyrunning) 
		wc->span.flags |= ZT_FLAG_RUNNING;
	spin_unlock_irqrestore(&wc->reglock, flags);
}

static int t1xxp_startup(struct zt_span *span)
{
	struct t1 *wc = span->pvt;
	int i;

	/* initialize the start value for the entire chunk of last ec buffer */
	for (i = 0; i < span->channels; i++) {
		memset(wc->ec_chunk1[i], ZT_LIN2X(0, &span->chans[i]), ZT_CHUNKSIZE);
		memset(wc->ec_chunk2[i], ZT_LIN2X(0, &span->chans[i]), ZT_CHUNKSIZE);
	}

	/* Reset framer with proper parameters and start */
	t1xxp_framer_start(wc, span);
	debug_printk(1, "Calling startup (flags is %d)\n", span->flags);

	return 0;
}

static int t1xxp_shutdown(struct zt_span *span)
{
	struct t1 *wc = span->pvt;
	unsigned long flags;

	t1_setreg(wc, 0x46, 0x41);	/* GCR: Interrupt on Activation/Deactivation of AIX, LOS */
	spin_lock_irqsave(&wc->reglock, flags);
	span->flags &= ~ZT_FLAG_RUNNING;
	spin_unlock_irqrestore(&wc->reglock, flags);
	return 0;
}

static int t1xxp_chanconfig(struct zt_chan *chan, int sigtype)
{
	struct t1 *wc = chan->pvt;
	int alreadyrunning = chan->span->flags & ZT_FLAG_RUNNING;

	if (alreadyrunning && (wc->spantype != TYPE_E1))
		__t1xxp_set_clear(wc, chan->channo);

	return 0;
}

static int t1xxp_spanconfig(struct zt_span *span, struct zt_lineconfig *lc)
{
	struct t1 *wc = span->pvt;

	/* Do we want to SYNC on receive or not */
	wc->sync = lc->sync;
	if (wc->sync)
		wc->ctlreg |= 0x80;
	else
		wc->ctlreg &= ~0x80;

	/* If already running, apply changes immediately */
	if (span->flags & ZT_FLAG_RUNNING)
		return t1xxp_startup(span);

	return 0;
}

static int t1xxp_rbsbits(struct zt_chan *chan, int bits)
{
	u_char m,c;
	int n,b;
	struct t1 *wc = chan->pvt;
	unsigned long flags;
	
	debug_printk(2, "Setting bits to %d on channel %s\n", bits, chan->name);
	if (wc->spantype == TYPE_E1) { /* do it E1 way */
		if (chan->chanpos == 16)
			return 0;

		n = chan->chanpos - 1;
		if (chan->chanpos > 15) n--;
		b = (n % 15);
		spin_lock_irqsave(&wc->reglock, flags);	
		c = wc->txsigs[b];
		m = (n / 15) << 2; /* nibble selector */
		c &= (0xf << m); /* keep the other nibble */
		c |= (bits & 0xf) << (4 - m); /* put our new nibble here */
		wc->txsigs[b] = c;
		spin_unlock_irqrestore(&wc->reglock, flags);
		  /* output them to the chip */
		t1_setreg_full(wc,0x71 + b,c,1,NOT_VPM); 
	} else if (wc->span.lineconfig & ZT_CONFIG_D4) {
		n = chan->chanpos - 1;
		b = (n / 4);
		spin_lock_irqsave(&wc->reglock, flags);	
		c = wc->txsigs[b];
		m = ((3 - (n % 4)) << 1); /* nibble selector */
		c &= ~(0x3 << m); /* keep the other nibble */
		c |= ((bits >> 2) & 0x3) << m; /* put our new nibble here */
		wc->txsigs[b] = c;
		spin_unlock_irqrestore(&wc->reglock, flags);
		/* output them to the chip */
		t1_setreg_full(wc,0x70 + b,c,1,NOT_VPM); 
		t1_setreg_full(wc,0x70 + b + 6,c,1,NOT_VPM); 
	} else if (wc->span.lineconfig & ZT_CONFIG_ESF) {
		n = chan->chanpos - 1;
		b = (n / 2);
		spin_lock_irqsave(&wc->reglock, flags);	
		c = wc->txsigs[b];
		m = ((n % 2) << 2); /* nibble selector */
		c &= (0xf << m); /* keep the other nibble */
		c |= (bits & 0xf) << (4 - m); /* put our new nibble here */
		wc->txsigs[b] = c;
		spin_unlock_irqrestore(&wc->reglock, flags);
		  /* output them to the chip */
		t1_setreg_full(wc,0x70 + b,c,1,NOT_VPM); 
	} 
	debug_printk(2,"Finished setting RBS bits\n");

	return 0;
}

static inline void __t1_check_sigbits_reads(struct t1 *wc)
{
	int i;

	if (!(wc->span.flags & ZT_FLAG_RUNNING))
		return;
	if (wc->spantype == TYPE_E1) {
		for (i = 0; i < 15; i++) {
			if (t1_getreg(wc, 0x71 + i, 1))
				wc->isrreaderrors++;
		}
	} else if (wc->span.lineconfig & ZT_CONFIG_D4) {
		for (i = 0; i < 24; i+=4) {
			if (t1_getreg(wc, 0x70 + (i >> 2), 1))
				wc->isrreaderrors++;
		}
	} else {
		for (i = 0; i < 24; i+=2) {
			if (t1_getreg(wc, 0x70 + (i >> 1), 1))
				wc->isrreaderrors++;
		}
	}
}

static inline void __t1_check_sigbits(struct t1 *wc)
{
	int a,i,rxs;

	if (!(wc->span.flags & ZT_FLAG_RUNNING))
		return;
	if (wc->spantype == TYPE_E1) {
		for (i = 0; i < 15; i++) {
			a = t1_getreg_isr(wc, 0x71 + i);
			if (a > -1) {
				/* Get high channel in low bits */
				rxs = (a & 0xf);
				if (!(wc->span.chans[i+16].sig & ZT_SIG_CLEAR)) {
					if (wc->span.chans[i+16].rxsig != rxs) {
						spin_unlock(&wc->reglock);
						zt_rbsbits(&wc->span.chans[i+16], rxs);
						spin_lock(&wc->reglock);
					}
				}
				rxs = (a >> 4) & 0xf;
				if (!(wc->span.chans[i].sig & ZT_SIG_CLEAR)) {
					if (wc->span.chans[i].rxsig != rxs) {
						spin_unlock(&wc->reglock);
						zt_rbsbits(&wc->span.chans[i], rxs);
						spin_lock(&wc->reglock);
					}
				}
			}
		}
	} else if (wc->span.lineconfig & ZT_CONFIG_D4) {
		for (i = 0; i < 24; i+=4) {
			a = t1_getreg_isr(wc, 0x70 + (i>>2));
			if (a > -1) {
				/* Get high channel in low bits */
				rxs = (a & 0x3) << 2;
				if (!(wc->span.chans[i+3].sig & ZT_SIG_CLEAR)) {
					if (wc->span.chans[i+3].rxsig != rxs) {
						spin_unlock(&wc->reglock);
						zt_rbsbits(&wc->span.chans[i+3], rxs);
						spin_lock(&wc->reglock);
					}
				}
				rxs = (a & 0xc);
				if (!(wc->span.chans[i+2].sig & ZT_SIG_CLEAR)) {
					if (wc->span.chans[i+2].rxsig != rxs) {
						spin_unlock(&wc->reglock);
						zt_rbsbits(&wc->span.chans[i+2], rxs);
						spin_lock(&wc->reglock);
					}
				}
				rxs = (a >> 2) & 0xc;
				if (!(wc->span.chans[i+1].sig & ZT_SIG_CLEAR)) {
					if (wc->span.chans[i+1].rxsig != rxs) {
						spin_unlock(&wc->reglock);
						zt_rbsbits(&wc->span.chans[i+1], rxs);
						spin_lock(&wc->reglock);
					}
				}
				rxs = (a >> 4) & 0xc;
				if (!(wc->span.chans[i].sig & ZT_SIG_CLEAR)) {
					if (wc->span.chans[i].rxsig != rxs) {
						spin_unlock(&wc->reglock);
						zt_rbsbits(&wc->span.chans[i], rxs);
						spin_lock(&wc->reglock);
					}	
				}
			}
		}
	} else {
		for (i = 0; i < 24; i+=2) {
			a = t1_getreg_isr(wc, 0x70 + (i>>1));
			if (a > -1) {
				/* Get high channel in low bits */
				rxs = (a & 0xf);
				if (!(wc->span.chans[i+1].sig & ZT_SIG_CLEAR)) {
					if (wc->span.chans[i+1].rxsig != rxs) {
						spin_unlock(&wc->reglock);
						zt_rbsbits(&wc->span.chans[i+1], rxs);
						spin_lock(&wc->reglock);
					}
				}
				rxs = (a >> 4) & 0xf;
				if (!(wc->span.chans[i].sig & ZT_SIG_CLEAR)) {
					if (wc->span.chans[i].rxsig != rxs) {
						spin_unlock(&wc->reglock);
						zt_rbsbits(&wc->span.chans[i], rxs);
						spin_lock(&wc->reglock);
					}
				}
			}
		}
	}
}

static int t1xxp_maint(struct zt_span *span, int cmd)
{
	struct t1 *wc = span->pvt;

	if (wc->spantype == TYPE_E1) {
		switch (cmd) {
		case ZT_MAINT_NONE:
			module_printk("XXX Turn off local and remote loops E1 XXX\n");
			break;
		case ZT_MAINT_LOCALLOOP:
			module_printk("XXX Turn on local loopback E1 XXX\n");
			break;
		case ZT_MAINT_REMOTELOOP:
			module_printk("XXX Turn on remote loopback E1 XXX\n");
			break;
		case ZT_MAINT_LOOPUP:
			module_printk("XXX Send loopup code E1 XXX\n");
			break;
		case ZT_MAINT_LOOPDOWN:
			module_printk("XXX Send loopdown code E1 XXX\n");
			break;
		case ZT_MAINT_LOOPSTOP:
			module_printk("XXX Stop sending loop codes E1 XXX\n");
			break;
		default:
			module_printk("Unknown E1 maint command: %d\n", cmd);
			break;
		}
	} else {
		switch (cmd) {
		case ZT_MAINT_NONE:
			module_printk("XXX Turn off local and remote loops T1 XXX\n");
			break;
		case ZT_MAINT_LOCALLOOP:
			module_printk("XXX Turn on local loop and no remote loop XXX\n");
			break;
		case ZT_MAINT_REMOTELOOP:
			module_printk("XXX Turn on remote loopup XXX\n");
			break;
		case ZT_MAINT_LOOPUP:
			t1_setreg(wc, 0x21, 0x50);	/* FMR5: Nothing but RBS mode */
			break;
		case ZT_MAINT_LOOPDOWN:
			t1_setreg(wc, 0x21, 0x60);	/* FMR5: Nothing but RBS mode */
			break;
		case ZT_MAINT_LOOPSTOP:
			t1_setreg(wc, 0x21, 0x40);	/* FMR5: Nothing but RBS mode */
			break;
		default:
			module_printk("Unknown T1 maint command: %d\n", cmd);
			break;
		}
	}

	return 0;
}

static int t1xxp_open(struct zt_chan *chan)
{
	struct t1 *wc = chan->pvt;

	if (wc->dead)
		return -ENODEV;
	wc->usecount++;

#ifndef LINUX26	
	MOD_INC_USE_COUNT;
#else
	try_module_get(THIS_MODULE);
#endif	

	return 0;
}

static int t1xxp_close(struct zt_chan *chan)
{
	struct t1 *wc = chan->pvt;

	wc->usecount--;

#ifndef LINUX26	
	MOD_DEC_USE_COUNT;
#else
	module_put(THIS_MODULE);
#endif

	/* If we're dead, release us now */
	if (!wc->usecount && wc->dead) 
		t1_release(wc);

	return 0;
}

static int t1xxp_ioctl(struct zt_chan *chan, unsigned int cmd, unsigned long data)
{
	unsigned int x;
	struct t1 *wc = chan->pvt;

	switch (cmd) {
	case WCT4_GET_REGS:
		/* Since all register access was moved into the voicebus
		 * module....this was removed.  Although...why does the client
		 * library need access to the registers (debugging)? \todo ..
		 */
		WARN_ON(1);
		return -ENOSYS;
		break;
#ifdef VPM_SUPPORT
	case ZT_TONEDETECT:
		if (get_user(x, (int *) data))
			return -EFAULT;
		if (!wc->vpm150m)
			return -ENOSYS;
		if (wc->vpm150m && (x && !vpmdtmfsupport))
			return -ENOSYS;
		if (x & ZT_TONEDETECT_ON) {
			set_bit(chan->chanpos - 1, &wc->dtmfmask);
			module_printk("turning on tone detection\n");
		} else {
			clear_bit(chan->chanpos - 1, &wc->dtmfmask);
			module_printk("turning off tone detection\n");
		}
		if (x & ZT_TONEDETECT_MUTE) {
			if(wc->vpm150m)
				set_bit(chan->chanpos - 1, &wc->vpm150m->desireddtmfmutestate);
		} else {
			if(wc->vpm150m)
				clear_bit(chan->chanpos - 1, &wc->vpm150m->desireddtmfmutestate);
		}
		return 0;
#endif
	default:
		return -ENOTTY;
	}
	return 0;
}

#ifdef VPM_SUPPORT

#include "adt_lec.c"

static int t1xxp_echocan_with_params(struct zt_chan *chan, struct zt_echocanparams *ecp, struct zt_echocanparam *p)
{
	struct adt_lec_params params;
	struct t1 *wc = chan->pvt;
	struct vpm150m *vpm150m = wc->vpm150m;
	unsigned long flags;
	struct vpm150m_workentry *work;
	unsigned int ret;

	if (!wc->vpm150m)
		return -ENODEV;

	adt_lec_init_defaults(&params, 32);

	if ((ret = adt_lec_parse_params(&params, ecp, p)))
		return ret;

	/* we can't really control the tap length, but the value is used
	   to control whether the ec is on or off, so translate it */
	params.tap_length = ecp->tap_length ? 1 : 0;

	if (!(work = kmalloc(sizeof(*work), GFP_KERNEL)))
		return -ENOMEM;

	work->params = params;
	work->wc = wc;
	work->chan = chan; 
	spin_lock_irqsave(&vpm150m->lock, flags);
	list_add_tail(&work->list, &vpm150m->worklist);
	spin_unlock_irqrestore(&vpm150m->lock, flags);
	
	/* we must do this later since we cannot sleep in the echocan function */
	if (test_bit(VPM150M_ACTIVE, &vpm150m->control))
		queue_work(vpm150m->wq, &vpm150m->work_echocan);

	return 0; /* how do I return the status since it is done later by the workqueue? */
}
#endif

static int t1_software_init(struct t1 *wc)
{
	int x;
	struct pci_dev* dev;

	dev = voicebus_get_pci_dev(wc->vb);

	/* Find position */
	for (x = 0; x < sizeof(ifaces) / sizeof(ifaces[0]); x++) {
		if (ifaces[x] == wc) {
			debug_printk(1, "software init for card %d\n",x);
			break;
		}
	}

	if (x == sizeof(ifaces) / sizeof(ifaces[0]))
		return -1;

	t4_serial_setup(wc);

	wc->num = x;
	sprintf(wc->span.name, "WCT1/%d", wc->num);
	snprintf(wc->span.desc, sizeof(wc->span.desc) - 1, "%s Card %d", wc->variety, wc->num);
	wc->span.manufacturer = "Digium";
	strncpy(wc->span.devicetype, wc->variety, sizeof(wc->span.devicetype) - 1);

#if defined(VPM_SUPPORT)
	if (wc->vpm150m)
		strncat(wc->span.devicetype, " with VPMADT032", sizeof(wc->span.devicetype) - 1);
#endif

	snprintf(wc->span.location, sizeof(wc->span.location) - 1,
		"PCI Bus %02d Slot %02d", dev->bus->number, PCI_SLOT(dev->devfn) + 1);

	wc->span.spanconfig = t1xxp_spanconfig;
	wc->span.chanconfig = t1xxp_chanconfig;
	wc->span.irq = dev->irq;
	wc->span.startup = t1xxp_startup;
	wc->span.shutdown = t1xxp_shutdown;
	wc->span.rbsbits = t1xxp_rbsbits;
	wc->span.maint = t1xxp_maint;
	wc->span.open = t1xxp_open;
	wc->span.close = t1xxp_close;
	wc->span.ioctl = t1xxp_ioctl;
#ifdef VPM_SUPPORT
	wc->span.echocan_with_params = t1xxp_echocan_with_params;
#endif

	if (wc->spantype == TYPE_E1) {
		if (unchannelized)
			wc->span.channels = 32;
		else
			wc->span.channels = 31;
		wc->span.spantype = "E1";
		wc->span.linecompat = ZT_CONFIG_HDB3 | ZT_CONFIG_CCS | ZT_CONFIG_CRC4;
		wc->span.deflaw = ZT_LAW_ALAW;
	} else {
		wc->span.channels = 24;
		wc->span.spantype = "T1";
		wc->span.linecompat = ZT_CONFIG_AMI | ZT_CONFIG_B8ZS | ZT_CONFIG_D4 | ZT_CONFIG_ESF;
		wc->span.deflaw = ZT_LAW_MULAW;
	}
	wc->span.chans = wc->chans;
	wc->span.flags = ZT_FLAG_RBS;
	wc->span.pvt = wc;
	init_waitqueue_head(&wc->span.maintq);
	for (x = 0; x < wc->span.channels; x++) {
		sprintf(wc->chans[x].name, "WCT1/%d/%d", wc->num, x + 1);
		wc->chans[x].sigcap = ZT_SIG_EM | ZT_SIG_CLEAR | ZT_SIG_EM_E1 | 
				      ZT_SIG_FXSLS | ZT_SIG_FXSGS | ZT_SIG_MTP2 |
				      ZT_SIG_FXSKS | ZT_SIG_FXOLS | ZT_SIG_DACS_RBS |
				      ZT_SIG_FXOGS | ZT_SIG_FXOKS | ZT_SIG_CAS | ZT_SIG_SF;
		wc->chans[x].pvt = wc;
		wc->chans[x].chanpos = x + 1;
	}
	if (zt_register(&wc->span, 0)) {
		module_printk("Unable to register span with Zaptel\n");
		return -1;
	}
	wc->initialized = 1;

	return 0;
}

#ifdef VPM_SUPPORT
static inline unsigned char t1_vpm_in(struct t1 *wc, int unit, const unsigned int addr) 
{
		return t1_getreg_full(wc, addr, 0, unit);
}

static inline unsigned char t1_vpm_out(struct t1 *wc, int unit, const unsigned int addr, const unsigned char val) 
{
		return t1_setreg_full(wc, addr, val, 0, unit);
}

#endif

static int t1_hardware_post_init(struct t1 *wc)
{
	unsigned int reg;
	int x;

	/* T1 or E1 */
	if (t1e1override > -1) {
		if (t1e1override)
			wc->spantype = TYPE_E1;
		else
			wc->spantype = TYPE_T1;
	} else {
		if (t1_getpins(wc,0) & 0x01) /* returns 1 for T1 mode */
			wc->spantype = TYPE_T1;
		else	
			wc->spantype = TYPE_E1;
	}
	debug_printk(1, "spantype: %s\n", wc->spantype==1 ? "T1" : "E1");
	
	if (wc->spantype == TYPE_E1) {
		if (unchannelized)
			wc->chanmap = chanmap_e1uc;
		else
			wc->chanmap = chanmap_e1;
	} else
		wc->chanmap = chanmap_t1;
	/* what version of the FALC are we using? */
	reg = t1_setreg(wc, 0x4a, 0xaa);
	reg = t1_getreg(wc, 0x4a, 0);
	debug_printk(1, "FALC version: %08x\n", reg);

	/* make sure reads and writes work */
	for (x = 0; x < 256; x++) {
		t1_setreg(wc, 0x14, x);
		if ((reg = t1_getreg(wc, 0x14, 0)) != x)
			module_printk("Wrote '%x' but read '%x'\n", x, reg);
	}

	/* all LED's blank */
	wc->ledtestreg = UNSET_LED_ORANGE(wc->ledtestreg);
	wc->ledtestreg = UNSET_LED_REDGREEN(wc->ledtestreg);
	t1_setleds(wc, wc->ledtestreg, 0);

#ifdef VPM_SUPPORT
	t1_vpm150m_init(wc);
	if (wc->vpm150m) {
		module_printk("VPM present and operational (Firmware version %x)\n", wc->vpm150m->version);
		wc->ctlreg |= 0x10; /* turn on vpm (RX audio from vpm module) */
		if (vpmtsisupport) {
			debug_printk(1, "enabling VPM TSI pin\n");
			wc->ctlreg |= 0x01; /* turn on vpm timeslot interchange pin */
		}
	}
#endif

	return 0;
}

static inline void __t1_check_alarms_reads(struct t1 *wc)
{
	if (!(wc->span.flags & ZT_FLAG_RUNNING))
		return;

	if (t1_getreg(wc, 0x4c, 1))
		wc->isrreaderrors++;
	if (t1_getreg(wc, 0x20, 1))
		wc->isrreaderrors++;
	if (t1_getreg(wc, 0x4d, 1))
		wc->isrreaderrors++;
}

static inline void __t1_check_alarms(struct t1 *wc)
{
	unsigned char c,d;
	int alarms;
	int x,j;
	unsigned char fmr4; /* must read this always */

	if (!(wc->span.flags & ZT_FLAG_RUNNING))
		return;

	c = t1_getreg_isr(wc, 0x4c);
	fmr4 = t1_getreg_isr(wc, 0x20); /* must read this even if we don't use it */
	d = t1_getreg_isr(wc, 0x4d);

	/* Assume no alarms */
	alarms = 0;

	/* And consider only carrier alarms */
	wc->span.alarms &= (ZT_ALARM_RED | ZT_ALARM_BLUE | ZT_ALARM_NOTOPEN);

	if (wc->spantype == TYPE_E1) {
		if (c & 0x04) {
			/* No multiframe found, force RAI high after 400ms only if
			   we haven't found a multiframe since last loss
			   of frame */
			if (!wc->flags.nmf) {
				t1_setreg_full(wc, 0x20, 0x9f | 0x20, 1, NOT_VPM);	/* LIM0: Force RAI High */
				wc->flags.nmf = 1;
				module_printk("NMF workaround on!\n");
			}
			t1_setreg_full(wc, 0x1e, 0xc3, 1, NOT_VPM);	/* Reset to CRC4 mode */
			t1_setreg_full(wc, 0x1c, 0xf2, 1, NOT_VPM);	/* Force Resync */
			t1_setreg_full(wc, 0x1c, 0xf0, 1, NOT_VPM);	/* Force Resync */
		} else if (!(c & 0x02)) {
			if (wc->flags.nmf) {
				t1_setreg_full(wc, 0x20, 0x9f, 1, NOT_VPM);	/* LIM0: Clear forced RAI */
				wc->flags.nmf = 0;
				module_printk("NMF workaround off!\n");
			}
		}
	} else {
		/* Detect loopup code if we're not sending one */
		if ((!wc->span.mainttimer) && (d & 0x08)) {
			/* Loop-up code detected */
			if ((wc->loopupcnt++ > 80)  && (wc->span.maintstat != ZT_MAINT_REMOTELOOP)) {
				t1_setreg_full(wc, 0x36, 0x08, 1, NOT_VPM);	/* LIM0: Disable any local loop */
				t1_setreg_full(wc, 0x37, 0xf6, 1, NOT_VPM);	/* LIM1: Enable remote loop */
				wc->span.maintstat = ZT_MAINT_REMOTELOOP;
			}
		} else
			wc->loopupcnt = 0;
		/* Same for loopdown code */
		if ((!wc->span.mainttimer) && (d & 0x10)) {
			/* Loop-down code detected */
			if ((wc->loopdowncnt++ > 80)  && (wc->span.maintstat == ZT_MAINT_REMOTELOOP)) {
				t1_setreg_full(wc, 0x36, 0x08, 1, NOT_VPM);	/* LIM0: Disable any local loop */
				t1_setreg_full(wc, 0x37, 0xf0, 1, NOT_VPM);	/* LIM1: Disable remote loop */
				wc->span.maintstat = ZT_MAINT_NONE;
			}
		} else
			wc->loopdowncnt = 0;
	}

	if (wc->span.lineconfig & ZT_CONFIG_NOTOPEN) {
		for (x=0,j=0;x < wc->span.channels;x++)
			if ((wc->span.chans[x].flags & ZT_FLAG_OPEN) ||
			    (wc->span.chans[x].flags & ZT_FLAG_NETDEV))
				j++;
		if (!j)
			alarms |= ZT_ALARM_NOTOPEN;
	}

	if (c & 0xa0) {
		if (wc->alarmcount >= alarmdebounce) {
			if (!unchannelized)
				alarms |= ZT_ALARM_RED;
		} else
			wc->alarmcount++;
	} else
		wc->alarmcount = 0;
	if (c & 0x4)
		alarms |= ZT_ALARM_BLUE;

	/* Keep track of recovering */
	if ((!alarms) && wc->span.alarms) 
		wc->alarmtimer = ZT_ALARMSETTLE_TIME;
	if (wc->alarmtimer)
		alarms |= ZT_ALARM_RECOVER;

	/* If receiving alarms, go into Yellow alarm state */
	if (alarms && !wc->flags.sendingyellow) {
		module_printk("Setting yellow alarm\n");

		/* We manually do yellow alarm to handle RECOVER and NOTOPEN, otherwise it's auto anyway */
		t1_setreg_full(wc, 0x20, fmr4 | 0x20, 1, NOT_VPM);
		wc->flags.sendingyellow = 1;
	} else if (!alarms && wc->flags.sendingyellow) {
		module_printk("Clearing yellow alarm\n");
		/* We manually do yellow alarm to handle RECOVER  */
		t1_setreg_full(wc, 0x20, fmr4 & ~0x20, 1, NOT_VPM);
		wc->flags.sendingyellow = 0;
	}
	
	if ((c & 0x10) && !unchannelized)
		alarms |= ZT_ALARM_YELLOW;
	if (wc->span.mainttimer || wc->span.maintstat) 
		alarms |= ZT_ALARM_LOOPBACK;
	wc->span.alarms = alarms;
	spin_unlock(&wc->reglock);
	zt_alarm_notify(&wc->span);
	spin_lock(&wc->reglock);
}

static inline void __handle_leds(struct t1 *wc)
{
	if (wc->span.alarms & (ZT_ALARM_RED | ZT_ALARM_BLUE)) {
		wc->blinktimer++;
		if (wc->blinktimer == 160)
			wc->ledtestreg = SET_LED_RED(wc->ledtestreg); 
		if (wc->blinktimer == 480) {
			wc->ledtestreg = UNSET_LED_REDGREEN(wc->ledtestreg); 
			wc->blinktimer = 0;
		}
	} else if (wc->span.alarms & ZT_ALARM_YELLOW) {
		wc->yellowtimer++;
		if (!(wc->yellowtimer % 2))
			wc->ledtestreg = SET_LED_RED(wc->ledtestreg); 
		else
			wc->ledtestreg = SET_LED_GREEN(wc->ledtestreg); 
	} else {
		if (wc->span.maintstat != ZT_MAINT_NONE)
			wc->ledtestreg = SET_LED_ORANGE(wc->ledtestreg);
		else
			wc->ledtestreg = UNSET_LED_ORANGE(wc->ledtestreg);
		if (wc->span.flags & ZT_FLAG_RUNNING)
			wc->ledtestreg = SET_LED_GREEN(wc->ledtestreg); 
		else
			wc->ledtestreg = UNSET_LED_REDGREEN(wc->ledtestreg); 
	}

	if (wc->ledtestreg != wc->ledlastvalue) {
		t1_setleds(wc, wc->ledtestreg, 1);
		wc->ledlastvalue = wc->ledtestreg;
	}
}


static void __t1_do_counters(struct t1 *wc)
{
	if (wc->alarmtimer) {
		if (!--wc->alarmtimer) {
			wc->span.alarms &= ~(ZT_ALARM_RECOVER);
			zt_alarm_notify(&wc->span);
		}
	}
}

static inline void t1_isr_misc(struct t1 *wc)
{
	const unsigned int x = wc->intcount & 0x3f;
	int buffer_count = voicebus_current_latency(wc->vb);

	if (unlikely(!wc->initialized)) return;
	
	__handle_leds(wc);

	__t1_do_counters(wc);

	if ( 0 == x ) {
		__t1_check_sigbits_reads(wc);
	}
	else if ( 1 == x ) {
		if (!(wc->intcount & 0x30)) {
			__t1_check_alarms_reads(wc);
			wc->alarms_read=1;
		}
	}
	else if ( x == buffer_count*2) {
		__t1_check_sigbits(wc);
	}
	else if ( x == (buffer_count*2)+1 ) {
		if (wc->alarms_read) {
			__t1_check_alarms(wc);
			wc->alarms_read=0;
		}
	}
	else if ( x == (buffer_count*2)+2) {
		clean_leftovers(wc);
	}
}

static inline void t1_transmitprep(struct t1 *wc, unsigned char* writechunk)
{
	int x;
	int y;
	int chan;

	/* Calculate Transmission */
	if (likely(wc->initialized)) {
		spin_unlock(&wc->reglock);
		zt_transmit(&wc->span);
		spin_lock(&wc->reglock);
	}

	for (x = 0; x < ZT_CHUNKSIZE; x++) {
		if (likely(wc->initialized)) {
			for (chan = 0; chan < wc->span.channels; chan++)
				writechunk[(chan+1)*2] = wc->chans[chan].writechunk[x];	
		}

		/* process the command queue */
		for (y = 0; y < 7; y++) {
			cmd_dequeue(wc, writechunk, x, y);
		}
#ifdef VPM_SUPPORT
		if(likely(wc->vpm150m)) {
			vpm150m_cmd_dequeue(wc, writechunk, x);
		}
#endif

		if (x < ZT_CHUNKSIZE - 1) {
			writechunk[EFRAME_SIZE] = wc->ctlreg;
			writechunk[EFRAME_SIZE + 1] = wc->txident++;
		}
		writechunk += (EFRAME_SIZE + EFRAME_GAP);
	}
}

static inline void cmd_retransmit(struct t1 *wc)
{
	unsigned int x;

	for (x = 0; x < sizeof(wc->cmdq.cmds) / sizeof(wc->cmdq.cmds[0]); x++) {
		if (!(wc->cmdq.cmds[x].flags &  __CMD_FIN)) {
			wc->cmdq.cmds[x].flags &= ~(__CMD_TX) ; /* clear __CMD_TX */
			wc->cmdq.cmds[x].ident = 0;
		}
	}
}

static inline void t1_receiveprep(struct t1 *wc, unsigned char* readchunk)
{
	int x,chan;
	unsigned char expected;

	for (x = 0; x < ZT_CHUNKSIZE; x++) {
		if (likely(wc->initialized)) {
			for (chan = 0; chan < wc->span.channels; chan++) {
				wc->chans[chan].readchunk[x]= readchunk[(chan+1)*2];
			}
		}
		if (x < ZT_CHUNKSIZE - 1) {
			expected = wc->rxident+1;
			wc->rxident = readchunk[EFRAME_SIZE + 1];
			wc->statreg = readchunk[EFRAME_SIZE + 2];
			if (wc->rxident != expected) {
				wc->span.irqmisses++;
				cmd_retransmit(wc);
				if (unlikely(debug && wc->initialized))
					module_printk("oops: rxident=%d expected=%d x=%d\n", wc->rxident, expected, x);
			}
		}
		cmd_decipher(wc, readchunk);
#ifdef VPM_SUPPORT
		if(wc->vpm150m)
			vpm150m_cmd_decipher(wc, readchunk);
#endif
		readchunk += (EFRAME_SIZE + EFRAME_GAP);
	}
	
	/* echo cancel */
	if (likely(wc->initialized)) {
		spin_unlock(&wc->reglock);
		for (x = 0; x < wc->span.channels; x++) {
			zt_ec_chunk(&wc->chans[x], wc->chans[x].readchunk, wc->ec_chunk2[x]);
			memcpy(wc->ec_chunk2[x],wc->ec_chunk1[x],ZT_CHUNKSIZE);
			memcpy(wc->ec_chunk1[x],wc->chans[x].writechunk,ZT_CHUNKSIZE);
		}
		zt_receive(&wc->span);
		spin_lock(&wc->reglock);
	}

	/* Wake up anyone sleeping to read/write a new register */
	wake_up_interruptible(&wc->regq);
}

static void
t1_handle_transmit(void* vbb, void* context)
{
	struct t1* wc = context;
	/* Either this function is called from within interrupt context, or
	 * the reglock will never be acquired from interrupt context, so it's
	 * safe to grab it without locking interrupt.
	 */
	memset(vbb, 0, SFRAME_SIZE);
	spin_lock(&wc->reglock);
	wc->txints++;
	t1_transmitprep(wc, vbb);
	wc->intcount++;
	t1_isr_misc(wc);
	spin_unlock(&wc->reglock);
	voicebus_transmit(wc->vb, vbb);
}

static void
t1_handle_receive(void* vbb, void* context)
{
	struct t1* wc = context;
	wc->rxints++;
	/* Either this function is called from within interrupt context, or
	 * the reglock will never be acquired from interrupt context, so it's
	 * safe to grab it without locking interrupt.
	 */
	spin_lock(&wc->reglock);
	t1_receiveprep(wc, vbb);
	spin_unlock(&wc->reglock);
}

static int __devinit te12xp_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct t1 *wc;
	struct t1_desc *d = (struct t1_desc *) ent->driver_data;
	unsigned int x;
	int res;
	int startinglatency;

	for (x = 0; x < sizeof(ifaces) / sizeof(ifaces[0]); x++)
		if (!ifaces[x]) break;

	if (x >= sizeof(ifaces) / sizeof(ifaces[0])) {
		module_printk("Too many interfaces\n");
		return -EIO;
	}
	
retry:
	wc = kmalloc(sizeof(*wc), GFP_KERNEL);
	if (!wc)
		return -ENOMEM;

	ifaces[x] = wc;
	memset(wc, 0, sizeof(*wc));
	spin_lock_init(&wc->reglock);
	wc->variety = d->name;
	wc->txident = 1;

	init_waitqueue_head(&wc->regq);
	snprintf(wc->name, sizeof(wc->name)-1, "wcte12xp%d", x);
	if ((res=voicebus_init(pdev, SFRAME_SIZE, wc->name,
		t1_handle_receive, t1_handle_transmit, wc, &wc->vb))) 
	{
		WARN_ON(1);
		kfree(wc);
		return res;
	}
	
	/* Keep track of which device we are */
	pci_set_drvdata(pdev, wc);
	if (VOICEBUS_DEFAULT_LATENCY != latency) {
		voicebus_set_minlatency(wc->vb, latency);
	}
	voicebus_start(wc->vb);
	startinglatency = voicebus_current_latency(wc->vb);
	t1_hardware_post_init(wc);
	t1_software_init(wc);
	if (voicebus_current_latency(wc->vb) > startinglatency) {
		/* The voicebus library increased the latency during
		 * initialization because the host wasn't able to service the
		 * interrupts from the adapter quickly enough.  In this case,
		 * we'll increase our latency and restart the initialization.
		 */
		printk(KERN_NOTICE "%s: Restarting board initialization " \
		 "after increasing latency.\n", wc->name);
		latency = voicebus_current_latency(wc->vb);
		zt_unregister(&wc->span);
		voicebus_release(wc->vb);
		wc->vb = NULL;
		kfree(wc);
		wc = NULL;
		goto retry;
	}
	module_printk("Found a %s\n", wc->variety);

	return 0;
}

static void __devexit te12xp_remove_one(struct pci_dev *pdev)
{
	struct t1 *wc = pci_get_drvdata(pdev);
#ifdef VPM_SUPPORT
	unsigned long flags;
	struct vpm150m *vpm150m = wc->vpm150m;
#endif
	if (!wc)
		return;

#ifdef VPM_SUPPORT
	if(vpm150m) {
		clear_bit(VPM150M_DTMFDETECT, &vpm150m->control);
		clear_bit(VPM150M_ACTIVE, &vpm150m->control);
		flush_workqueue(vpm150m->wq);
		destroy_workqueue(vpm150m->wq);
	}
#endif

	BUG_ON(!wc->vb);
	voicebus_release(wc->vb);
	wc->vb = NULL;

	if (debug && wc->isrreaderrors)
		debug_printk(1, "isrreaderrors=%d\n", wc->isrreaderrors);

#ifdef VPM_SUPPORT
	if(vpm150m) {
		spin_lock_irqsave(&wc->reglock, flags);
		wc->vpm150m = NULL;
		vpm150m->wc = NULL;
		spin_unlock_irqrestore(&wc->reglock, flags);
		kfree(wc->vpm150m);
	}
#endif
	/* Release span, possibly delayed */
	if (!wc->usecount)
		t1_release(wc);
	else
		wc->dead = 1;
}

static struct pci_device_id te12xp_pci_tbl[] = {
	{ 0xd161, 0x0120, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &te120p},
	{ 0xd161, 0x8000, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &te121},
	{ 0xd161, 0x8001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &te122},
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, te12xp_pci_tbl);

struct pci_driver te12xp_driver = {
	name: 	"wcte12xp",
	probe: 	te12xp_init_one,
#ifdef LINUX26
	remove:	__devexit_p(te12xp_remove_one),
#else
	remove:	te12xp_remove_one,
#endif
	suspend: NULL,
	resume:	NULL,
	id_table: te12xp_pci_tbl,
};

static int __init te12xp_init(void)
{
	int res;

	res = zap_pci_module(&te12xp_driver);

	return res ? -ENODEV : 0;
}


static void __exit te12xp_cleanup(void)
{
	pci_unregister_driver(&te12xp_driver);
}

#ifdef LINUX26
module_param(debug, int, S_IRUGO | S_IWUSR);
module_param(loopback, int, S_IRUGO | S_IWUSR);
module_param(t1e1override, int, S_IRUGO | S_IWUSR);
module_param(j1mode, int, S_IRUGO | S_IWUSR);
module_param(alarmdebounce, int, S_IRUGO | S_IWUSR);
module_param(latency, int, S_IRUGO | S_IWUSR);
#ifdef VPM_SUPPORT
module_param(vpmsupport, int, S_IRUGO | S_IWUSR);
module_param(vpmdtmfsupport, int, S_IRUGO | S_IWUSR);
module_param(vpmtsisupport, int, S_IRUGO | S_IWUSR);
#endif
#else
MODULE_PARM(debug, "i");
MODULE_PARM(loopback, "i");
MODULE_PARM(t1e1override, "i");
MODULE_PARM(j1mode, "i");
MODULE_PARM(alarmdebounce, "i");
#ifdef VPM_SUPPORT
MODULE_PARM(vpmsupport, "i");
MODULE_PARM(vpmdtmfsupport, "i");
MODULE_PARM(vpmtsisupport, "i");
MODULE_PARM(vpmnlptype, "i");
MODULE_PARM(vpmnlpthresh, "i");
MODULE_PARM(vpmnlpmaxsupp, "i");
#endif
#endif

#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

module_init(te12xp_init);
module_exit(te12xp_cleanup);
