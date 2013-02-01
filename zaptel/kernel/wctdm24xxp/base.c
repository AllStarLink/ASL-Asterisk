/*
 * Wildcard TDM2400P TDM FXS/FXO Interface Driver for Zapata Telephony interface
 *
 * Written by Mark Spencer <markster@digium.com>
 * Support for TDM800P and VPM150M by Matthew Fredrickson <creslin@digium.com>
 *
 * Copyright (C) 2005 - 2008 Digium, Inc.
 * All rights reserved.
 *
 * Sections for QRV cards written by Jim Dixon <jim@lambdatel.com>
 * Copyright (C) 2006, Jim Dixon and QRV Communications
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

/* For QRV DRI cards, gain is signed short, expressed in hundredths of
db (in reference to 1v Peak @ 1000Hz) , as follows:

Rx Gain: -11.99 to 15.52 db
Tx Gain - No Pre-Emphasis: -35.99 to 12.00 db
Tx Gain - W/Pre-Emphasis: -23.99 to 0.00 db
*/

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/slab.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
#include <linux/semaphore.h>
#else
#include <asm/semaphore.h>
#endif

#ifdef LINUX26
#include <linux/moduleparam.h>
#endif

#include "zaptel.h"
#include "proslic.h"
#include "wctdm.h"

#include "wctdm24xxp.h"

#ifdef VPM150M_SUPPORT
#include "adt_lec.h"
#endif

#include "GpakCust.h"
#include "GpakApi.h"

/*
  Experimental max loop current limit for the proslic
  Loop current limit is from 20 mA to 41 mA in steps of 3
  (according to datasheet)
  So set the value below to:
  0x00 : 20mA (default)
  0x01 : 23mA
  0x02 : 26mA
  0x03 : 29mA
  0x04 : 32mA
  0x05 : 35mA
  0x06 : 37mA
  0x07 : 41mA
*/
static int loopcurrent = 20;

static alpha  indirect_regs[] =
{
{0,255,"DTMF_ROW_0_PEAK",0x55C2},
{1,255,"DTMF_ROW_1_PEAK",0x51E6},
{2,255,"DTMF_ROW2_PEAK",0x4B85},
{3,255,"DTMF_ROW3_PEAK",0x4937},
{4,255,"DTMF_COL1_PEAK",0x3333},
{5,255,"DTMF_FWD_TWIST",0x0202},
{6,255,"DTMF_RVS_TWIST",0x0202},
{7,255,"DTMF_ROW_RATIO_TRES",0x0198},
{8,255,"DTMF_COL_RATIO_TRES",0x0198},
{9,255,"DTMF_ROW_2ND_ARM",0x0611},
{10,255,"DTMF_COL_2ND_ARM",0x0202},
{11,255,"DTMF_PWR_MIN_TRES",0x00E5},
{12,255,"DTMF_OT_LIM_TRES",0x0A1C},
{13,0,"OSC1_COEF",0x7B30},
{14,1,"OSC1X",0x0063},
{15,2,"OSC1Y",0x0000},
{16,3,"OSC2_COEF",0x7870},
{17,4,"OSC2X",0x007D},
{18,5,"OSC2Y",0x0000},
{19,6,"RING_V_OFF",0x0000},
{20,7,"RING_OSC",0x7EF0},
{21,8,"RING_X",0x0160},
{22,9,"RING_Y",0x0000},
{23,255,"PULSE_ENVEL",0x2000},
{24,255,"PULSE_X",0x2000},
{25,255,"PULSE_Y",0x0000},
//{26,13,"RECV_DIGITAL_GAIN",0x4000},	// playback volume set lower
{26,13,"RECV_DIGITAL_GAIN",0x2000},	// playback volume set lower
{27,14,"XMIT_DIGITAL_GAIN",0x4000},
//{27,14,"XMIT_DIGITAL_GAIN",0x2000},
{28,15,"LOOP_CLOSE_TRES",0x1000},
{29,16,"RING_TRIP_TRES",0x3600},
{30,17,"COMMON_MIN_TRES",0x1000},
{31,18,"COMMON_MAX_TRES",0x0200},
{32,19,"PWR_ALARM_Q1Q2",0x07C0},
{33,20,"PWR_ALARM_Q3Q4", 0x4C00 /* 0x2600 */},
{34,21,"PWR_ALARM_Q5Q6",0x1B80},
{35,22,"LOOP_CLOSURE_FILTER",0x8000},
{36,23,"RING_TRIP_FILTER",0x0320},
{37,24,"TERM_LP_POLE_Q1Q2",0x008C},
{38,25,"TERM_LP_POLE_Q3Q4",0x0100},
{39,26,"TERM_LP_POLE_Q5Q6",0x0010},
{40,27,"CM_BIAS_RINGING",0x0C00},
{41,64,"DCDC_MIN_V",0x0C00},
{42,255,"DCDC_XTRA",0x1000},
{43,66,"LOOP_CLOSE_TRES_LOW",0x1000},
};

#ifdef FANCY_ECHOCAN
static char ectab[] = {
0, 0, 0, 1, 2, 3, 4, 6, 8, 9, 11, 13, 16, 18, 20, 22, 24, 25, 27, 28, 29, 30, 31, 31, 32, 
32, 32, 32, 32, 32, 32, 32, 32, 32, 32 ,32 ,32, 32,
32, 32, 32, 32, 32, 32, 32, 32, 32, 32 ,32 ,32, 32,
32, 32, 32, 32, 32, 32, 32, 32, 32, 32 ,32 ,32, 32,
31, 31, 30, 29, 28, 27, 25, 23, 22, 20, 18, 16, 13, 11, 9, 8, 6, 4, 3, 2, 1, 0, 0, 
};
static int ectrans[4] = { 0, 1, 3, 2 };
#define EC_SIZE (sizeof(ectab))
#define EC_SIZE_Q (sizeof(ectab) / 4)
#endif

/* Undefine to enable Power alarm / Transistor debug -- note: do not
   enable for normal operation! */
/* #define PAQ_DEBUG */

#define DEBUG_CARD (1 << 0)
#define DEBUG_ECHOCAN (1 << 1)

#include "fxo_modes.h"

struct wctdm_desc {
	char *name;
	int flags;
	int ports;
};

static struct wctdm_desc wctdm2400 = { "Wildcard TDM2400P", 0, 24 };
static struct wctdm_desc wctdm800 = { "Wildcard TDM800P", 0, 8 };
static struct wctdm_desc wctdm410 = { "Wildcard TDM410P", 0, 4 };
static struct wctdm_desc wcaex2400 = { "Wildcard AEX2400", FLAG_EXPRESS, 24 };
static struct wctdm_desc wcaex800 = { "Wildcard AEX800", FLAG_EXPRESS, 8 };
static struct wctdm_desc wcaex410 = { "Wildcard AEX410", FLAG_EXPRESS, 4 };

static int acim2tiss[16] = { 0x0, 0x1, 0x4, 0x5, 0x7, 0x0, 0x0, 0x6, 0x0, 0x0, 0x0, 0x2, 0x0, 0x3 };

struct wctdm *ifaces[WC_MAX_IFACES];
spinlock_t ifacelock = SPIN_LOCK_UNLOCKED;

static void wctdm_release(struct wctdm *wc);

static int fxovoltage = 0;
static unsigned int battdebounce;
static unsigned int battalarm;
static unsigned int battthresh;
static int debug = 0;
static int robust = 0;
static int lowpower = 0;
static int boostringer = 0;
static int fastringer = 0;
static int _opermode = 0;
static char *opermode = "FCC";
static int fxshonormode = 0;
static int alawoverride = 0;
static int fxo_addrs[4] = { 0x00, 0x08, 0x04, 0x0c };
static int fxotxgain = 0;
static int fxorxgain = 0;
static int fxstxgain = 0;
static int fxsrxgain = 0;
static int nativebridge = 0;
static int ringdebounce = DEFAULT_RING_DEBOUNCE;
static int fwringdetect = 0;
static int latency = VOICEBUS_DEFAULT_LATENCY;

#define MS_PER_HOOKCHECK	(1)
#define NEONMWI_ON_DEBOUNCE	(100/MS_PER_HOOKCHECK)

static int neonmwi_monitor = 0; 	/* Note: this causes use of full wave ring detect */
static int neonmwi_level = 75;		/* neon mwi trip voltage */
static int neonmwi_envelope = 10;
static int neonmwi_offlimit = 16000;  /* Time in milliseconds the monitor is checked before saying no message is waiting */

static int neonmwi_offlimit_cycles;  /* Time in milliseconds the monitor is checked before saying no message is waiting */

#ifdef VPM_SUPPORT
static int vpmsupport = 1;
static int vpmdtmfsupport = 0;
#define VPM_DEFAULT_DTMFTHRESHOLD 1250
static int dtmfthreshold = VPM_DEFAULT_DTMFTHRESHOLD;
/*
 * This parameter is used to adjust the NLP type used.  The options are:
 * 0 : None
 * 1 : Mute
 * 2 : Random Noise
 * 3 : Hoth Noise
 * 4 : Suppression NLP - In order to use this, you must set the vpmnlpmaxsupp parameter to
 * 	some value in order to give the amount of dB to suppress to the suppressor
 */
static int vpmnlptype = 1;
/* This is the threshold (in dB) for enabling and disabling of the NLP */
static int vpmnlpthresh = 24;
/* See vpmnlptype = 4 for more info */
static int vpmnlpmaxsupp = 0;
#endif

static int wctdm_init_proslic(struct wctdm *wc, int card, int fast , int manual, int sane);

static inline int CMD_BYTE(int card, int bit, int altcs)
{
	/* Let's add some trickery to make the TDM410 work */
	if (altcs == 3) {
		if (card == 2) {
			card = 4;
			altcs = 0;
		} else if (card == 3) {
			card = 5;
			altcs = 2;
		}
	}

	return (((((card) & 0x3) * 3 + (bit)) * 7) \
			+ ((card) >> 2) + (altcs) + ((altcs) ? -21 : 0));
}

/* sleep in user space until woken up. Equivilant of tsleep() in BSD */
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

static inline int empty_slot(struct wctdm *wc, int card)
{
	int x;
	for (x=0;x<USER_COMMANDS;x++) {
		if (!wc->cmdq[card].cmds[x])
			return x;
	}
	return -1;
}

#ifdef VPM_SUPPORT
static inline void cmd_dequeue_vpm150m(struct wctdm *wc, volatile unsigned char *writechunk, int whichframe)
{
	unsigned long flags;
	struct vpm150m_cmd *curcmd = NULL;
	struct vpm150m *vpm150m = wc->vpm150m;
	int x;
	unsigned char leds = ~((wc->intcount / 1000) % 8) & 0x7;

	/* Skip audio */
	writechunk += 24;

	spin_lock_irqsave(&wc->reglock, flags);

	if (test_bit(VPM150M_SPIRESET, &vpm150m->control) || test_bit(VPM150M_HPIRESET, &vpm150m->control)) {
		if (debug & DEBUG_ECHOCAN)
			printk("HW Resetting VPMADT032...\n");
		for (x = 24; x < 28; x++) {
			if (x == 24) {
				if (test_and_clear_bit(VPM150M_SPIRESET, &vpm150m->control))
					writechunk[CMD_BYTE(x, 0, 0)] = 0x08;
				else if (test_and_clear_bit(VPM150M_HPIRESET, &vpm150m->control))
					writechunk[CMD_BYTE(x, 0, 0)] = 0x0b;
			} else
				writechunk[CMD_BYTE(x, 0, 0)] = 0x00 | leds;
			writechunk[CMD_BYTE(x, 1, 0)] = 0;
			writechunk[CMD_BYTE(x, 2, 0)] = 0x00;
		}
		spin_unlock_irqrestore(&wc->reglock, flags);
		return;
	}


	/* Search for something waiting to transmit */
	for (x = 0; x < VPM150M_MAX_COMMANDS; x++) {
		if ((vpm150m->cmdq[x].desc & (__VPM150M_RD | __VPM150M_WR)) && 
		   !(vpm150m->cmdq[x].desc & (__VPM150M_FIN | __VPM150M_TX))) {
		   	curcmd = &vpm150m->cmdq[x];
			curcmd->txident = wc->txident;
			curcmd->desc |= __VPM150M_TX;
			break;
		}
	}
	if (curcmd) {
#if 0
		printk("Found command txident = %d, desc = 0x%x, addr = 0x%x, data = 0x%x\n", curcmd->txident, curcmd->desc, curcmd->addr, curcmd->data);
#endif
		if (curcmd->desc & __VPM150M_RWPAGE) {
			/* Set CTRL access to page*/
			writechunk[CMD_BYTE(24, 0, 0)] = (0x8 << 4);
			writechunk[CMD_BYTE(24, 1, 0)] = 0;
			writechunk[CMD_BYTE(24, 2, 0)] = 0x20;

			/* Do a page write */
			if (curcmd->desc & __VPM150M_WR)
				writechunk[CMD_BYTE(25, 0, 0)] = ((0x8 | 0x4) << 4);
			else
				writechunk[CMD_BYTE(25, 0, 0)] = ((0x8 | 0x4 | 0x1) << 4);
			writechunk[CMD_BYTE(25, 1, 0)] = 0;
			if (curcmd->desc & __VPM150M_WR)
				writechunk[CMD_BYTE(25, 2, 0)] = curcmd->data[0] & 0xf;
			else
				writechunk[CMD_BYTE(25, 2, 0)] = 0;

			/* Clear XADD */
			writechunk[CMD_BYTE(26, 0, 0)] = (0x8 << 4);
			writechunk[CMD_BYTE(26, 1, 0)] = 0;
			writechunk[CMD_BYTE(26, 2, 0)] = 0;

			/* Fill in to buffer to size */
			writechunk[CMD_BYTE(27, 0, 0)] = 0;
			writechunk[CMD_BYTE(27, 1, 0)] = 0;
			writechunk[CMD_BYTE(27, 2, 0)] = 0;

		} else {
			/* Set address */
			writechunk[CMD_BYTE(24, 0, 0)] = ((0x8 | 0x4) << 4);
			writechunk[CMD_BYTE(24, 1, 0)] = (curcmd->addr >> 8) & 0xff;
			writechunk[CMD_BYTE(24, 2, 0)] = curcmd->addr & 0xff;

			/* Send/Get our data */
			if (curcmd->desc & __VPM150M_WR) {
				if (curcmd->datalen > 1)
					writechunk[CMD_BYTE(25, 0, 0)] = ((0x8 | (0x1 << 1)) << 4);
				else
					writechunk[CMD_BYTE(25, 0, 0)] = ((0x8 | (0x3 << 1)) << 4);
			} else
				if (curcmd->datalen > 1)
					writechunk[CMD_BYTE(25, 0, 0)] = ((0x8 | (0x1 << 1) | 0x1) << 4);
				else
					writechunk[CMD_BYTE(25, 0, 0)] = ((0x8 | (0x3 << 1) | 0x1) << 4);
			writechunk[CMD_BYTE(25, 1, 0)] = (curcmd->data[0] >> 8) & 0xff;
			writechunk[CMD_BYTE(25, 2, 0)] = curcmd->data[0] & 0xff;
			
			if (curcmd->datalen > 1) {
				if (curcmd->desc & __VPM150M_WR)
					writechunk[CMD_BYTE(26, 0, 0)] = ((0x8 | (0x1 << 1)) << 4);
				else
					writechunk[CMD_BYTE(26, 0, 0)] = ((0x8 | (0x1 << 1) | 0x1) << 4);
				writechunk[CMD_BYTE(26, 1, 0)] = (curcmd->data[1] >> 8) & 0xff;
				writechunk[CMD_BYTE(26, 2, 0)] = curcmd->data[1] & 0xff;
			} else {
				/* Fill in the rest */
				writechunk[CMD_BYTE(26, 0, 0)] = 0;
				writechunk[CMD_BYTE(26, 1, 0)] = 0;
				writechunk[CMD_BYTE(26, 2, 0)] = 0;
			}

			if (curcmd->datalen > 2) {
				if (curcmd->desc & __VPM150M_WR)
					writechunk[CMD_BYTE(27, 0, 0)] = ((0x8 | (0x1 << 1)) << 4);
				else
					writechunk[CMD_BYTE(27, 0, 0)] = ((0x8 | (0x1 << 1) | 0x1) << 4);
				writechunk[CMD_BYTE(27, 1, 0)] = (curcmd->data[2] >> 8) & 0xff;
				writechunk[CMD_BYTE(27, 2, 0)] = curcmd->data[2] & 0xff;
			} else {
				/* Fill in the rest */
				writechunk[CMD_BYTE(27, 0, 0)] = 0;
				writechunk[CMD_BYTE(27, 1, 0)] = 0;
				writechunk[CMD_BYTE(27, 2, 0)] = 0;
			}


		}
	} else if (test_and_clear_bit(VPM150M_SWRESET, &vpm150m->control)) {
		printk("Booting VPMADT032\n");
		for (x = 24; x < 28; x++) {
			if (x == 24)
				writechunk[CMD_BYTE(x, 0, 0)] = (0x8 << 4);
			else
				writechunk[CMD_BYTE(x, 0, 0)] = 0x00;
			writechunk[CMD_BYTE(x, 1, 0)] = 0;
			if (x == 24)
				writechunk[CMD_BYTE(x, 2, 0)] = 0x01;
			else
				writechunk[CMD_BYTE(x, 2, 0)] = 0x00;
		}
	} else {
		for (x = 24; x < 28; x++) {
			writechunk[CMD_BYTE(x, 0, 0)] = 0x00;
			writechunk[CMD_BYTE(x, 1, 0)] = 0x00;
			writechunk[CMD_BYTE(x, 2, 0)] = 0x00;
		}
	}

#ifdef VPM150M_SUPPORT
	/* Add our leds in */
	for (x = 24; x < 28; x++)
		writechunk[CMD_BYTE(x, 0, 0)] |= leds;

	/* Now let's figure out if we need to check for DTMF */
	if (test_bit(VPM150M_ACTIVE, &vpm150m->control) && !whichframe && !(wc->intcount % 100))
		queue_work(vpm150m->wq, &vpm150m->work);
#endif

	spin_unlock_irqrestore(&wc->reglock, flags);
}
#endif /* VPM_SUPPORT */

static inline void cmd_dequeue(struct wctdm *wc, volatile unsigned char *writechunk, int card, int pos)
{
	unsigned long flags;
	unsigned int curcmd=0;
	int x;
	int subaddr = card & 0x3;
#ifdef FANCY_ECHOCAN
	int ecval;
	ecval = wc->echocanpos;
	ecval += EC_SIZE_Q * ectrans[(card & 0x3)];
	ecval = ecval % EC_SIZE;
#endif

 	/* if a QRV card, map it to its first channel */  
 	if ((wc->modtype[card] ==  MOD_TYPE_QRV) && (card & 3))
 	{
 		return;
 	}
 	if (wc->altcs[card])
 		subaddr = 0;
 

 
	/* Skip audio */
	writechunk += 24;
	spin_lock_irqsave(&wc->reglock, flags);
	/* Search for something waiting to transmit */
	if (pos) {
		for (x=0;x<MAX_COMMANDS;x++) {
			if ((wc->cmdq[card].cmds[x] & (__CMD_RD | __CMD_WR)) && 
			   !(wc->cmdq[card].cmds[x] & (__CMD_TX | __CMD_FIN))) {
			   	curcmd = wc->cmdq[card].cmds[x];
#if 0
				printk("Transmitting command '%08x' in slot %d\n", wc->cmdq[card].cmds[x], wc->txident);
#endif			
				wc->cmdq[card].cmds[x] |= (wc->txident << 24) | __CMD_TX;
				break;
			}
		}
	}
	if (!curcmd) {
		/* If nothing else, use filler */
		if (wc->modtype[card] == MOD_TYPE_FXS)
			curcmd = CMD_RD(64);
		else if (wc->modtype[card] == MOD_TYPE_FXO)
			curcmd = CMD_RD(12);
		else if (wc->modtype[card] == MOD_TYPE_QRV)
			curcmd = CMD_RD(3);
		else if (wc->modtype[card] == MOD_TYPE_VPM) {
#ifdef FANCY_ECHOCAN
			if (wc->blinktimer >= 0xf) {
				curcmd = CMD_WR(0x1ab, 0x0f);
			} else if (wc->blinktimer == (ectab[ecval] >> 1)) {
				curcmd = CMD_WR(0x1ab, 0x00);
			} else
#endif
			curcmd = CMD_RD(0x1a0);
		}
	}
	if (wc->modtype[card] == MOD_TYPE_FXS) {
 		writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = (1 << (subaddr));
		if (curcmd & __CMD_WR)
 			writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = (curcmd >> 8) & 0x7f;
		else
 			writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = 0x80 | ((curcmd >> 8) & 0x7f);
 		writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = curcmd & 0xff;
	} else if (wc->modtype[card] == MOD_TYPE_FXO) {
		if (curcmd & __CMD_WR)
 			writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = 0x20 | fxo_addrs[subaddr];
		else
 			writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = 0x60 | fxo_addrs[subaddr];
 		writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = (curcmd >> 8) & 0xff;
 		writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = curcmd & 0xff;
	} else if (wc->modtype[card] == MOD_TYPE_FXSINIT) {
		/* Special case, we initialize the FXS's into the three-byte command mode then
		   switch to the regular mode.  To send it into thee byte mode, treat the path as
		   6 two-byte commands and in the last one we initialize register 0 to 0x80. All modules
		   read this as the command to switch to daisy chain mode and we're done.  */
 		writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = 0x00;
 		writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = 0x00;
		if ((card & 0x1) == 0x1) 
 			writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = 0x80;
		else
 			writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = 0x00;
#ifdef VPM_SUPPORT
	} else if (wc->modtype[card] == MOD_TYPE_VPM) {
		if (curcmd & __CMD_WR)
 			writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = ((card & 0x3) << 4) | 0xc | ((curcmd >> 16) & 0x1);
		else
 			writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = ((card & 0x3) << 4) | 0xa | ((curcmd >> 16) & 0x1);
 		writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = (curcmd >> 8) & 0xff;
 		writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = curcmd & 0xff;
	} else if (wc->modtype[card] == MOD_TYPE_VPM150M) {
#endif
 	} else if (wc->modtype[card] == MOD_TYPE_QRV) {
 
 		writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = 0x00;
 		if (!curcmd)
 		{
 			writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = 0x00;
 			writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = 0x00;
 		}
 		else
 		{
 			if (curcmd & __CMD_WR)
 				writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = 0x40 | ((curcmd >> 8) & 0x3f);
 			else
 				writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = 0xc0 | ((curcmd >> 8) & 0x3f);
 			writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = curcmd & 0xff;
 		}
	} else if (wc->modtype[card] == MOD_TYPE_NONE) {
 		writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = 0x00;
 		writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = 0x00;
 		writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = 0x00;
	}
#if 0
	/* XXX */
	if (cmddesc < 40)
		printk("Pass %d, card = %d (modtype=%d), pos = %d, CMD_BYTES = %d,%d,%d, (%02x,%02x,%02x) curcmd = %08x\n", cmddesc, card, wc->modtype[card], pos, CMD_BYTE(card, 0), CMD_BYTE(card, 1), CMD_BYTE(card, 2), writechunk[CMD_BYTE(card, 0)], writechunk[CMD_BYTE(card, 1)], writechunk[CMD_BYTE(card, 2)], curcmd);
#endif
	spin_unlock_irqrestore(&wc->reglock, flags);
#if 0
	/* XXX */
	cmddesc++;
#endif	
}

#ifdef VPM_SUPPORT
static inline void cmd_decifer_vpm150m(struct wctdm *wc, volatile unsigned char *readchunk)
{
	unsigned long flags;
	unsigned char ident;
	int x, i;

	/* Skip audio */
	readchunk += 24;
	spin_lock_irqsave(&wc->reglock, flags);
	/* Search for any pending results */
	for (x = 0; x < VPM150M_MAX_COMMANDS; x++) {
		if ((wc->vpm150m->cmdq[x].desc & (__VPM150M_RD | __VPM150M_WR)) && 
		    (wc->vpm150m->cmdq[x].desc & (__VPM150M_TX)) && 
		   !(wc->vpm150m->cmdq[x].desc & (__VPM150M_FIN))) {
		   	ident = wc->vpm150m->cmdq[x].txident;
		   	if (ident == wc->rxident) {
				/* Store result */
				for (i = 0; i < wc->vpm150m->cmdq[x].datalen; i++) {
					wc->vpm150m->cmdq[x].data[i] = (0xff & readchunk[CMD_BYTE((25 + i), 1, 0)]) << 8;
					wc->vpm150m->cmdq[x].data[i] |= readchunk[CMD_BYTE((25 + i), 2, 0)];
				}
				if (wc->vpm150m->cmdq[x].desc & __VPM150M_WR) {
					/* Go ahead and clear out writes since they need no acknowledgement */
					wc->vpm150m->cmdq[x].desc = 0;
				} else
					wc->vpm150m->cmdq[x].desc |= __VPM150M_FIN;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&wc->reglock, flags);
}
#endif /* VPM_SUPPORT */

static inline void cmd_decifer(struct wctdm *wc, volatile unsigned char *readchunk, int card)
{
	unsigned long flags;
	unsigned char ident;
	int x;

	/* if a QRV card, map it to its first channel */  
	if ((wc->modtype[card] ==  MOD_TYPE_QRV) && (card & 3))
	{
		return;
	}
	/* Skip audio */
	readchunk += 24;
	spin_lock_irqsave(&wc->reglock, flags);
	/* Search for any pending results */
	for (x=0;x<MAX_COMMANDS;x++) {
		if ((wc->cmdq[card].cmds[x] & (__CMD_RD | __CMD_WR)) && 
		    (wc->cmdq[card].cmds[x] & (__CMD_TX)) && 
		   !(wc->cmdq[card].cmds[x] & (__CMD_FIN))) {
		   	ident = (wc->cmdq[card].cmds[x] >> 24) & 0xff;
		   	if (ident == wc->rxident) {
				/* Store result */
				wc->cmdq[card].cmds[x] |= readchunk[CMD_BYTE(card, 2, wc->altcs[card])];
				wc->cmdq[card].cmds[x] |= __CMD_FIN;
				if (wc->cmdq[card].cmds[x] & __CMD_WR) {
					/* Go ahead and clear out writes since they need no acknowledgement */
					wc->cmdq[card].cmds[x] = 0x00000000;
				} else if (x >= USER_COMMANDS) {
					/* Clear out ISR reads */
					wc->cmdq[card].isrshadow[x - USER_COMMANDS] = wc->cmdq[card].cmds[x] & 0xff;
					wc->cmdq[card].cmds[x] = 0x00000000;
				}
				break;
			}
		}
	}
#if 0
	/* XXX */
	if (!pos && (cmddesc < 256))
		printk("Card %d: Command '%08x' => %02x\n",card,  wc->cmdq[card].lasttx[pos], wc->cmdq[card].lastrd[pos]);
#endif
	spin_unlock_irqrestore(&wc->reglock, flags);
}

static inline void cmd_checkisr(struct wctdm *wc, int card)
{
	if (!wc->cmdq[card].cmds[USER_COMMANDS + 0]) {
		if (wc->sethook[card]) {
			wc->cmdq[card].cmds[USER_COMMANDS + 0] = wc->sethook[card];
			wc->sethook[card] = 0;
		} else if (wc->modtype[card] == MOD_TYPE_FXS) {
			wc->cmdq[card].cmds[USER_COMMANDS + 0] = CMD_RD(68);	/* Hook state */
		} else if (wc->modtype[card] == MOD_TYPE_FXO) {
			wc->cmdq[card].cmds[USER_COMMANDS + 0] = CMD_RD(5);	/* Hook/Ring state */
		} else if (wc->modtype[card] == MOD_TYPE_QRV) {
			wc->cmdq[card & 0xfc].cmds[USER_COMMANDS + 0] = CMD_RD(3);	/* COR/CTCSS state */
#ifdef VPM_SUPPORT
		} else if (wc->modtype[card] == MOD_TYPE_VPM) {
			wc->cmdq[card].cmds[USER_COMMANDS + 0] = CMD_RD(0xb9); /* DTMF interrupt */
#endif
		}
	}
	if (!wc->cmdq[card].cmds[USER_COMMANDS + 1]) {
		if (wc->modtype[card] == MOD_TYPE_FXS) {
#ifdef PAQ_DEBUG
			wc->cmdq[card].cmds[USER_COMMANDS + 1] = CMD_RD(19);	/* Transistor interrupts */
#else
			wc->cmdq[card].cmds[USER_COMMANDS + 1] = CMD_RD(64);	/* Battery mode */
#endif
		} else if (wc->modtype[card] == MOD_TYPE_FXO) {
			wc->cmdq[card].cmds[USER_COMMANDS + 1] = CMD_RD(29);	/* Battery */
		} else if (wc->modtype[card] == MOD_TYPE_QRV) {
			wc->cmdq[card & 0xfc].cmds[USER_COMMANDS + 1] = CMD_RD(3);	/* Battery */
#ifdef VPM_SUPPORT
		} else if (wc->modtype[card] == MOD_TYPE_VPM) {
			wc->cmdq[card].cmds[USER_COMMANDS + 1] = CMD_RD(0xbd); /* DTMF interrupt */
#endif
		}
	}
}

static inline void wctdm_transmitprep(struct wctdm *wc, unsigned char *writechunk)
{
	int x,y;

	/* Calculate Transmission */
	if (likely(wc->initialized)) {
		zt_transmit(&wc->span);
	}

	for (x=0;x<ZT_CHUNKSIZE;x++) {
		/* Send a sample, as a 32-bit word */
		for (y=0;y < wc->cards;y++) {
			if (!x) {
				cmd_checkisr(wc, y);
			}

			if (likely(wc->initialized)) {
				if (y < wc->type)
					writechunk[y] = wc->chans[y].writechunk[x];
			}
			cmd_dequeue(wc, writechunk, y, x);
		}
#ifdef VPM_SUPPORT
		if (!x)
			wc->blinktimer++;
		if (wc->vpm) {
			for (y=24;y<28;y++) {
				if (!x) {
					cmd_checkisr(wc, y);
				}
				cmd_dequeue(wc, writechunk, y, x);
			}
#ifdef FANCY_ECHOCAN
			if (wc->vpm && wc->blinktimer >= 0xf) {
				wc->blinktimer = -1;
				wc->echocanpos++;
			}
#endif			
		} else if (wc->vpm150m) {
			cmd_dequeue_vpm150m(wc, writechunk, x);
		}
#endif		
		if (x < ZT_CHUNKSIZE - 1) {
			writechunk[EFRAME_SIZE] = wc->ctlreg;
			writechunk[EFRAME_SIZE + 1] = wc->txident++;
#if 1
			if ((wc->type == 4) && ((wc->ctlreg & 0x10) || (wc->modtype[NUM_CARDS] == MOD_TYPE_NONE))) {
				writechunk[EFRAME_SIZE + 2] = 0;
				for (y = 0; y < 4; y++) {
					if (wc->modtype[y] == MOD_TYPE_NONE)
						writechunk[EFRAME_SIZE + 2] |= (1 << y);
				}
			} else
				writechunk[EFRAME_SIZE + 2] = 0xf;
#endif
		}
		writechunk += (EFRAME_SIZE + EFRAME_GAP);
	}
}

static inline int wctdm_setreg_full(struct wctdm *wc, int card, int addr, int val, int inisr)
{
	unsigned long flags;
	int hit=0;
	int ret;

	/* if a QRV card, use only its first channel */  
	if (wc->modtype[card] ==  MOD_TYPE_QRV)
	{
		if (card & 3) return(0);
	}
	do {
		spin_lock_irqsave(&wc->reglock, flags);
		hit = empty_slot(wc, card);
		if (hit > -1) {
			wc->cmdq[card].cmds[hit] = CMD_WR(addr, val);
		}
		spin_unlock_irqrestore(&wc->reglock, flags);
		if (inisr)
			break;
		if (hit < 0) {
			if ((ret = schluffen(&wc->regq)))
				return ret;
		}
	} while (hit < 0);
	return (hit > -1) ? 0 : -1;
}

static inline int wctdm_setreg_intr(struct wctdm *wc, int card, int addr, int val)
{
	return wctdm_setreg_full(wc, card, addr, val, 1);
}
static inline int wctdm_setreg(struct wctdm *wc, int card, int addr, int val)
{
	return wctdm_setreg_full(wc, card, addr, val, 0);
}

static inline int wctdm_getreg(struct wctdm *wc, int card, int addr)
{
	unsigned long flags;
	int hit;
	int ret=0;

	/* if a QRV card, use only its first channel */  
	if (wc->modtype[card] ==  MOD_TYPE_QRV)
	{
		if (card & 3) return(0);
	}
	do {
		spin_lock_irqsave(&wc->reglock, flags);
		hit = empty_slot(wc, card);
		if (hit > -1) {
			wc->cmdq[card].cmds[hit] = CMD_RD(addr);
		}
		spin_unlock_irqrestore(&wc->reglock, flags);
		if (hit < 0) {
			if ((ret = schluffen(&wc->regq)))
				return ret;
		}
	} while (hit < 0);
	do {
		spin_lock_irqsave(&wc->reglock, flags);
		if (wc->cmdq[card].cmds[hit] & __CMD_FIN) {
			ret = wc->cmdq[card].cmds[hit] & 0xff;
			wc->cmdq[card].cmds[hit] = 0x00000000;
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

#ifdef VPM_SUPPORT
static inline unsigned char wctdm_vpm_in(struct wctdm *wc, int unit, const unsigned int addr)
{
	return wctdm_getreg(wc, unit + NUM_CARDS, addr);
}

static inline void wctdm_vpm_out(struct wctdm *wc, int unit, const unsigned int addr, const unsigned char val)
{
	wctdm_setreg(wc, unit + NUM_CARDS, addr, val);
}

static inline void cmd_vpm150m_retransmit(struct wctdm *wc)
{
	unsigned long flags;
	int x;

	spin_lock_irqsave(&wc->reglock, flags);
	for (x = 0; x < VPM150M_MAX_COMMANDS; x++) {
		if (!(wc->vpm150m->cmdq[x].desc & __VPM150M_FIN)) {
			//printk("Retransmit!\n");
			wc->vpm150m->cmdq[x].desc &= ~(__VPM150M_TX);
		}
	}
	spin_unlock_irqrestore(&wc->reglock, flags);

}
#endif

static inline void cmd_retransmit(struct wctdm *wc)
{
	int x,y;
	unsigned long flags;
	/* Force retransmissions */
	spin_lock_irqsave(&wc->reglock, flags);
	for (x=0;x<MAX_COMMANDS;x++) {
		for (y=0;y<wc->cards;y++) {
			if (!(wc->cmdq[y].cmds[x] & __CMD_FIN))
				wc->cmdq[y].cmds[x] &= ~(__CMD_TX | (0xff << 24));
		}
	}
	spin_unlock_irqrestore(&wc->reglock, flags);
#ifdef VPM_SUPPORT
	if (wc->vpm150m)
		cmd_vpm150m_retransmit(wc);
#endif
}

static inline void wctdm_receiveprep(struct wctdm *wc, unsigned char *readchunk)
{
	int x,y;
	unsigned char expected;

	BUG_ON(NULL == readchunk);

	for (x=0;x<ZT_CHUNKSIZE;x++) {
		if (x < ZT_CHUNKSIZE - 1) {
			expected = wc->rxident+1;
			wc->rxident = readchunk[EFRAME_SIZE + 1];
			if (wc->rxident != expected) {
				wc->span.irqmisses++;
				cmd_retransmit(wc);
			}
		}
		for (y=0;y < wc->cards;y++) {
			if (likely(wc->initialized)) {
				if (y < wc->type) {
					wc->chans[y].readchunk[x] = readchunk[y];
				}
			}	
			cmd_decifer(wc, readchunk, y);
		}
#ifdef VPM_SUPPORT
		if (wc->vpm) {
			for (y=NUM_CARDS;y < NUM_CARDS + NUM_EC; y++)
				cmd_decifer(wc, readchunk, y);
		} else if (wc->vpm150m)
			cmd_decifer_vpm150m(wc, readchunk);
#endif

		readchunk += (EFRAME_SIZE + EFRAME_GAP);
	}
	/* XXX We're wasting 8 taps.  We should get closer :( */
	if (likely(wc->initialized)) {
		for (x=0;x<wc->type;x++) {
			if (wc->cardflag & (1 << x))
				zt_ec_chunk(&wc->chans[x], wc->chans[x].readchunk, wc->chans[x].writechunk);
		}
		zt_receive(&wc->span);
	}
	/* Wake up anyone sleeping to read/write a new register */
	wake_up_interruptible(&wc->regq);
}

static int wait_access(struct wctdm *wc, int card)
{
    unsigned char data=0;
    long origjiffies;
    int count = 0;

    #define MAX 10 /* attempts */


    origjiffies = jiffies;
    /* Wait for indirect access */
    while (count++ < MAX)
	 {
		data = wctdm_getreg(wc, card, I_STATUS);

		if (!data)
			return 0;

	 }

    if(count > (MAX-1)) printk(" ##### Loop error (%02x) #####\n", data);

	return 0;
}

static unsigned char translate_3215(unsigned char address)
{
	int x;
	for (x=0;x<sizeof(indirect_regs)/sizeof(indirect_regs[0]);x++) {
		if (indirect_regs[x].address == address) {
			address = indirect_regs[x].altaddr;
			break;
		}
	}
	return address;
}

static int wctdm_proslic_setreg_indirect(struct wctdm *wc, int card, unsigned char address, unsigned short data)
{
	int res = -1;
	/* Translate 3215 addresses */
	if (wc->flags[card] & FLAG_3215) {
		address = translate_3215(address);
		if (address == 255)
			return 0;
	}
	if(!wait_access(wc, card)) {
		wctdm_setreg(wc, card, IDA_LO,(unsigned char)(data & 0xFF));
		wctdm_setreg(wc, card, IDA_HI,(unsigned char)((data & 0xFF00)>>8));
		wctdm_setreg(wc, card, IAA,address);
		res = 0;
	};
	return res;
}

static int wctdm_proslic_getreg_indirect(struct wctdm *wc, int card, unsigned char address)
{ 
	int res = -1;
	char *p=NULL;
	/* Translate 3215 addresses */
	if (wc->flags[card] & FLAG_3215) {
		address = translate_3215(address);
		if (address == 255)
			return 0;
	}
	if (!wait_access(wc, card)) {
		wctdm_setreg(wc, card, IAA, address);
		if (!wait_access(wc, card)) {
			unsigned char data1, data2;
			data1 = wctdm_getreg(wc, card, IDA_LO);
			data2 = wctdm_getreg(wc, card, IDA_HI);
			res = data1 | (data2 << 8);
		} else
			p = "Failed to wait inside";
	} else
		p = "failed to wait";
	if (p)
		printk("%s\n",p);
	return res;
}

static int wctdm_proslic_init_indirect_regs(struct wctdm *wc, int card)
{
	unsigned char i;

	for (i=0; i<sizeof(indirect_regs) / sizeof(indirect_regs[0]); i++)
	{
		if(wctdm_proslic_setreg_indirect(wc, card, indirect_regs[i].address,indirect_regs[i].initial))
			return -1;
	}

	return 0;
}

static int wctdm_proslic_verify_indirect_regs(struct wctdm *wc, int card)
{ 
	int passed = 1;
	unsigned short i, initial;
	int j;

	for (i=0; i<sizeof(indirect_regs) / sizeof(indirect_regs[0]); i++) 
	{
		if((j = wctdm_proslic_getreg_indirect(wc, card, (unsigned char) indirect_regs[i].address)) < 0) {
			printk("Failed to read indirect register %d\n", i);
			return -1;
		}
		initial= indirect_regs[i].initial;

		if ( j != initial && (!(wc->flags[card] & FLAG_3215) || (indirect_regs[i].altaddr != 255)))
		{
			 printk("!!!!!!! %s  iREG %X = %X  should be %X\n",
				indirect_regs[i].name,indirect_regs[i].address,j,initial );
			 passed = 0;
		}	
	}

    if (passed) {
		if (debug & DEBUG_CARD)
			printk("Init Indirect Registers completed successfully.\n");
    } else {
		printk(" !!!!! Init Indirect Registers UNSUCCESSFULLY.\n");
		return -1;
    }
    return 0;
}

static inline void wctdm_proslic_recheck_sanity(struct wctdm *wc, int card)
{
	int res;
#ifdef PAQ_DEBUG
	res = wc->cmdq[card].isrshadow[1];
	res &= ~0x3;
	if (res) {
		wc->cmdq[card].isrshadow[1]=0;
		wc->mods[card].fxs.palarms++;
		if (wc->mods[card].fxs.palarms < MAX_ALARMS) {
			printk("Power alarm (%02x) on module %d, resetting!\n", res, card + 1);
			if (wc->mods[card].fxs.lasttxhook == 4)
				wc->mods[card].fxs.lasttxhook = 0x11;
			wc->sethook[card] = CMD_WR(19, res);
#if 0
			wc->sethook[card] = CMD_WR(64, wc->mods[card].fxs.lasttxhook);
#endif

			/* wctdm_setreg_intr(wc, card, 64, wc->mods[card].fxs.lasttxhook); */
			/* Update shadow register to avoid extra power alarms until next read */
			wc->cmdq[card].isrshadow[1] = 0;
		} else {
			if (wc->mods[card].fxs.palarms == MAX_ALARMS)
				printk("Too many power alarms on card %d, NOT resetting!\n", card + 1);
		}
	}
#else
	res = wc->cmdq[card].isrshadow[1];
	/* This makes sure the lasthook was put in reg 64 the linefeed reg */
	if (((res & 0x0f) | 0x10) == wc->mods[card].fxs.lasttxhook) 
		wc->mods[card].fxs.lasttxhook &= 0x0f;

	res = !res &&    /* reg 64 has to be zero at last isr read */
		!(wc->mods[card].fxs.lasttxhook & 0x10 ) && /* not a transition */
		wc->mods[card].fxs.lasttxhook; /* not an intended zero */
	
	if (res) {
		wc->mods[card].fxs.palarms++;
		if (wc->mods[card].fxs.palarms < MAX_ALARMS) {
			printk("Power alarm on module %d, resetting!\n", card + 1);
			if (wc->mods[card].fxs.lasttxhook == 4)
				wc->mods[card].fxs.lasttxhook = 0x11;
			wc->mods[card].fxs.lasttxhook |= 0x10;
			wc->sethook[card] = CMD_WR(64, wc->mods[card].fxs.lasttxhook);

			/* wctdm_setreg_intr(wc, card, 64, wc->mods[card].fxs.lasttxhook); */
			/* Update shadow register to avoid extra power alarms until next read */
			wc->cmdq[card].isrshadow[1] = wc->mods[card].fxs.lasttxhook;
		} else {
			if (wc->mods[card].fxs.palarms == MAX_ALARMS)
				printk("Too many power alarms on card %d, NOT resetting!\n", card + 1);
		}
	}
#endif
}

static inline void wctdm_qrvdri_check_hook(struct wctdm *wc, int card)
{
	signed char b,b1;
	int qrvcard = card & 0xfc;

	
	if (wc->qrvdebtime[card] >= 2) wc->qrvdebtime[card]--;
	b = wc->cmdq[qrvcard].isrshadow[0];	/* Hook/Ring state */
	b &= 0xcc; /* use bits 3-4 and 6-7 only */

	if (wc->radmode[qrvcard] & RADMODE_IGNORECOR) b &= ~4;
	else if (!(wc->radmode[qrvcard] & RADMODE_INVERTCOR)) b ^= 4;
	if (wc->radmode[qrvcard + 1] | RADMODE_IGNORECOR) b &= ~0x40;
	else if (!(wc->radmode[qrvcard + 1] | RADMODE_INVERTCOR)) b ^= 0x40;

	if ((wc->radmode[qrvcard] & RADMODE_IGNORECT) || 
		(!(wc->radmode[qrvcard] & RADMODE_EXTTONE))) b &= ~8;
	else if (!(wc->radmode[qrvcard] & RADMODE_EXTINVERT)) b ^= 8;
	if ((wc->radmode[qrvcard + 1] & RADMODE_IGNORECT) || 
		(!(wc->radmode[qrvcard + 1] & RADMODE_EXTTONE))) b &= ~0x80;
	else if (!(wc->radmode[qrvcard + 1] & RADMODE_EXTINVERT)) b ^= 0x80;
	/* now b & MASK should be zero, if its active */
	/* check for change in chan 0 */
	if ((!(b & 0xc)) != wc->qrvhook[qrvcard + 2])
	{
		wc->qrvdebtime[qrvcard] = wc->debouncetime[qrvcard];
		wc->qrvhook[qrvcard + 2] = !(b & 0xc);
	} 
	/* if timed-out and ready */
	if (wc->qrvdebtime[qrvcard] == 1)
	{
		b1 = wc->qrvhook[qrvcard + 2];
if (debug) printk("QRV channel %d rx state changed to %d\n",qrvcard,wc->qrvhook[qrvcard + 2]);
		zt_hooksig(&wc->chans[qrvcard], 
			(b1) ? ZT_RXSIG_OFFHOOK : ZT_RXSIG_ONHOOK);
		wc->qrvdebtime[card] = 0;
	}
	/* check for change in chan 1 */
	if ((!(b & 0xc0)) != wc->qrvhook[qrvcard + 3])
	{
		wc->qrvdebtime[qrvcard + 1] = QRV_DEBOUNCETIME;
		wc->qrvhook[qrvcard + 3] = !(b & 0xc0);
	}
	if (wc->qrvdebtime[qrvcard + 1] == 1)
	{
		b1 = wc->qrvhook[qrvcard + 3];
if (debug) printk("QRV channel %d rx state changed to %d\n",qrvcard + 1,wc->qrvhook[qrvcard + 3]);
		zt_hooksig(&wc->chans[qrvcard + 1], 
			(b1) ? ZT_RXSIG_OFFHOOK : ZT_RXSIG_ONHOOK);
		wc->qrvdebtime[card] = 0;
	}
	return;
}

static inline void wctdm_voicedaa_check_hook(struct wctdm *wc, int card)
{
#define MS_PER_CHECK_HOOK 1

	unsigned char res;
	signed char b;
	struct fxo *fxo = &wc->mods[card].fxo;
	unsigned int abs_voltage;

	/* Try to track issues that plague slot one FXO's */
	b = wc->cmdq[card].isrshadow[0];	/* Hook/Ring state */
	b &= 0x9b;
	if (fxo->offhook) {
		if (b != 0x9)
			wctdm_setreg_intr(wc, card, 5, 0x9);
	} else {
		if (b != 0x8)
			wctdm_setreg_intr(wc, card, 5, 0x8);
	}
	if (!fxo->offhook) {
		if(fwringdetect || neonmwi_monitor) {
			/* Look for ring status bits (Ring Detect Signal Negative and
			 * Ring Detect Signal Positive) to transition back and forth
			 * some number of times to indicate that a ring is occurring.
			 * Provide some number of samples to allow for the transitions
			 * to occur before ginving up.
			 * NOTE: neon mwi voltages will trigger one of these bits to go active
			 * but not to have transitions between the two bits (i.e. no negative
			 * to positive or positive to negative transversals )
			 */ 
			res =  wc->cmdq[card].isrshadow[0] & 0x60;
			if (0 == wc->mods[card].fxo.wasringing) {
				if (res) {
					/* Look for positive/negative crossings in ring status reg */
					fxo->wasringing = 2; 
					fxo->ringdebounce = ringdebounce /16;
					fxo->lastrdtx = res;
					fxo->lastrdtx_count = 0;
				}
			} else if (2 == fxo->wasringing) {
				/* If ring detect signal has transversed */
				if (res && res != fxo->lastrdtx) {
					/* if there are at least 3 ring polarity transversals */
					if(++fxo->lastrdtx_count >= 2) {
						fxo->wasringing = 1;
						if (debug)
							printk("FW RING on %d/%d!\n", wc->span.spanno, card + 1);
						zt_hooksig(&wc->chans[card], ZT_RXSIG_RING);
						fxo->ringdebounce = ringdebounce / 16;
					} else {
						fxo->lastrdtx = res;
						fxo->ringdebounce = ringdebounce / 16;
					}
				/* ring indicator (positve or negative) has not transitioned, check debounce count */
				} else if (--fxo->ringdebounce == 0) {
					fxo->wasringing = 0;
				}
			} else {  /* I am in ring state */
				if (res) { /* If any ringdetect bits are still active */
					fxo->ringdebounce = ringdebounce / 16;
				} else if (--fxo->ringdebounce == 0) {
					fxo->wasringing = 0;
					if (debug)
						printk("FW NO RING on %d/%d!\n", wc->span.spanno, card + 1);
					zt_hooksig(&wc->chans[card], ZT_RXSIG_OFFHOOK);
				}
			}
		} else {
			res =  wc->cmdq[card].isrshadow[0];
			if ((res & 0x60) && (fxo->battery == BATTERY_PRESENT)) {
				fxo->ringdebounce += (ZT_CHUNKSIZE * 16);
				if (fxo->ringdebounce >= ZT_CHUNKSIZE * ringdebounce) {
					if (!fxo->wasringing) {
						fxo->wasringing = 1;
						zt_hooksig(&wc->chans[card], ZT_RXSIG_RING);
						if (debug)
							printk("RING on %d/%d!\n", wc->span.spanno, card + 1);
					}
					fxo->ringdebounce = ZT_CHUNKSIZE * ringdebounce;
				}
			} else {
				fxo->ringdebounce -= ZT_CHUNKSIZE * 4;
				if (fxo->ringdebounce <= 0) {
					if (fxo->wasringing) {
						fxo->wasringing = 0;
						zt_hooksig(&wc->chans[card], ZT_RXSIG_OFFHOOK);
						if (debug)
							printk("NO RING on %d/%d!\n", wc->span.spanno, card + 1);
					}
					fxo->ringdebounce = 0;
				}
					
			}
		}
	}

	b = wc->cmdq[card].isrshadow[1]; /* Voltage */
	abs_voltage = abs(b);

	if (fxovoltage) {
		if (!(wc->intcount % 100)) {
			printk("Port %d: Voltage: %d  Debounce %d\n", card + 1, 
			       b, fxo->battdebounce);
		}
	}

	if (abs_voltage < battthresh) {
		/* possible existing states:
		   battery lost, no debounce timer
		   battery lost, debounce timer (going to battery present)
		   battery present or unknown, no debounce timer
		   battery present or unknown, debounce timer (going to battery lost)
		*/

		if (fxo->battery == BATTERY_LOST) {
			if (fxo->battdebounce) {
				/* we were going to BATTERY_PRESENT, but battery was lost again,
				   so clear the debounce timer */
				fxo->battdebounce = 0;
			}
		} else {
			if (fxo->battdebounce) {
				/* going to BATTERY_LOST, see if we are there yet */
				if (--fxo->battdebounce == 0) {
					fxo->battery = BATTERY_LOST;
					if (debug)
						printk("NO BATTERY on %d/%d!\n", wc->span.spanno, card + 1);
#ifdef	JAPAN
					if (!wc->ohdebounce && wc->offhook) {
						zt_hooksig(&wc->chans[card], ZT_RXSIG_ONHOOK);
						if (debug)
							printk("Signalled On Hook\n");
#ifdef	ZERO_BATT_RING
						wc->onhook++;
#endif
					}
#else
					zt_hooksig(&wc->chans[card], ZT_RXSIG_ONHOOK);
					/* set the alarm timer, taking into account that part of its time
					   period has already passed while debouncing occurred */
					fxo->battalarm = (battalarm - battdebounce) / MS_PER_CHECK_HOOK;
#endif
				}
			} else {
				/* start the debounce timer to verify that battery has been lost */
				fxo->battdebounce = battdebounce / MS_PER_CHECK_HOOK;
			}
		}
	} else {
		/* possible existing states:
		   battery lost or unknown, no debounce timer
		   battery lost or unknown, debounce timer (going to battery present)
		   battery present, no debounce timer
		   battery present, debounce timer (going to battery lost)
		*/

		if (fxo->battery == BATTERY_PRESENT) {
			if (fxo->battdebounce) {
				/* we were going to BATTERY_LOST, but battery appeared again,
				   so clear the debounce timer */
				fxo->battdebounce = 0;
			}
		} else {
			if (fxo->battdebounce) {
				/* going to BATTERY_PRESENT, see if we are there yet */
				if (--fxo->battdebounce == 0) {
					fxo->battery = BATTERY_PRESENT;
					if (debug)
						printk("BATTERY on %d/%d (%s)!\n", wc->span.spanno, card + 1, 
						       (b < 0) ? "-" : "+");			    
#ifdef	ZERO_BATT_RING
					if (wc->onhook) {
						wc->onhook = 0;
						zt_hooksig(&wc->chans[card], ZT_RXSIG_OFFHOOK);
						if (debug)
							printk("Signalled Off Hook\n");
					}
#else
					zt_hooksig(&wc->chans[card], ZT_RXSIG_OFFHOOK);
#endif
					/* set the alarm timer, taking into account that part of its time
					   period has already passed while debouncing occurred */
					fxo->battalarm = (battalarm - battdebounce) / MS_PER_CHECK_HOOK;
				}
			} else {
				/* start the debounce timer to verify that battery has appeared */
				fxo->battdebounce = battdebounce / MS_PER_CHECK_HOOK;
			}
		}

		if (fxo->lastpol >= 0) {
			if (b < 0) {
				fxo->lastpol = -1;
				fxo->polaritydebounce = POLARITY_DEBOUNCE / MS_PER_CHECK_HOOK;
			}
		} 
		if (fxo->lastpol <= 0) {
			if (b > 0) {
				fxo->lastpol = 1;
				fxo->polaritydebounce = POLARITY_DEBOUNCE / MS_PER_CHECK_HOOK;
			}
		}
	}

	if (fxo->battalarm) {
		if (--fxo->battalarm == 0) {
			/* the alarm timer has expired, so update the battery alarm state
			   for this channel */
			zt_alarm_channel(&wc->chans[card], fxo->battery == BATTERY_LOST ? ZT_ALARM_RED : ZT_ALARM_NONE);
		}
	}

	if (fxo->polaritydebounce) {
	        fxo->polaritydebounce--;
		if (fxo->polaritydebounce < 1) {
		    if (fxo->lastpol != fxo->polarity) {
			if (debug & DEBUG_CARD)
				printk("%lu Polarity reversed (%d -> %d)\n", jiffies, 
				       fxo->polarity, 
				       fxo->lastpol);
			if (fxo->polarity)
				zt_qevent_lock(&wc->chans[card], ZT_EVENT_POLARITY);
			fxo->polarity = fxo->lastpol;
		    }
		}
	}

	/* Look for neon mwi pulse */
	if (neonmwi_monitor && !wc->mods[card].fxo.offhook) {
		/* Look for NEONMWI_ON_DEBOUNCE mS of consecutive voltage readings
		 * where the voltage is over the neon limit butdoes not vary greatly
		 * from the last reading
		 */
		if (fxo->battery == 1 &&
			abs_voltage > neonmwi_level &&
				(0 == fxo->neonmwi_last_voltage ||
					(b >= fxo->neonmwi_last_voltage - neonmwi_envelope &&
					 b <= fxo->neonmwi_last_voltage + neonmwi_envelope ))) {
			fxo->neonmwi_last_voltage = b;
			if (NEONMWI_ON_DEBOUNCE == fxo->neonmwi_debounce) {
				fxo->neonmwi_offcounter = neonmwi_offlimit_cycles;
				if(0 == fxo->neonmwi_state) {
					zt_qevent_lock(&wc->chans[card], ZT_EVENT_NEONMWI_ACTIVE);
					fxo->neonmwi_state = 1;
					if (debug)
						printk("NEON MWI active for card %d\n", card+1);
				}
				fxo->neonmwi_debounce++;  /* terminate the processing */
			} else if (NEONMWI_ON_DEBOUNCE > fxo->neonmwi_debounce) {
				fxo->neonmwi_debounce++;
			} else { /* Insure the count gets reset */
				fxo->neonmwi_offcounter = neonmwi_offlimit_cycles;
			}
		} else {
			fxo->neonmwi_debounce = 0;
			fxo->neonmwi_last_voltage = 0;
		}
		/* If no neon mwi pulse for given period of time, indicte no neon mwi state */
		if (fxo->neonmwi_state && 0 < fxo->neonmwi_offcounter ) {
			fxo->neonmwi_offcounter--;
			if (0 == fxo->neonmwi_offcounter) {
				zt_qevent_lock(&wc->chans[card], ZT_EVENT_NEONMWI_INACTIVE);
				fxo->neonmwi_state = 0;
				if (debug)
					printk("NEON MWI cleared for card %d\n", card+1);
			}
		}
	}
#undef MS_PER_CHECK_HOOK
}

static inline void wctdm_proslic_check_hook(struct wctdm *wc, int card)
{
	char res;
	int hook;

	/* For some reason we have to debounce the
	   hook detector.  */

	res = wc->cmdq[card].isrshadow[0];	/* Hook state */
	hook = (res & 1);
	
	if (hook != wc->mods[card].fxs.lastrxhook) {
		/* Reset the debounce (must be multiple of 4ms) */
		wc->mods[card].fxs.debounce = 8 * (4 * 8);
#if 0
		printk("Resetting debounce card %d hook %d, %d\n", card, hook, wc->mods[card].fxs.debounce);
#endif
	} else {
		if (wc->mods[card].fxs.debounce > 0) {
			wc->mods[card].fxs.debounce-= 4 * ZT_CHUNKSIZE;
#if 0
			printk("Sustaining hook %d, %d\n", hook, wc->mods[card].fxs.debounce);
#endif
			if (!wc->mods[card].fxs.debounce) {
#if 0
				printk("Counted down debounce, newhook: %d...\n", hook);
#endif
				wc->mods[card].fxs.debouncehook = hook;
			}
			if (!wc->mods[card].fxs.oldrxhook && wc->mods[card].fxs.debouncehook) {
				/* Off hook */
				if (debug & DEBUG_CARD)
					printk("wctdm: Card %d Going off hook\n", card);
				zt_hooksig(&wc->chans[card], ZT_RXSIG_OFFHOOK);
				if (robust)
					wctdm_init_proslic(wc, card, 1, 0, 1);
				wc->mods[card].fxs.oldrxhook = 1;
			
			} else if (wc->mods[card].fxs.oldrxhook && !wc->mods[card].fxs.debouncehook) {
				/* On hook */
				if (debug & DEBUG_CARD)
					printk("wctdm: Card %d Going on hook\n", card);
				zt_hooksig(&wc->chans[card], ZT_RXSIG_ONHOOK);
				wc->mods[card].fxs.oldrxhook = 0;
			}
		}
	}
	wc->mods[card].fxs.lastrxhook = hook;
}


#ifdef VPM_SUPPORT
static inline void wctdm_vpm_check(struct wctdm *wc, int x)
{
	if (wc->cmdq[x].isrshadow[0]) {
		if (debug & DEBUG_ECHOCAN)
			printk("VPM: Detected dtmf ON channel %02x on chip %d!\n", wc->cmdq[x].isrshadow[0], x - NUM_CARDS);
		wc->sethook[x] = CMD_WR(0xb9, wc->cmdq[x].isrshadow[0]);
		wc->cmdq[x].isrshadow[0] = 0;
		/* Cancel most recent lookup, if there is one */
		wc->cmdq[x].cmds[USER_COMMANDS+0] = 0x00000000; 
	} else if (wc->cmdq[x].isrshadow[1]) {
		if (debug & DEBUG_ECHOCAN)
			printk("VPM: Detected dtmf OFF channel %02x on chip %d!\n", wc->cmdq[x].isrshadow[1], x - NUM_CARDS);
		wc->sethook[x] = CMD_WR(0xbd, wc->cmdq[x].isrshadow[1]);
		wc->cmdq[x].isrshadow[1] = 0;
		/* Cancel most recent lookup, if there is one */
		wc->cmdq[x].cmds[USER_COMMANDS+1] = 0x00000000; 
	}
}

#include "adt_lec.c"

static int wctdm_echocan_with_params(struct zt_chan *chan, struct zt_echocanparams *ecp, struct zt_echocanparam *p)
{
	struct wctdm *wc = chan->pvt;

	if (wc->vpm) {
		int channel;
		int unit;

		channel = (chan->chanpos - 1);
		unit = (chan->chanpos - 1) & 0x3;
		if (wc->vpm < 2)
			channel >>= 2;
	
		if(debug & DEBUG_ECHOCAN) 
			printk("echocan: Unit is %d, Channel is  %d length %d\n", 
				unit, channel, ecp->tap_length);
		if (ecp->tap_length)
			wctdm_vpm_out(wc,unit,channel,0x3e);
		else
			wctdm_vpm_out(wc,unit,channel,0x01);

		return 0;
#ifdef VPM150M_SUPPORT
	} else if (wc->vpm150m) {
		struct vpm150m *vpm150m = wc->vpm150m;
		unsigned int ret;
		int channo = chan->chanpos - 1;

		if ((ret = adt_lec_parse_params(&vpm150m->desiredecstate[channo], ecp, p)))
			return ret;

		vpm150m->desiredecstate[channo].tap_length = ecp->tap_length;

		if (memcmp(&vpm150m->curecstate[channo], &vpm150m->desiredecstate[channo], sizeof(vpm150m->curecstate[channo]))
				&& test_bit(VPM150M_ACTIVE, &vpm150m->control))
			queue_work(vpm150m->wq, &vpm150m->work);

		return 0;
#endif
	} else
		return -ENODEV;
}
#endif

static inline void wctdm_isr_misc(struct wctdm *wc)
{
	int x;

	if (unlikely(!wc->initialized)) {
		return;
	}

	for (x=0;x<wc->cards;x++) {
		if (wc->cardflag & (1 << x)) {
			if (wc->modtype[x] == MOD_TYPE_FXS) {
				if (!(wc->intcount % 10000)) {
					/* Accept an alarm once per 10 seconds */
					if (wc->mods[x].fxs.palarms)
						wc->mods[x].fxs.palarms--;
				}
				wctdm_proslic_check_hook(wc, x);
				if (!(wc->intcount & 0xfc))
					wctdm_proslic_recheck_sanity(wc, x);
				if (wc->mods[x].fxs.lasttxhook == 0x4) {
					/* RINGing, prepare for OHT */
					wc->mods[x].fxs.ohttimer = OHT_TIMER << 3;
					wc->mods[x].fxs.idletxhookstate = 0x2;	/* OHT mode when idle */
				} else {
					if (wc->mods[x].fxs.ohttimer) {
						wc->mods[x].fxs.ohttimer-= ZT_CHUNKSIZE;
						if (!wc->mods[x].fxs.ohttimer) {
							wc->mods[x].fxs.idletxhookstate = 0x1;	/* Switch to active */
							if (wc->mods[x].fxs.lasttxhook == 0x2) {
								/* Apply the change if appropriate */
								wc->mods[x].fxs.lasttxhook = 0x11;
								wc->sethook[x] = CMD_WR(64, wc->mods[x].fxs.lasttxhook);
								/* wctdm_setreg_intr(wc, x, 64, wc->mods[x].fxs.lasttxhook); */
							}
						}
					}
				}
			} else if (wc->modtype[x] == MOD_TYPE_FXO) {
				wctdm_voicedaa_check_hook(wc, x);
			} else if (wc->modtype[x] == MOD_TYPE_QRV) {
				wctdm_qrvdri_check_hook(wc, x);
			}
		}
	}
#ifdef VPM_SUPPORT
	if (wc->vpm > 0) {
		for (x=NUM_CARDS;x<NUM_CARDS+NUM_EC;x++) {
			wctdm_vpm_check(wc, x);
		}
	}
#endif
}

void handle_receive(void* vbb, void* context)
{
	struct wctdm *wc = context;
	wc->rxints++;
	wctdm_receiveprep(wc, vbb);
}

void handle_transmit(void* vbb, void* context)
{
	struct wctdm *wc = context;
	memset(vbb, 0, SFRAME_SIZE);
	wc->txints++;
	wctdm_transmitprep(wc, vbb);
	wctdm_isr_misc(wc);
	wc->intcount++;
	voicebus_transmit(wc->vb, vbb);
}

static int wctdm_voicedaa_insane(struct wctdm *wc, int card)
{
	int blah;
	blah = wctdm_getreg(wc, card, 2);
	if (blah != 0x3)
		return -2;
	blah = wctdm_getreg(wc, card, 11);
	if (debug & DEBUG_CARD)
		printk("VoiceDAA System: %02x\n", blah & 0xf);
	return 0;
}

static int wctdm_proslic_insane(struct wctdm *wc, int card)
{
	int blah,insane_report;
	insane_report=0;

	blah = wctdm_getreg(wc, card, 0);
	if (debug & DEBUG_CARD) 
		printk("ProSLIC on module %d, product %d, version %d\n", card, (blah & 0x30) >> 4, (blah & 0xf));

#if 0
	if ((blah & 0x30) >> 4) {
		printk("ProSLIC on module %d is not a 3210.\n", card);
		return -1;
	}
#endif
	if (((blah & 0xf) == 0) || ((blah & 0xf) == 0xf)) {
		/* SLIC not loaded */
		return -1;
	}
	if ((blah & 0xf) < 2) {
		printk("ProSLIC 3210 version %d is too old\n", blah & 0xf);
		return -1;
	}
	if (wctdm_getreg(wc, card, 1) & 0x80)
		/* ProSLIC 3215, not a 3210 */
		wc->flags[card] |= FLAG_3215;

	blah = wctdm_getreg(wc, card, 8);
	if (blah != 0x2) {
		printk("ProSLIC on module %d insane (1) %d should be 2\n", card, blah);
		return -1;
	} else if ( insane_report)
		printk("ProSLIC on module %d Reg 8 Reads %d Expected is 0x2\n",card,blah);

	blah = wctdm_getreg(wc, card, 64);
	if (blah != 0x0) {
		printk("ProSLIC on module %d insane (2)\n", card);
		return -1;
	} else if ( insane_report)
		printk("ProSLIC on module %d Reg 64 Reads %d Expected is 0x0\n",card,blah);

	blah = wctdm_getreg(wc, card, 11);
	if (blah != 0x33) {
		printk("ProSLIC on module %d insane (3)\n", card);
		return -1;
	} else if ( insane_report)
		printk("ProSLIC on module %d Reg 11 Reads %d Expected is 0x33\n",card,blah);

	/* Just be sure it's setup right. */
	wctdm_setreg(wc, card, 30, 0);

	if (debug & DEBUG_CARD) 
		printk("ProSLIC on module %d seems sane.\n", card);
	return 0;
}

static int wctdm_proslic_powerleak_test(struct wctdm *wc, int card)
{
	unsigned long origjiffies;
	unsigned char vbat;

	/* Turn off linefeed */
	wctdm_setreg(wc, card, 64, 0);

	/* Power down */
	wctdm_setreg(wc, card, 14, 0x10);

	/* Wait for one second */
	origjiffies = jiffies;

	while((vbat = wctdm_getreg(wc, card, 82)) > 0x6) {
		if ((jiffies - origjiffies) >= (HZ/2))
			break;;
	}

	if (vbat < 0x06) {
		printk("Excessive leakage detected on module %d: %d volts (%02x) after %d ms\n", card,
		       376 * vbat / 1000, vbat, (int)((jiffies - origjiffies) * 1000 / HZ));
		return -1;
	} else if (debug & DEBUG_CARD) {
		printk("Post-leakage voltage: %d volts\n", 376 * vbat / 1000);
	}
	return 0;
}

static int wctdm_powerup_proslic(struct wctdm *wc, int card, int fast)
{
	unsigned char vbat;
	unsigned long origjiffies;
	int lim;

	/* Set period of DC-DC converter to 1/64 khz */
	wctdm_setreg(wc, card, 92, 0xc0 /* was 0xff */);

	/* Wait for VBat to powerup */
	origjiffies = jiffies;

	/* Disable powerdown */
	wctdm_setreg(wc, card, 14, 0);

	/* If fast, don't bother checking anymore */
	if (fast)
		return 0;

	while((vbat = wctdm_getreg(wc, card, 82)) < 0xc0) {
		/* Wait no more than 500ms */
		if ((jiffies - origjiffies) > HZ/2) {
			break;
		}
	}

	if (vbat < 0xc0) {
		printk("ProSLIC on module %d failed to powerup within %d ms (%d mV only)\n\n -- DID YOU REMEMBER TO PLUG IN THE HD POWER CABLE TO THE TDM CARD??\n",
		       card, (int)(((jiffies - origjiffies) * 1000 / HZ)),
			vbat * 375);
		return -1;
	} else if (debug & DEBUG_CARD) {
		printk("ProSLIC on module %d powered up to -%d volts (%02x) in %d ms\n",
		       card, vbat * 376 / 1000, vbat, (int)(((jiffies - origjiffies) * 1000 / HZ)));
	}

        /* Proslic max allowed loop current, reg 71 LOOP_I_LIMIT */
        /* If out of range, just set it to the default value     */
        lim = (loopcurrent - 20) / 3;
        if ( loopcurrent > 41 ) {
                lim = 0;
                if (debug & DEBUG_CARD)
                        printk("Loop current out of range! Setting to default 20mA!\n");
        }
        else if (debug & DEBUG_CARD)
                        printk("Loop current set to %dmA!\n",(lim*3)+20);
        wctdm_setreg(wc,card,LOOP_I_LIMIT,lim);

	/* Engage DC-DC converter */
	wctdm_setreg(wc, card, 93, 0x19 /* was 0x19 */);
#if 0
	origjiffies = jiffies;
	while(0x80 & wctdm_getreg(wc, card, 93)) {
		if ((jiffies - origjiffies) > 2 * HZ) {
			printk("Timeout waiting for DC-DC calibration on module %d\n", card);
			return -1;
		}
	}

#if 0
	/* Wait a full two seconds */
	while((jiffies - origjiffies) < 2 * HZ);

	/* Just check to be sure */
	vbat = wctdm_getreg(wc, card, 82);
	printk("ProSLIC on module %d powered up to -%d volts (%02x) in %d ms\n",
		       card, vbat * 376 / 1000, vbat, (int)(((jiffies - origjiffies) * 1000 / HZ)));
#endif
#endif
	return 0;

}

static int wctdm_proslic_manual_calibrate(struct wctdm *wc, int card)
{
	unsigned long origjiffies;
	unsigned char i;

	wctdm_setreg(wc, card, 21, 0);//(0)  Disable all interupts in DR21
	wctdm_setreg(wc, card, 22, 0);//(0)Disable all interupts in DR21
	wctdm_setreg(wc, card, 23, 0);//(0)Disable all interupts in DR21
	wctdm_setreg(wc, card, 64, 0);//(0)

	wctdm_setreg(wc, card, 97, 0x18); //(0x18)Calibrations without the ADC and DAC offset and without common mode calibration.
	wctdm_setreg(wc, card, 96, 0x47); //(0x47)	Calibrate common mode and differential DAC mode DAC + ILIM

	origjiffies=jiffies;
	while( wctdm_getreg(wc,card,96)!=0 ){
		if((jiffies-origjiffies)>80)
			return -1;
	}
//Initialized DR 98 and 99 to get consistant results.
// 98 and 99 are the results registers and the search should have same intial conditions.

/*******************************The following is the manual gain mismatch calibration****************************/
/*******************************This is also available as a function *******************************************/
	// Delay 10ms
	origjiffies=jiffies; 
	while((jiffies-origjiffies)<1);
	wctdm_proslic_setreg_indirect(wc, card, 88,0);
	wctdm_proslic_setreg_indirect(wc,card,89,0);
	wctdm_proslic_setreg_indirect(wc,card,90,0);
	wctdm_proslic_setreg_indirect(wc,card,91,0);
	wctdm_proslic_setreg_indirect(wc,card,92,0);
	wctdm_proslic_setreg_indirect(wc,card,93,0);

	wctdm_setreg(wc, card, 98,0x10); // This is necessary if the calibration occurs other than at reset time
	wctdm_setreg(wc, card, 99,0x10);

	for ( i=0x1f; i>0; i--)
	{
		wctdm_setreg(wc, card, 98,i);
		origjiffies=jiffies; 
		while((jiffies-origjiffies)<4);
		if((wctdm_getreg(wc,card,88)) == 0)
			break;
	} // for

	for ( i=0x1f; i>0; i--)
	{
		wctdm_setreg(wc, card, 99,i);
		origjiffies=jiffies; 
		while((jiffies-origjiffies)<4);
		if((wctdm_getreg(wc,card,89)) == 0)
			break;
	}//for

/*******************************The preceding is the manual gain mismatch calibration****************************/
/**********************************The following is the longitudinal Balance Cal***********************************/
	wctdm_setreg(wc,card,64,1);
	while((jiffies-origjiffies)<10); // Sleep 100?

	wctdm_setreg(wc, card, 64, 0);
	wctdm_setreg(wc, card, 23, 0x4);  // enable interrupt for the balance Cal
	wctdm_setreg(wc, card, 97, 0x1); // this is a singular calibration bit for longitudinal calibration
	wctdm_setreg(wc, card, 96,0x40);

	wctdm_getreg(wc,card,96); /* Read Reg 96 just cause */

	wctdm_setreg(wc, card, 21, 0xFF);
	wctdm_setreg(wc, card, 22, 0xFF);
	wctdm_setreg(wc, card, 23, 0xFF);

	/**The preceding is the longitudinal Balance Cal***/
	return(0);

}

static int wctdm_proslic_calibrate(struct wctdm *wc, int card)
{
	unsigned long origjiffies;
	int x;
	/* Perform all calibrations */
	wctdm_setreg(wc, card, 97, 0x1f);
	
	/* Begin, no speedup */
	wctdm_setreg(wc, card, 96, 0x5f);

	/* Wait for it to finish */
	origjiffies = jiffies;
	while(wctdm_getreg(wc, card, 96)) {
		if ((jiffies - origjiffies) > 2 * HZ) {
			printk("Timeout waiting for calibration of module %d\n", card);
			return -1;
		}
	}
	
	if (debug & DEBUG_CARD) {
		/* Print calibration parameters */
		printk("Calibration Vector Regs 98 - 107: \n");
		for (x=98;x<108;x++) {
			printk("%d: %02x\n", x, wctdm_getreg(wc, card, x));
		}
	}
	return 0;
}

static void wait_just_a_bit(int foo)
{
	long newjiffies;
	newjiffies = jiffies + foo;
	while(jiffies < newjiffies);
}

/*********************************************************************
 * Set the hwgain on the analog modules
 *
 * card = the card position for this module (0-23)
 * gain = gain in dB x10 (e.g. -3.5dB  would be gain=-35)
 * tx = (0 for rx; 1 for tx)
 *
 *******************************************************************/
static int wctdm_set_hwgain(struct wctdm *wc, int card, __s32 gain, __u32 tx)
{
	if (!(wc->modtype[card] == MOD_TYPE_FXO)) {
		printk("Cannot adjust gain.  Unsupported module type!\n");
		return -1;
	}
	if (tx) {
		if (debug)
			printk("setting FXO tx gain for card=%d to %d\n", card, gain);
		if (gain >=  -150 && gain <= 0) {
			wctdm_setreg(wc, card, 38, 16 + (gain/-10));
			wctdm_setreg(wc, card, 40, 16 + (-gain%10));
		} else if (gain <= 120 && gain > 0) {
			wctdm_setreg(wc, card, 38, gain/10);
			wctdm_setreg(wc, card, 40, (gain%10));
		} else {
			printk("FXO tx gain is out of range (%d)\n", gain);
			return -1;
		}
	} else { /* rx */
		if (debug)
			printk("setting FXO rx gain for card=%d to %d\n", card, gain);
		if (gain >=  -150 && gain <= 0) {
			wctdm_setreg(wc, card, 39, 16+ (gain/-10));
			wctdm_setreg(wc, card, 41, 16 + (-gain%10));
		} else if (gain <= 120 && gain > 0) {
			wctdm_setreg(wc, card, 39, gain/10);
			wctdm_setreg(wc, card, 41, (gain%10));
		} else {
			printk("FXO rx gain is out of range (%d)\n", gain);
			return -1;
		}
	}

	return 0;
}

static int wctdm_init_voicedaa(struct wctdm *wc, int card, int fast, int manual, int sane)
{
	unsigned char reg16=0, reg26=0, reg30=0, reg31=0;
	long newjiffies;

	if (wc->modtype[card & 0xfc] == MOD_TYPE_QRV) return -2;

	wc->modtype[card] = MOD_TYPE_NONE;
	/* Wait just a bit */
	wait_just_a_bit(HZ/10);

	wc->modtype[card] = MOD_TYPE_FXO;
	wait_just_a_bit(HZ/10);

	if (!sane && wctdm_voicedaa_insane(wc, card))
		return -2;

	/* Software reset */
	wctdm_setreg(wc, card, 1, 0x80);

	/* Wait just a bit */
	wait_just_a_bit(HZ/10);

	/* Enable PCM, ulaw */
	if (alawoverride)
		wctdm_setreg(wc, card, 33, 0x20);
	else
		wctdm_setreg(wc, card, 33, 0x28);

	/* Set On-hook speed, Ringer impedence, and ringer threshold */
	reg16 |= (fxo_modes[_opermode].ohs << 6);
	reg16 |= (fxo_modes[_opermode].rz << 1);
	reg16 |= (fxo_modes[_opermode].rt);
	wctdm_setreg(wc, card, 16, reg16);

	if(fwringdetect || neonmwi_monitor) {
		/* Enable ring detector full-wave rectifier mode */
		wctdm_setreg(wc, card, 18, 2);
		wctdm_setreg(wc, card, 24, 0);
	} else { 
		/* Set to the device defaults */
		wctdm_setreg(wc, card, 18, 0);
		wctdm_setreg(wc, card, 24, 0x19);
	}
	
	/* Enable ring detector full-wave rectifier mode */
	wctdm_setreg(wc, card, 18, 2);
	wctdm_setreg(wc, card, 24, 0);
	
	/* Set DC Termination:
	   Tip/Ring voltage adjust, minimum operational current, current limitation */
	reg26 |= (fxo_modes[_opermode].dcv << 6);
	reg26 |= (fxo_modes[_opermode].mini << 4);
	reg26 |= (fxo_modes[_opermode].ilim << 1);
	wctdm_setreg(wc, card, 26, reg26);

	/* Set AC Impedence */
	reg30 = (fxo_modes[_opermode].acim);
	wctdm_setreg(wc, card, 30, reg30);

	/* Misc. DAA parameters */
	reg31 = 0xa3;
	reg31 |= (fxo_modes[_opermode].ohs2 << 3);
	wctdm_setreg(wc, card, 31, reg31);

	/* Set Transmit/Receive timeslot */
	wctdm_setreg(wc, card, 34, (card * 8) & 0xff);
	wctdm_setreg(wc, card, 35, (card * 8) >> 8);
	wctdm_setreg(wc, card, 36, (card * 8) & 0xff);
	wctdm_setreg(wc, card, 37, (card * 8) >> 8);

	/* Enable ISO-Cap */
	wctdm_setreg(wc, card, 6, 0x00);

	/* Wait 1000ms for ISO-cap to come up */
	newjiffies = jiffies;
	newjiffies += 2 * HZ;
	while((jiffies < newjiffies) && !(wctdm_getreg(wc, card, 11) & 0xf0))
		wait_just_a_bit(HZ/10);

	if (!(wctdm_getreg(wc, card, 11) & 0xf0)) {
		printk("VoiceDAA did not bring up ISO link properly!\n");
		return -1;
	}
	if (debug & DEBUG_CARD)
		printk("ISO-Cap is now up, line side: %02x rev %02x\n", 
		       wctdm_getreg(wc, card, 11) >> 4,
		       (wctdm_getreg(wc, card, 13) >> 2) & 0xf);
	/* Enable on-hook line monitor */
	wctdm_setreg(wc, card, 5, 0x08);
	
	/* Take values for fxotxgain and fxorxgain and apply them to module */
	wctdm_set_hwgain(wc, card, fxotxgain, 1);
	wctdm_set_hwgain(wc, card, fxorxgain, 0);

	if(debug)
		printk("DEBUG fxotxgain:%i.%i fxorxgain:%i.%i\n", (wctdm_getreg(wc, card, 38)/16) ? -(wctdm_getreg(wc, card, 38) - 16) : wctdm_getreg(wc, card, 38), (wctdm_getreg(wc, card, 40)/16) ? -(wctdm_getreg(wc, card, 40) - 16) : wctdm_getreg(wc, card, 40), (wctdm_getreg(wc, card, 39)/16) ? -(wctdm_getreg(wc, card, 39) - 16): wctdm_getreg(wc, card, 39), (wctdm_getreg(wc, card, 41)/16)?-(wctdm_getreg(wc, card, 41) - 16) : wctdm_getreg(wc, card, 41));
	
	return 0;
		
}

static int wctdm_init_proslic(struct wctdm *wc, int card, int fast, int manual, int sane)
{

	unsigned short tmp[5];
	unsigned char r19,r9;
	int x;
	int fxsmode=0;

	if (wc->modtype[card & 0xfc] == MOD_TYPE_QRV) return -2;

	/* Sanity check the ProSLIC */
	if (!sane && wctdm_proslic_insane(wc, card))
		return -2;

	/* By default, don't send on hook */
	wc->mods[card].fxs.idletxhookstate = 1;
	wc->mods[card].fxs.lasttxhook = 0x10;

	if (sane) {
		/* Make sure we turn off the DC->DC converter to prevent anything from blowing up */
		wctdm_setreg(wc, card, 14, 0x10);
	}

	if (wctdm_proslic_init_indirect_regs(wc, card)) {
		printk(KERN_INFO "Indirect Registers failed to initialize on module %d.\n", card);
		return -1;
	}

	/* Clear scratch pad area */
	wctdm_proslic_setreg_indirect(wc, card, 97,0);

	/* Clear digital loopback */
	wctdm_setreg(wc, card, 8, 0);

	/* Revision C optimization */
	wctdm_setreg(wc, card, 108, 0xeb);

	/* Disable automatic VBat switching for safety to prevent
	   Q7 from accidently turning on and burning out. */
	wctdm_setreg(wc, card, 67, 0x07); /* If pulse dialing has trouble at high REN
					     loads change this to 0x17 */

	/* Turn off Q7 */
	wctdm_setreg(wc, card, 66, 1);

	/* Flush ProSLIC digital filters by setting to clear, while
	   saving old values */
	for (x=0;x<5;x++) {
		tmp[x] = wctdm_proslic_getreg_indirect(wc, card, x + 35);
		wctdm_proslic_setreg_indirect(wc, card, x + 35, 0x8000);
	}

	/* Power up the DC-DC converter */
	if (wctdm_powerup_proslic(wc, card, fast)) {
		printk("Unable to do INITIAL ProSLIC powerup on module %d\n", card);
		return -1;
	}

	if (!fast) {

		/* Check for power leaks */
		if (wctdm_proslic_powerleak_test(wc, card)) {
			printk("ProSLIC module %d failed leakage test.  Check for short circuit\n", card);
		}
		/* Power up again */
		if (wctdm_powerup_proslic(wc, card, fast)) {
			printk("Unable to do FINAL ProSLIC powerup on module %d\n", card);
			return -1;
		}
#ifndef NO_CALIBRATION
		/* Perform calibration */
		if(manual) {
			if (wctdm_proslic_manual_calibrate(wc, card)) {
				//printk("Proslic failed on Manual Calibration\n");
				if (wctdm_proslic_manual_calibrate(wc, card)) {
					printk("Proslic Failed on Second Attempt to Calibrate Manually. (Try -DNO_CALIBRATION in Makefile)\n");
					return -1;
				}
				printk("Proslic Passed Manual Calibration on Second Attempt\n");
			}
		}
		else {
			if(wctdm_proslic_calibrate(wc, card))  {
				//printk("ProSlic died on Auto Calibration.\n");
				if (wctdm_proslic_calibrate(wc, card)) {
					printk("Proslic Failed on Second Attempt to Auto Calibrate\n");
					return -1;
				}
				printk("Proslic Passed Auto Calibration on Second Attempt\n");
			}
		}
		/* Perform DC-DC calibration */
		wctdm_setreg(wc, card, 93, 0x99);
		r19 = wctdm_getreg(wc, card, 107);
		if ((r19 < 0x2) || (r19 > 0xd)) {
			printk("DC-DC cal has a surprising direct 107 of 0x%02x!\n", r19);
			wctdm_setreg(wc, card, 107, 0x8);
		}

		/* Save calibration vectors */
		for (x=0;x<NUM_CAL_REGS;x++)
			wc->mods[card].fxs.calregs.vals[x] = wctdm_getreg(wc, card, 96 + x);
#endif

	} else {
		/* Restore calibration registers */
		for (x=0;x<NUM_CAL_REGS;x++)
			wctdm_setreg(wc, card, 96 + x, wc->mods[card].fxs.calregs.vals[x]);
	}
	/* Calibration complete, restore original values */
	for (x=0;x<5;x++) {
		wctdm_proslic_setreg_indirect(wc, card, x + 35, tmp[x]);
	}

	if (wctdm_proslic_verify_indirect_regs(wc, card)) {
		printk(KERN_INFO "Indirect Registers failed verification.\n");
		return -1;
	}


#if 0
    /* Disable Auto Power Alarm Detect and other "features" */
    wctdm_setreg(wc, card, 67, 0x0e);
    blah = wctdm_getreg(wc, card, 67);
#endif

#if 0
    if (wctdm_proslic_setreg_indirect(wc, card, 97, 0x0)) { // Stanley: for the bad recording fix
		 printk(KERN_INFO "ProSlic IndirectReg Died.\n");
		 return -1;
	}
#endif

    if (alawoverride)
    	wctdm_setreg(wc, card, 1, 0x20);
    else
    	wctdm_setreg(wc, card, 1, 0x28);
 	// U-Law 8-bit interface
    wctdm_setreg(wc, card, 2, (card * 8) & 0xff);    // Tx Start count low byte  0
    wctdm_setreg(wc, card, 3, (card * 8) >> 8);    // Tx Start count high byte 0
    wctdm_setreg(wc, card, 4, (card * 8) & 0xff);    // Rx Start count low byte  0
    wctdm_setreg(wc, card, 5, (card * 8) >> 8);    // Rx Start count high byte 0
    wctdm_setreg(wc, card, 18, 0xff);     // clear all interrupt
    wctdm_setreg(wc, card, 19, 0xff);
    wctdm_setreg(wc, card, 20, 0xff);
    wctdm_setreg(wc, card, 22, 0xff);
    wctdm_setreg(wc, card, 73, 0x04);
	if (fxshonormode) {
		fxsmode = acim2tiss[fxo_modes[_opermode].acim];
		wctdm_setreg(wc, card, 10, 0x08 | fxsmode);
		if (fxo_modes[_opermode].ring_osc)
			wctdm_proslic_setreg_indirect(wc, card, 20, fxo_modes[_opermode].ring_osc);
		if (fxo_modes[_opermode].ring_x)
			wctdm_proslic_setreg_indirect(wc, card, 21, fxo_modes[_opermode].ring_x);
	}
    if (lowpower)
    	wctdm_setreg(wc, card, 72, 0x10);

#if 0
    wctdm_setreg(wc, card, 21, 0x00); 	// enable interrupt
    wctdm_setreg(wc, card, 22, 0x02); 	// Loop detection interrupt
    wctdm_setreg(wc, card, 23, 0x01); 	// DTMF detection interrupt
#endif

#if 0
    /* Enable loopback */
    wctdm_setreg(wc, card, 8, 0x2);
    wctdm_setreg(wc, card, 14, 0x0);
    wctdm_setreg(wc, card, 64, 0x0);
    wctdm_setreg(wc, card, 1, 0x08);
#endif

	if (fastringer) {
		/* Speed up Ringer */
		wctdm_proslic_setreg_indirect(wc, card, 20, 0x7e6d);
		wctdm_proslic_setreg_indirect(wc, card, 21, 0x01b9);
		/* Beef up Ringing voltage to 89V */
		if (boostringer) {
			wctdm_setreg(wc, card, 74, 0x3f);
			if (wctdm_proslic_setreg_indirect(wc, card, 21, 0x247)) 
				return -1;
			printk("Boosting fast ringer on slot %d (89V peak)\n", card + 1);
		} else if (lowpower) {
			if (wctdm_proslic_setreg_indirect(wc, card, 21, 0x14b)) 
				return -1;
			printk("Reducing fast ring power on slot %d (50V peak)\n", card + 1);
		} else
			printk("Speeding up ringer on slot %d (25Hz)\n", card + 1);
	} else {
		/* Beef up Ringing voltage to 89V */
		if (boostringer) {
			wctdm_setreg(wc, card, 74, 0x3f);
			if (wctdm_proslic_setreg_indirect(wc, card, 21, 0x1d1)) 
				return -1;
			printk("Boosting ringer on slot %d (89V peak)\n", card + 1);
		} else if (lowpower) {
			if (wctdm_proslic_setreg_indirect(wc, card, 21, 0x108)) 
				return -1;
			printk("Reducing ring power on slot %d (50V peak)\n", card + 1);
		}
	}

	if (fxstxgain || fxsrxgain) {
		r9 = wctdm_getreg(wc, card, 9);
		switch (fxstxgain) {
		
			case 35:
				r9+=8;
				break;
			case -35:
				r9+=4;
				break;
			case 0: 
				break;
		}
	
		switch (fxsrxgain) {
			
			case 35:
				r9+=2;
				break;
			case -35:
				r9+=1;
				break;
			case 0:
				break;
		}
		wctdm_setreg(wc, card, 9, r9);
	}

	if (debug)
		printk("DEBUG: fxstxgain:%s fxsrxgain:%s\n",((wctdm_getreg(wc, card, 9)/8) == 1)?"3.5":(((wctdm_getreg(wc,card,9)/4) == 1)?"-3.5":"0.0"),((wctdm_getreg(wc, card, 9)/2) == 1)?"3.5":((wctdm_getreg(wc,card,9)%2)?"-3.5":"0.0"));

	wc->mods[card].fxs.lasttxhook = 0x11;
	wctdm_setreg(wc, card, 64, 0x01);
	return 0;
}

static int wctdm_init_qrvdri(struct wctdm *wc, int card)
{
	unsigned char x,y;
	unsigned long endjif;

	/* have to set this, at least for now */
	wc->modtype[card] = MOD_TYPE_QRV;
	if (!(card & 3)) /* if at base of card, reset and write it */
	{
		wctdm_setreg(wc,card,0,0x80); 
		wctdm_setreg(wc,card,0,0x55);
		wctdm_setreg(wc,card,1,0x69);
		wc->qrvhook[card] = wc->qrvhook[card + 1] = 0;
		wc->qrvhook[card + 2] = wc->qrvhook[card + 3] = 0xff;
		wc->debouncetime[card] = wc->debouncetime[card + 1] = QRV_DEBOUNCETIME;
		wc->qrvdebtime[card] = wc->qrvdebtime[card + 1] = 0;
		wc->radmode[card] = wc->radmode[card + 1] = 0;
		wc->txgain[card] = wc->txgain[card + 1] = 3599;
		wc->rxgain[card] = wc->rxgain[card + 1] = 1199;
	} else { /* channel is on same card as base, no need to test */
		if (wc->modtype[card & 0x7c] == MOD_TYPE_QRV) 
		{
			/* only lower 2 are valid */
			if (!(card & 2)) return 0;
		}
		wc->modtype[card] = MOD_TYPE_NONE;
		return 1;
	}
	x = wctdm_getreg(wc,card,0);
	y = wctdm_getreg(wc,card,1);
	/* if not a QRV card, return as such */
	if ((x != 0x55) || (y != 0x69))
	{
		wc->modtype[card] = MOD_TYPE_NONE;
		return 1;
	}
	for(x = 0; x < 0x30; x++)
	{
		if ((x >= 0x1c) && (x <= 0x1e)) wctdm_setreg(wc,card,x,0xff);
		else wctdm_setreg(wc,card,x,0);
	}
	wctdm_setreg(wc,card,0,0x80); 
	endjif = jiffies + (HZ/10);
	while(endjif > jiffies);
	wctdm_setreg(wc,card,0,0x10); 
	wctdm_setreg(wc,card,0,0x10); 
	endjif = jiffies + (HZ/10);
	while(endjif > jiffies);
	/* set up modes */
	wctdm_setreg(wc,card,0,0x1c); 
	/* set up I/O directions */
	wctdm_setreg(wc,card,1,0x33); 
	wctdm_setreg(wc,card,2,0x0f); 
	wctdm_setreg(wc,card,5,0x0f); 
	/* set up I/O to quiescent state */
	wctdm_setreg(wc,card,3,0x11);  /* D0-7 */
	wctdm_setreg(wc,card,4,0xa);  /* D8-11 */
	wctdm_setreg(wc,card,7,0);  /* CS outputs */
	/* set up timeslots */
	wctdm_setreg(wc,card,0x13,card + 0x80);  /* codec 2 tx, ts0 */
	wctdm_setreg(wc,card,0x17,card + 0x80);  /* codec 0 rx, ts0 */
	wctdm_setreg(wc,card,0x14,card + 0x81);  /* codec 1 tx, ts1 */
	wctdm_setreg(wc,card,0x18,card + 0x81);  /* codec 1 rx, ts1 */
	/* set up for max gains */
	wctdm_setreg(wc,card,0x26,0x24); 
	wctdm_setreg(wc,card,0x27,0x24); 
	wctdm_setreg(wc,card,0x0b,0x01);  /* "Transmit" gain codec 0 */
	wctdm_setreg(wc,card,0x0c,0x01);  /* "Transmit" gain codec 1 */
	wctdm_setreg(wc,card,0x0f,0xff);  /* "Receive" gain codec 0 */
	wctdm_setreg(wc,card,0x10,0xff);  /* "Receive" gain codec 1 */
	return 0;
}

static void qrv_dosetup(struct zt_chan *chan,struct wctdm *wc)
{
int qrvcard;
unsigned char r;
long l;

	/* actually do something with the values */
	qrvcard = (chan->chanpos - 1) & 0xfc;
	if (debug) printk("@@@@@ radmodes: %d,%d  rxgains: %d,%d   txgains: %d,%d\n",
	wc->radmode[qrvcard],wc->radmode[qrvcard + 1],
		wc->rxgain[qrvcard],wc->rxgain[qrvcard + 1],
			wc->txgain[qrvcard],wc->txgain[qrvcard + 1]);
	r = 0;
	if (wc->radmode[qrvcard] & RADMODE_DEEMP) r |= 4;		
	if (wc->radmode[qrvcard + 1] & RADMODE_DEEMP) r |= 8;		
	if (wc->rxgain[qrvcard] < 1200) r |= 1;
	if (wc->rxgain[qrvcard + 1] < 1200) r |= 2;
	wctdm_setreg(wc, qrvcard, 7, r);
	if (debug) printk("@@@@@ setting reg 7 to %02x hex\n",r);
	r = 0;
	if (wc->radmode[qrvcard] & RADMODE_PREEMP) r |= 3;
	else if (wc->txgain[qrvcard] >= 3600) r |= 1;
	else if (wc->txgain[qrvcard] >= 1200) r |= 2;
	if (wc->radmode[qrvcard + 1] & RADMODE_PREEMP) r |= 0xc;
	else if (wc->txgain[qrvcard + 1] >= 3600) r |= 4;
	else if (wc->txgain[qrvcard + 1] >= 1200) r |= 8;
	wctdm_setreg(wc, qrvcard, 4, r);
	if (debug) printk("@@@@@ setting reg 4 to %02x hex\n",r);
	r = 0;
	if (wc->rxgain[qrvcard] >= 2400) r |= 1; 
	if (wc->rxgain[qrvcard + 1] >= 2400) r |= 2; 
	wctdm_setreg(wc, qrvcard, 0x25, r);
	if (debug) printk("@@@@@ setting reg 0x25 to %02x hex\n",r);
	r = 0;
	if (wc->txgain[qrvcard] < 2400) r |= 1; else r |= 4;
	if (wc->txgain[qrvcard + 1] < 2400) r |= 8; else r |= 0x20;
	wctdm_setreg(wc, qrvcard, 0x26, r);
	if (debug) printk("@@@@@ setting reg 0x26 to %02x hex\n",r);
	l = ((long)(wc->rxgain[qrvcard] % 1200) * 10000) / 46875;
	if (l == 0) l = 1;
	if (wc->rxgain[qrvcard] >= 2400) l += 181;
	wctdm_setreg(wc, qrvcard, 0x0b, (unsigned char)l);
	if (debug) printk("@@@@@ setting reg 0x0b to %02x hex\n",(unsigned char)l);
	l = ((long)(wc->rxgain[qrvcard + 1] % 1200) * 10000) / 46875;
	if (l == 0) l = 1;
	if (wc->rxgain[qrvcard + 1] >= 2400) l += 181;
	wctdm_setreg(wc, qrvcard, 0x0c, (unsigned char)l);
	if (debug) printk("@@@@@ setting reg 0x0c to %02x hex\n",(unsigned char)l);
	l = ((long)(wc->txgain[qrvcard] % 1200) * 10000) / 46875;
	if (l == 0) l = 1;
	wctdm_setreg(wc, qrvcard, 0x0f, (unsigned char)l);
	if (debug) printk("@@@@@ setting reg 0x0f to %02x hex\n", (unsigned char)l);
	l = ((long)(wc->txgain[qrvcard + 1] % 1200) * 10000) / 46875;
	if (l == 0) l = 1;
	wctdm_setreg(wc, qrvcard, 0x10,(unsigned char)l);
	if (debug) printk("@@@@@ setting reg 0x10 to %02x hex\n",(unsigned char)l);
	return;
}

static int wctdm_ioctl(struct zt_chan *chan, unsigned int cmd, unsigned long data)
{
	struct wctdm_stats stats;
	struct wctdm_regs regs;
	struct wctdm_regop regop;
	struct wctdm_echo_coefs echoregs;
	struct zt_hwgain hwgain;
	struct wctdm *wc = chan->pvt;
	int x;
	union {
		struct zt_radio_stat s;
		struct zt_radio_param p;
	} stack;

	switch (cmd) {
	case ZT_ONHOOKTRANSFER:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
			return -EINVAL;
		if (get_user(x, (int *)data))
			return -EFAULT;
		wc->mods[chan->chanpos - 1].fxs.ohttimer = x << 3;
		wc->mods[chan->chanpos - 1].fxs.idletxhookstate = 0x2;	/* OHT mode when idle */
		if (wc->mods[chan->chanpos - 1].fxs.lasttxhook == 0x1) {
			/* Apply the change if appropriate */
			wc->mods[chan->chanpos - 1].fxs.lasttxhook = 0x12;
			wc->sethook[chan->chanpos - 1] = CMD_WR(64, wc->mods[chan->chanpos - 1].fxs.lasttxhook);
			/* wctdm_setreg(wc, chan->chanpos - 1, 64, wc->mods[chan->chanpos - 1].fxs.lasttxhook); */
		}
		break;
	case WCTDM_GET_STATS:
		if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXS) {
			stats.tipvolt = wctdm_getreg(wc, chan->chanpos - 1, 80) * -376;
			stats.ringvolt = wctdm_getreg(wc, chan->chanpos - 1, 81) * -376;
			stats.batvolt = wctdm_getreg(wc, chan->chanpos - 1, 82) * -376;
		} else if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXO) {
			stats.tipvolt = (signed char)wctdm_getreg(wc, chan->chanpos - 1, 29) * 1000;
			stats.ringvolt = (signed char)wctdm_getreg(wc, chan->chanpos - 1, 29) * 1000;
			stats.batvolt = (signed char)wctdm_getreg(wc, chan->chanpos - 1, 29) * 1000;
		} else 
			return -EINVAL;
		if (copy_to_user((struct wctdm_stats *)data, &stats, sizeof(stats)))
			return -EFAULT;
		break;
	case WCTDM_GET_REGS:
		if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXS) {
			for (x=0;x<NUM_INDIRECT_REGS;x++)
				regs.indirect[x] = wctdm_proslic_getreg_indirect(wc, chan->chanpos -1, x);
			for (x=0;x<NUM_REGS;x++)
				regs.direct[x] = wctdm_getreg(wc, chan->chanpos - 1, x);
		} else if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_QRV) {
			memset(&regs, 0, sizeof(regs));
			for (x=0;x<0x32;x++)
				regs.direct[x] = wctdm_getreg(wc, chan->chanpos - 1, x);
		} else {
			memset(&regs, 0, sizeof(regs));
			for (x=0;x<NUM_FXO_REGS;x++)
				regs.direct[x] = wctdm_getreg(wc, chan->chanpos - 1, x);
		}
		if (copy_to_user((struct wctdm_regs *)data, &regs, sizeof(regs)))
			return -EFAULT;
		break;
	case WCTDM_SET_REG:
		if (copy_from_user(&regop, (struct wctdm_regop *)data, sizeof(regop)))
			return -EFAULT;
		if (regop.indirect) {
			if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
				return -EINVAL;
			printk("Setting indirect %d to 0x%04x on %d\n", regop.reg, regop.val, chan->chanpos);
			wctdm_proslic_setreg_indirect(wc, chan->chanpos - 1, regop.reg, regop.val);
		} else {
			regop.val &= 0xff;
			if (regop.reg == 64)
				wc->mods[chan->chanpos-1].fxs.lasttxhook = (regop.val & 0x0f) |  0x10;
			
			printk("Setting direct %d to %04x on %d\n", regop.reg, regop.val, chan->chanpos);
			wctdm_setreg(wc, chan->chanpos - 1, regop.reg, regop.val);
		}
		break;
	case WCTDM_SET_ECHOTUNE:
		printk("-- Setting echo registers: \n");
		if (copy_from_user(&echoregs, (struct wctdm_echo_coefs*)data, sizeof(echoregs)))
			return -EFAULT;

		if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXO) {
			/* Set the ACIM register */
			wctdm_setreg(wc, chan->chanpos - 1, 30, echoregs.acim);

			/* Set the digital echo canceller registers */
			wctdm_setreg(wc, chan->chanpos - 1, 45, echoregs.coef1);
			wctdm_setreg(wc, chan->chanpos - 1, 46, echoregs.coef2);
			wctdm_setreg(wc, chan->chanpos - 1, 47, echoregs.coef3);
			wctdm_setreg(wc, chan->chanpos - 1, 48, echoregs.coef4);
			wctdm_setreg(wc, chan->chanpos - 1, 49, echoregs.coef5);
			wctdm_setreg(wc, chan->chanpos - 1, 50, echoregs.coef6);
			wctdm_setreg(wc, chan->chanpos - 1, 51, echoregs.coef7);
			wctdm_setreg(wc, chan->chanpos - 1, 52, echoregs.coef8);

			printk("-- Set echo registers successfully\n");

			break;
		} else {
			return -EINVAL;

		}
		break;
	case ZT_SET_HWGAIN:
		if (copy_from_user(&hwgain, (struct zt_hwgain*) data, sizeof(hwgain)))
			return -EFAULT;

		wctdm_set_hwgain(wc, chan->chanpos-1, hwgain.newgain, hwgain.tx);

		if (debug)
			printk("Setting hwgain on channel %d to %d for %s direction\n", 
				chan->chanpos-1, hwgain.newgain, hwgain.tx ? "tx" : "rx");
		break;
#ifdef VPM_SUPPORT
	case ZT_TONEDETECT:
		if (get_user(x, (int *) data))
			return -EFAULT;
		if (!wc->vpm && !wc->vpm150m)
			return -ENOSYS;
		if ((wc->vpm || wc->vpm150m) && (x && !vpmdtmfsupport))
			return -ENOSYS;
		if (x & ZT_TONEDETECT_ON) {
			set_bit(chan->chanpos - 1, &wc->dtmfmask);
		} else {
			clear_bit(chan->chanpos - 1, &wc->dtmfmask);
		}
		if (x & ZT_TONEDETECT_MUTE) {
			if (wc->vpm150m) {
				set_bit(chan->chanpos - 1, &wc->vpm150m->desireddtmfmutestate);
			}
		} else {
			if (wc->vpm150m) {
				clear_bit(chan->chanpos - 1, &wc->vpm150m->desireddtmfmutestate);
			}
		}
		return 0;
#endif
	case ZT_RADIO_GETPARAM:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_QRV) 
			return -ENOTTY;
		if (copy_from_user(&stack.p,(struct zt_radio_param *)data,sizeof(struct zt_radio_param))) return -EFAULT;
		stack.p.data = 0; /* start with 0 value in output */
		switch(stack.p.radpar) {
		case ZT_RADPAR_INVERTCOR:
			if (wc->radmode[chan->chanpos - 1] & RADMODE_INVERTCOR)
				stack.p.data = 1;
			break;
		case ZT_RADPAR_IGNORECOR:
			if (wc->radmode[chan->chanpos - 1] & RADMODE_IGNORECOR)
				stack.p.data = 1;
			break;
		case ZT_RADPAR_IGNORECT:
			if (wc->radmode[chan->chanpos - 1] & RADMODE_IGNORECT)
				stack.p.data = 1;
			break;
		case ZT_RADPAR_EXTRXTONE:
			stack.p.data = 0;
			if (wc->radmode[chan->chanpos - 1] & RADMODE_EXTTONE)
			{
				stack.p.data = 1;
				if (wc->radmode[chan->chanpos - 1] & RADMODE_EXTINVERT)
				{
					stack.p.data = 2;
				}
			}
			break;
		case ZT_RADPAR_DEBOUNCETIME:
			stack.p.data = wc->debouncetime[chan->chanpos - 1];
			break;
		case ZT_RADPAR_RXGAIN:
			stack.p.data = wc->rxgain[chan->chanpos - 1] - 1199;
			break;
		case ZT_RADPAR_TXGAIN:
			stack.p.data = wc->txgain[chan->chanpos - 1] - 3599;
			break;
		case ZT_RADPAR_DEEMP:
			stack.p.data = 0;
			if (wc->radmode[chan->chanpos - 1] & RADMODE_DEEMP)
			{
				stack.p.data = 1;
			}
			break;
		case ZT_RADPAR_PREEMP:
			stack.p.data = 0;
			if (wc->radmode[chan->chanpos - 1] & RADMODE_PREEMP)
			{
				stack.p.data = 1;
			}
			break;
		default:
			return -EINVAL;
		}
		if (copy_to_user((struct zt_radio_param *)data,&stack.p,sizeof(struct zt_radio_param))) return -EFAULT;
		break;
	case ZT_RADIO_SETPARAM:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_QRV) 
			return -ENOTTY;
		if (copy_from_user(&stack.p,(struct zt_radio_param *)data,sizeof(struct zt_radio_param))) return -EFAULT;
		switch(stack.p.radpar) {
		case ZT_RADPAR_INVERTCOR:
			if (stack.p.data)
				wc->radmode[chan->chanpos - 1] |= RADMODE_INVERTCOR;
			else
				wc->radmode[chan->chanpos - 1] &= ~RADMODE_INVERTCOR;
			return 0;
		case ZT_RADPAR_IGNORECOR:
			if (stack.p.data)
				wc->radmode[chan->chanpos - 1] |= RADMODE_IGNORECOR;
			else
				wc->radmode[chan->chanpos - 1] &= ~RADMODE_IGNORECOR;
			return 0;
		case ZT_RADPAR_IGNORECT:
			if (stack.p.data)
				wc->radmode[chan->chanpos - 1] |= RADMODE_IGNORECT;
			else
				wc->radmode[chan->chanpos - 1] &= ~RADMODE_IGNORECT;
			return 0;
		case ZT_RADPAR_EXTRXTONE:
			if (stack.p.data)
				wc->radmode[chan->chanpos - 1] |= RADMODE_EXTTONE;
			else
				wc->radmode[chan->chanpos - 1] &= ~RADMODE_EXTTONE;
			if (stack.p.data > 1)
				wc->radmode[chan->chanpos - 1] |= RADMODE_EXTINVERT;
			else
				wc->radmode[chan->chanpos - 1] &= ~RADMODE_EXTINVERT;
			return 0;
		case ZT_RADPAR_DEBOUNCETIME:
			wc->debouncetime[chan->chanpos - 1] = stack.p.data;
			return 0;
		case ZT_RADPAR_RXGAIN:
			/* if out of range */
			if ((stack.p.data <= -1200) || (stack.p.data > 1552))
			{
				return -EINVAL;
			}
			wc->rxgain[chan->chanpos - 1] = stack.p.data + 1199;
			break;
		case ZT_RADPAR_TXGAIN:
			/* if out of range */
			if (wc->radmode[chan->chanpos -1] & RADMODE_PREEMP)
			{
				if ((stack.p.data <= -2400) || (stack.p.data > 0))
				{
					return -EINVAL;
				}
			}
			else
			{
				if ((stack.p.data <= -3600) || (stack.p.data > 1200))
				{
					return -EINVAL;
				}
			}
			wc->txgain[chan->chanpos - 1] = stack.p.data + 3599;
			break;
		case ZT_RADPAR_DEEMP:
			if (stack.p.data)
				wc->radmode[chan->chanpos - 1] |= RADMODE_DEEMP;
			else
				wc->radmode[chan->chanpos - 1] &= ~RADMODE_DEEMP;
			wc->rxgain[chan->chanpos - 1] = 1199;
			break;
		case ZT_RADPAR_PREEMP:
			if (stack.p.data)
				wc->radmode[chan->chanpos - 1] |= RADMODE_PREEMP;
			else
				wc->radmode[chan->chanpos - 1] &= ~RADMODE_PREEMP;
			wc->txgain[chan->chanpos - 1] = 3599;
			break;
		default:
			return -EINVAL;
		}
		qrv_dosetup(chan,wc);
		return 0;				
	default:
		return -ENOTTY;
	}
	return 0;
}

static int wctdm_open(struct zt_chan *chan)
{
	struct wctdm *wc = chan->pvt;
	int channo = chan->chanpos - 1;
	unsigned long flags;

	if (!(wc->cardflag & (1 << (chan->chanpos - 1))))
		return -ENODEV;
	if (wc->dead)
		return -ENODEV;
	wc->usecount++;
#ifndef LINUX26
	MOD_INC_USE_COUNT;
#else
	try_module_get(THIS_MODULE);
#endif
	/* Reset the mwi indicators */
	spin_lock_irqsave(&wc->reglock, flags);
	wc->mods[channo].fxo.neonmwi_debounce = 0;
	wc->mods[channo].fxo.neonmwi_offcounter = 0;
	wc->mods[channo].fxo.neonmwi_state = 0;
	spin_unlock_irqrestore(&wc->reglock, flags);
	
	return 0;
}

static int wctdm_watchdog(struct zt_span *span, int event)
{
	printk("TDM: Called watchdog\n");
	return 0;
}

static int wctdm_close(struct zt_chan *chan)
{
	struct wctdm *wc = chan->pvt;
	int x;
	signed char reg;
	wc->usecount--;
#ifndef LINUX26
	MOD_DEC_USE_COUNT;
#else
	module_put(THIS_MODULE);
#endif
	for (x=0;x<wc->cards;x++) {
		if (wc->modtype[x] == MOD_TYPE_FXS)
			wc->mods[x].fxs.idletxhookstate = 1;
		if (wc->modtype[x] == MOD_TYPE_QRV)
		{
			int qrvcard = x & 0xfc;

			wc->qrvhook[x] = 0;
			wc->qrvhook[x + 2] = 0xff;
			wc->debouncetime[x] = QRV_DEBOUNCETIME;
			wc->qrvdebtime[x] = 0;
			wc->radmode[x] = 0;
			wc->txgain[x] = 3599;
			wc->rxgain[x] = 1199;
			reg = 0;
			if (!wc->qrvhook[qrvcard]) reg |= 1;
			if (!wc->qrvhook[qrvcard + 1]) reg |= 0x10;
			wc->sethook[qrvcard] = CMD_WR(3, reg);
			qrv_dosetup(chan,wc);
		}
	}
	/* If we're dead, release us now */
	if (!wc->usecount && wc->dead) 
		wctdm_release(wc);
	return 0;
}

static int wctdm_hooksig(struct zt_chan *chan, zt_txsig_t txsig)
{
	struct wctdm *wc = chan->pvt;
	int reg=0,qrvcard;
	if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_QRV) {
		qrvcard = (chan->chanpos - 1) & 0xfc;
		switch(txsig) {
		case ZT_TXSIG_START:
		case ZT_TXSIG_OFFHOOK:
			wc->qrvhook[chan->chanpos - 1] = 1;
			break;
		case ZT_TXSIG_ONHOOK:
			wc->qrvhook[chan->chanpos - 1] = 0;
			break;
		default:
			printk("wctdm24xxp: Can't set tx state to %d\n", txsig);
		}
		reg = 0;
		if (!wc->qrvhook[qrvcard]) reg |= 1;
		if (!wc->qrvhook[qrvcard + 1]) reg |= 0x10;
		wc->sethook[qrvcard] = CMD_WR(3, reg);
		/* wctdm_setreg(wc, qrvcard, 3, reg); */
	} else if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXO) {
		switch(txsig) {
		case ZT_TXSIG_START:
		case ZT_TXSIG_OFFHOOK:
			wc->mods[chan->chanpos - 1].fxo.offhook = 1;
			wc->sethook[chan->chanpos - 1] = CMD_WR(5, 0x9);
			/* wctdm_setreg(wc, chan->chanpos - 1, 5, 0x9); */
			break;
		case ZT_TXSIG_ONHOOK:
			wc->mods[chan->chanpos - 1].fxo.offhook = 0;
			wc->sethook[chan->chanpos - 1] = CMD_WR(5, 0x8);
			/* wctdm_setreg(wc, chan->chanpos - 1, 5, 0x8); */
			break;
		default:
			printk("wctdm24xxp: Can't set tx state to %d\n", txsig);
		}
	} else {
		switch(txsig) {
		case ZT_TXSIG_ONHOOK:
			switch(chan->sig) {
			case ZT_SIG_EM:
			case ZT_SIG_FXOKS:
			case ZT_SIG_FXOLS:
				wc->mods[chan->chanpos - 1].fxs.lasttxhook = 0x10 |
					wc->mods[chan->chanpos - 1].fxs.idletxhookstate;
				break;
			case ZT_SIG_FXOGS:
				wc->mods[chan->chanpos - 1].fxs.lasttxhook = 0x13;
				break;
			}
			break;
		case ZT_TXSIG_OFFHOOK:
			switch(chan->sig) {
			case ZT_SIG_EM:
				wc->mods[chan->chanpos - 1].fxs.lasttxhook = 0x15;
				break;
			default:
				wc->mods[chan->chanpos - 1].fxs.lasttxhook = 0x10 |
					wc->mods[chan->chanpos - 1].fxs.idletxhookstate;
				break;
			}
			break;
		case ZT_TXSIG_START:
			wc->mods[chan->chanpos - 1].fxs.lasttxhook = 0x14;
			break;
		case ZT_TXSIG_KEWL:
			wc->mods[chan->chanpos - 1].fxs.lasttxhook = 0x10;
			break;
		default:
			printk("wctdm24xxp: Can't set tx state to %d\n", txsig);
		}
		if (debug & DEBUG_CARD)
			printk("Setting FXS hook state to %d (%02x)\n", txsig, reg);

		
		wc->sethook[chan->chanpos - 1] = CMD_WR(64, wc->mods[chan->chanpos - 1].fxs.lasttxhook);
		/* wctdm_setreg(wc, chan->chanpos - 1, 64, wc->mods[chan->chanpos - 1].fxs.lasttxhook); */
	}
	return 0;
}

static void wctdm_dacs_connect(struct wctdm *wc, int srccard, int dstcard)
{

	if (wc->dacssrc[dstcard] > - 1) {
		printk("wctdm_dacs_connect: Can't have double sourcing yet!\n");
		return;
	}
	if (!((wc->modtype[srccard] == MOD_TYPE_FXS)||(wc->modtype[srccard] == MOD_TYPE_FXO))){
		printk("wctdm_dacs_connect: Unsupported modtype for card %d\n", srccard);
		return;
	}
	if (!((wc->modtype[dstcard] == MOD_TYPE_FXS)||(wc->modtype[dstcard] == MOD_TYPE_FXO))){
		printk("wctdm_dacs_connect: Unsupported modtype for card %d\n", dstcard);
		return;
	}
	if (debug)
		printk("connect %d => %d\n", srccard, dstcard);
	wc->dacssrc[dstcard] = srccard;

	/* make srccard transmit to srccard+24 on the TDM bus */
	if (wc->modtype[srccard] == MOD_TYPE_FXS) {
		/* proslic */
		wctdm_setreg(wc, srccard, PCM_XMIT_START_COUNT_LSB, ((srccard+24) * 8) & 0xff); 
		wctdm_setreg(wc, srccard, PCM_XMIT_START_COUNT_MSB, ((srccard+24) * 8) >> 8);
	} else if(wc->modtype[srccard] == MOD_TYPE_FXO) { 
		/* daa */
		wctdm_setreg(wc, srccard, 34, ((srccard+24) * 8) & 0xff); /* TX */
		wctdm_setreg(wc, srccard, 35, ((srccard+24) * 8) >> 8);   /* TX */
	}

	/* have dstcard receive from srccard+24 on the TDM bus */
	if (wc->modtype[dstcard] == MOD_TYPE_FXS) {
		/* proslic */
    	wctdm_setreg(wc, dstcard, PCM_RCV_START_COUNT_LSB,  ((srccard+24) * 8) & 0xff);
		wctdm_setreg(wc, dstcard, PCM_RCV_START_COUNT_MSB,  ((srccard+24) * 8) >> 8);
	} else if(wc->modtype[dstcard] == MOD_TYPE_FXO) {
		/* daa */
		wctdm_setreg(wc, dstcard, 36, ((srccard+24) * 8) & 0xff); /* RX */
		wctdm_setreg(wc, dstcard, 37, ((srccard+24) * 8) >> 8);   /* RX */
	}

}

static void wctdm_dacs_disconnect(struct wctdm *wc, int card)
{
	if (wc->dacssrc[card] > -1) {
		if (debug)
			printk("wctdm_dacs_disconnect: restoring TX for %d and RX for %d\n",wc->dacssrc[card], card);

		/* restore TX (source card) */
		if(wc->modtype[wc->dacssrc[card]] == MOD_TYPE_FXS){
			wctdm_setreg(wc, wc->dacssrc[card], PCM_XMIT_START_COUNT_LSB, (wc->dacssrc[card] * 8) & 0xff);
			wctdm_setreg(wc, wc->dacssrc[card], PCM_XMIT_START_COUNT_MSB, (wc->dacssrc[card] * 8) >> 8);
		} else if(wc->modtype[wc->dacssrc[card]] == MOD_TYPE_FXO){
			wctdm_setreg(wc, card, 34, (card * 8) & 0xff);
			wctdm_setreg(wc, card, 35, (card * 8) >> 8);
		} else {
			printk("WARNING: wctdm_dacs_disconnect() called on unsupported modtype\n");
		}

		/* restore RX (this card) */
		if(wc->modtype[card] == MOD_TYPE_FXS){
	   		wctdm_setreg(wc, card, PCM_RCV_START_COUNT_LSB, (card * 8) & 0xff);
	    	wctdm_setreg(wc, card, PCM_RCV_START_COUNT_MSB, (card * 8) >> 8);
		} else if(wc->modtype[card] == MOD_TYPE_FXO){
			wctdm_setreg(wc, card, 36, (card * 8) & 0xff);
			wctdm_setreg(wc, card, 37, (card * 8) >> 8);
		} else {
			printk("WARNING: wctdm_dacs_disconnect() called on unsupported modtype\n");
		}

		wc->dacssrc[card] = -1;
	}
}

static int wctdm_dacs(struct zt_chan *dst, struct zt_chan *src)
{
	struct wctdm *wc;

	if(!nativebridge)
		return 0; /* should this return -1 since unsuccessful? */

	wc = dst->pvt;

	if(src) {
		wctdm_dacs_connect(wc, src->chanpos - 1, dst->chanpos - 1);
		if (debug)
			printk("dacs connecct: %d -> %d!\n\n", src->chanpos, dst->chanpos);
	} else {
		wctdm_dacs_disconnect(wc, dst->chanpos - 1);
		if (debug)
			printk("dacs disconnect: %d!\n", dst->chanpos);
	}
	return 0;
}

static int wctdm_initialize(struct wctdm *wc)
{
	int x;
	struct pci_dev *pdev = voicebus_get_pci_dev(wc->vb);

	/* Zapata stuff */
	sprintf(wc->span.name, "WCTDM/%d", wc->pos);
	snprintf(wc->span.desc, sizeof(wc->span.desc) - 1, "%s Board %d", wc->variety, wc->pos + 1);
	snprintf(wc->span.location, sizeof(wc->span.location) - 1,
		 "PCI%s Bus %02d Slot %02d", (wc->flags[0] & FLAG_EXPRESS) ? " Express" : "",
		 pdev->bus->number, PCI_SLOT(pdev->devfn) + 1);
	wc->span.manufacturer = "Digium";
	strncpy(wc->span.devicetype, wc->variety, sizeof(wc->span.devicetype) - 1);
	if (alawoverride) {
		printk("ALAW override parameter detected.  Device will be operating in ALAW\n");
		wc->span.deflaw = ZT_LAW_ALAW;
	} else
		wc->span.deflaw = ZT_LAW_MULAW;
	for (x=0;x<wc->cards;x++) {
		sprintf(wc->chans[x].name, "WCTDM/%d/%d", wc->pos, x);
		wc->chans[x].sigcap = ZT_SIG_FXOKS | ZT_SIG_FXOLS | ZT_SIG_FXOGS | ZT_SIG_SF | ZT_SIG_EM | ZT_SIG_CLEAR;
		wc->chans[x].sigcap |= ZT_SIG_FXSKS | ZT_SIG_FXSLS | ZT_SIG_SF | ZT_SIG_CLEAR;
		wc->chans[x].chanpos = x+1;
		wc->chans[x].pvt = wc;
	}
	wc->span.chans = wc->chans;
	wc->span.channels = wc->type;
	wc->span.irq = pdev->irq;
	wc->span.hooksig = wctdm_hooksig;
	wc->span.open = wctdm_open;
	wc->span.close = wctdm_close;
	wc->span.flags = ZT_FLAG_RBS;
	wc->span.ioctl = wctdm_ioctl;
	wc->span.watchdog = wctdm_watchdog;
	wc->span.dacs= wctdm_dacs;
#ifdef VPM_SUPPORT
	wc->span.echocan_with_params = wctdm_echocan_with_params;
#endif	
	init_waitqueue_head(&wc->span.maintq);

	wc->span.pvt = wc;
	return 0;
}

static void wctdm_post_initialize(struct wctdm *wc)
{
	int x;

	/* Finalize signalling  */
	for (x = 0; x <wc->cards; x++) {
		if (wc->cardflag & (1 << x)) {
			if (wc->modtype[x] == MOD_TYPE_FXO)
				wc->chans[x].sigcap = ZT_SIG_FXSKS | ZT_SIG_FXSLS | ZT_SIG_SF | ZT_SIG_CLEAR;
			else if (wc->modtype[x] == MOD_TYPE_FXS)
				wc->chans[x].sigcap = ZT_SIG_FXOKS | ZT_SIG_FXOLS | ZT_SIG_FXOGS | ZT_SIG_SF | ZT_SIG_EM | ZT_SIG_CLEAR;
			else if (wc->modtype[x] == MOD_TYPE_QRV)
				wc->chans[x].sigcap = ZT_SIG_SF | ZT_SIG_EM | ZT_SIG_CLEAR;
		} else if (!(wc->chans[x].sigcap & ZT_SIG_BROKEN)) {
			wc->chans[x].sigcap = 0;
		}
	}

	if (wc->vpm)
		strncat(wc->span.devicetype, " with VPM100M", sizeof(wc->span.devicetype) - 1);
	else if (wc->vpm150m)
		strncat(wc->span.devicetype, " with VPMADT032", sizeof(wc->span.devicetype) - 1);
}

#ifdef VPM_SUPPORT

#ifdef VPM150M_SUPPORT

void vpm150m_set_chanconfig_from_state(struct adt_lec_params * parms, int channum, GpakChannelConfig_t *chanconfig)
{
	chanconfig->PcmInPortA = 3;
	chanconfig->PcmInSlotA = channum;
	chanconfig->PcmOutPortA = SerialPortNull;
	chanconfig->PcmOutSlotA = channum;
	chanconfig->PcmInPortB = 2;
	chanconfig->PcmInSlotB = channum;
	chanconfig->PcmOutPortB = 3;
	chanconfig->PcmOutSlotB = channum;
	if (vpmdtmfsupport) {
		chanconfig->ToneTypesA = DTMF_tone;
		chanconfig->MuteToneA = Enabled;
		chanconfig->FaxCngDetA = Enabled;
	} else {
		chanconfig->ToneTypesA = Null_tone;
		chanconfig->MuteToneA = Disabled;
		chanconfig->FaxCngDetA = Disabled;
	}
	chanconfig->ToneTypesB = Null_tone;
	chanconfig->EcanEnableA = Enabled;
	chanconfig->EcanEnableB = Disabled;
	chanconfig->MuteToneB = Disabled;
	chanconfig->FaxCngDetB = Disabled;

	if (alawoverride)
		chanconfig->SoftwareCompand = cmpPCMA;
	else
		chanconfig->SoftwareCompand = cmpPCMU;

	chanconfig->FrameRate = rate2ms;
	chanconfig->EcanParametersA.EcanTapLength = 1024;
	chanconfig->EcanParametersA.EcanNlpType = parms->nlp_type;
	chanconfig->EcanParametersA.EcanAdaptEnable = 1;
	chanconfig->EcanParametersA.EcanG165DetEnable = 1;
	chanconfig->EcanParametersA.EcanDblTalkThresh = 6;
	chanconfig->EcanParametersA.EcanNlpThreshold = parms->nlp_threshold;
	chanconfig->EcanParametersA.EcanNlpConv = 0;
	chanconfig->EcanParametersA.EcanNlpUnConv = 0;
	chanconfig->EcanParametersA.EcanNlpMaxSuppress = parms->nlp_max_suppress;
	chanconfig->EcanParametersA.EcanCngThreshold = 43;
	chanconfig->EcanParametersA.EcanAdaptLimit = 50;
	chanconfig->EcanParametersA.EcanCrossCorrLimit = 15;
	chanconfig->EcanParametersA.EcanNumFirSegments = 3;
	chanconfig->EcanParametersA.EcanFirSegmentLen = 64;

	chanconfig->EcanParametersB.EcanTapLength = 1024;
	chanconfig->EcanParametersB.EcanNlpType = parms->nlp_type;
	chanconfig->EcanParametersB.EcanAdaptEnable = 1;
	chanconfig->EcanParametersB.EcanG165DetEnable = 1;
	chanconfig->EcanParametersB.EcanDblTalkThresh = 6;
	chanconfig->EcanParametersB.EcanNlpThreshold = parms->nlp_threshold;
	chanconfig->EcanParametersB.EcanNlpConv = 0;
	chanconfig->EcanParametersB.EcanNlpUnConv = 0;
	chanconfig->EcanParametersB.EcanNlpMaxSuppress = parms->nlp_max_suppress;
	chanconfig->EcanParametersB.EcanCngThreshold = 43;
	chanconfig->EcanParametersB.EcanAdaptLimit = 50;
	chanconfig->EcanParametersB.EcanCrossCorrLimit = 15;
	chanconfig->EcanParametersB.EcanNumFirSegments = 3;
	chanconfig->EcanParametersB.EcanFirSegmentLen = 64;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void vpm150m_bh(void *data)
{
	struct vpm150m *vpm150m = data;
#else
static void vpm150m_bh(struct work_struct *data)
{
	struct vpm150m *vpm150m = container_of(data, struct vpm150m, work);
#endif
	struct wctdm *wc = vpm150m->wc;
	int i;
	
	for (i = 0; i < wc->type; i++) {
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
				if (debug & DEBUG_ECHOCAN)
					printk("DTMF mute enable took %d ms\n", wc->intcount - start);
			} else {
				res = gpakAlgControl(vpm150m->dspid, i, DisableDTMFMuteA, &pstatus);
				if (debug & DEBUG_ECHOCAN)
					printk("DTMF mute disable took %d ms\n", wc->intcount - start);
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

			if (debug & DEBUG_ECHOCAN)
				printk("ReadEventFIFOMessage took %d ms\n", wc->intcount - start);

			if (res == RefInvalidEvent || res == RefDspCommFailure) {
				printk("VPM Comm Error\n");
				continue;
			}

			if (res == RefNoEventAvail) {
				continue;
			}

			if (eventcode == EventToneDetect) {
				GpakToneCodes_t tone = eventdata.toneEvent.ToneCode;
				int duration = eventdata.toneEvent.ToneDuration;
				char zaptone = vpm150mtone_to_zaptone(tone);

				if (debug & DEBUG_ECHOCAN)
					printk("Channel %d: Detected DTMF tone %d of duration %d!!!\n", channel + 1, tone, duration);

				if (test_bit(channel, &wc->dtmfmask) && (eventdata.toneEvent.ToneDuration > 0)) {
					struct zt_chan *chan = &wc->chans[channel];

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
							if (debug & DEBUG_ECHOCAN)
								printk("Queuing DTMFDOWN %c\n", zaptone);
							set_bit(channel, &wc->dtmfactive);
							zt_qevent_lock(chan, (ZT_EVENT_DTMFDOWN | zaptone));
						}
					} else if ((tone == EndofMFDigit) && test_bit(channel, &wc->dtmfactive)) {
						if (debug & DEBUG_ECHOCAN)
							printk("Queuing DTMFUP %c\n", vpm150mtone_to_zaptone(vpm150m->curtone[channel]));
						zt_qevent_lock(chan, (ZT_EVENT_DTMFUP | vpm150mtone_to_zaptone(vpm150m->curtone[channel])));
						clear_bit(channel, &wc->dtmfactive);
					}
				}
			}
		} while ((res != RefNoEventAvail) && (res != RefInvalidEvent) && (res != RefDspCommFailure));
	}
	
	for (i = 0; i < wc->type; i++) {
		unsigned int start = wc->intcount;
		GPAK_AlgControlStat_t pstatus;
		int res = 1;

		if ((vpm150m->desiredecstate[i].nlp_type != vpm150m->curecstate[i].nlp_type)
			|| (vpm150m->desiredecstate[i].nlp_threshold != vpm150m->curecstate[i].nlp_threshold)
			|| (vpm150m->desiredecstate[i].nlp_max_suppress != vpm150m->curecstate[i].nlp_max_suppress)) {

			GPAK_ChannelConfigStat_t cstatus;
			GPAK_TearDownChanStat_t tstatus;
			GpakChannelConfig_t chanconfig;

			if (debug & DEBUG_ECHOCAN)
				printk("Reconfiguring chan %d for nlp %d, nlp_thresh %d, and max_supp %d\n", i + 1, vpm150m->desiredecstate[i].nlp_type,
					vpm150m->desiredecstate[i].nlp_threshold, vpm150m->desiredecstate[i].nlp_max_suppress);

			vpm150m_set_chanconfig_from_state(&vpm150m->desiredecstate[i], i, &chanconfig);

			if ((res = gpakTearDownChannel(vpm150m->dspid, i, &tstatus))) {
				goto vpm_bh_out;
			}

			if ((res = gpakConfigureChannel(vpm150m->dspid, i, tdmToTdm, &chanconfig, &cstatus))) {
				goto vpm_bh_out;
			}

			if (!vpm150m->desiredecstate[i].tap_length)
				res = gpakAlgControl(vpm150m->dspid, i, BypassEcanA, &pstatus);

		} else if (vpm150m->desiredecstate[i].tap_length != vpm150m->curecstate[i].tap_length) {
			if (vpm150m->desiredecstate[i].tap_length) {
				res = gpakAlgControl(vpm150m->dspid, i, EnableEcanA, &pstatus);
				if (debug & DEBUG_ECHOCAN)
					printk("Echocan enable took %d ms\n", wc->intcount - start);
			} else {
				res = gpakAlgControl(vpm150m->dspid, i, BypassEcanA, &pstatus);
				if (debug & DEBUG_ECHOCAN)
					printk("Echocan disable took %d ms\n", wc->intcount - start);
			}
		}

vpm_bh_out:
		if (!res)
			vpm150m->curecstate[i] = vpm150m->desiredecstate[i];
	}
			
	return;
}
	
static int vpm150m_config_hw(struct wctdm *wc)
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
	portconfig.SlotsSelect2 = SlotCfg2Groups;
	portconfig.FirstBlockNum2 = 0;
	portconfig.FirstSlotMask2 = 0xffff;
	portconfig.SecBlockNum2 = 1;
	portconfig.SecSlotMask2 = 0xffff;
	portconfig.SerialWordSize2 = SerWordSize8;
	portconfig.CompandingMode2 = cmpNone;
	portconfig.TxFrameSyncPolarity2 = FrameSyncActHigh;
	portconfig.RxFrameSyncPolarity2 = FrameSyncActHigh;
	portconfig.TxClockPolarity2 = SerClockActHigh;
	portconfig.RxClockPolarity2 = SerClockActLow;
	portconfig.TxDataDelay2 = DataDelay0;
	portconfig.RxDataDelay2 = DataDelay0;
	portconfig.DxDelay2 = Disabled;
	portconfig.ThirdSlotMask2 = 0x0000;
	portconfig.FouthSlotMask2 = 0x0000;
	portconfig.FifthSlotMask2 = 0x0000;
	portconfig.SixthSlotMask2 = 0x0000;
	portconfig.SevenSlotMask2 = 0x0000;
	portconfig.EightSlotMask2 = 0x0000;

	/* Third Serial Port Config */
	portconfig.SlotsSelect3 = SlotCfg2Groups;
	portconfig.FirstBlockNum3 = 0;
	portconfig.FirstSlotMask3 = 0xffff;
	portconfig.SecBlockNum3 = 1;
	portconfig.SecSlotMask3 = 0xffff;
	portconfig.SerialWordSize3 = SerWordSize8;
	portconfig.CompandingMode3 = cmpNone;
	portconfig.TxFrameSyncPolarity3 = FrameSyncActHigh;
	portconfig.RxFrameSyncPolarity3 = FrameSyncActHigh;
	portconfig.TxClockPolarity3 = SerClockActHigh;
	portconfig.RxClockPolarity3 = SerClockActLow;
	portconfig.TxDataDelay3 = DataDelay0;
	portconfig.RxDataDelay3 = DataDelay0;
	portconfig.DxDelay3 = Disabled;
	portconfig.ThirdSlotMask3 = 0x0000;
	portconfig.FouthSlotMask3 = 0x0000;
	portconfig.FifthSlotMask3 = 0x0000;
	portconfig.SixthSlotMask3 = 0x0000;
	portconfig.SevenSlotMask3 = 0x0000;
	portconfig.EightSlotMask3 = 0x0000;

	if ((configportstatus = gpakConfigurePorts(vpm150m->dspid, &portconfig, &pstatus))) {
		printk("Configuration of ports failed (%d)!\n", configportstatus);
		return -1;
	} else {
		if (debug & DEBUG_ECHOCAN)
			printk("Configured McBSP ports successfully\n");
	}

	if ((res = gpakPingDsp(vpm150m->dspid, &vpm150m->version))) {
		printk("Error pinging DSP (%d)\n", res);
		return -1;
	}

	for (i = 0; i < wc->type; i++) {
		vpm150m->curecstate[i].tap_length = 0;
		vpm150m->curecstate[i].nlp_type = vpmnlptype;
		vpm150m->curecstate[i].nlp_threshold = vpmnlpthresh;
		vpm150m->curecstate[i].nlp_max_suppress = vpmnlpmaxsupp;
	
		vpm150m->desiredecstate[i].tap_length = 0;
		vpm150m->desiredecstate[i].nlp_type = vpmnlptype;
		vpm150m->desiredecstate[i].nlp_threshold = vpmnlpthresh;
		vpm150m->desiredecstate[i].nlp_max_suppress = vpmnlpmaxsupp;
	
		vpm150m_set_chanconfig_from_state(&vpm150m->curecstate[i], i, &chanconfig);
	
		if ((res = gpakConfigureChannel(vpm150m->dspid, i, tdmToTdm, &chanconfig, &cstatus))) {
			printk("Unable to configure channel (%d)\n", res);
			if (res == 1) {
				printk("Reason %d\n", cstatus);
			}
	
			return -1;
		}

		if ((res = gpakAlgControl(vpm150m->dspid, i, BypassEcanA, &algstatus))) {
			printk("Unable to disable echo can on channel %d (reason %d:%d)\n", i + 1, res, algstatus);
			return -1;
		}

		if (vpmdtmfsupport) {
			if ((res = gpakAlgControl(vpm150m->dspid, i, DisableDTMFMuteA, &algstatus))) {
				printk("Unable to disable dtmf muting  on channel %d (reason %d:%d)\n", i + 1, res, algstatus);
				return -1;
			}
		}
	}

	if ((res = gpakPingDsp(vpm150m->dspid, &vpm150m->version))) {
		printk("Error pinging DSP (%d)\n", res);
		return -1;
	}

	vpm150m->wq = create_singlethread_workqueue("wctdm24xxp");
	vpm150m->wc = wc;

	if (!vpm150m->wq) {
		printk("Unable to create work queue!\n");
		return -1;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	INIT_WORK(&vpm150m->work, vpm150m_bh, vpm150m);
#else
	INIT_WORK(&vpm150m->work, vpm150m_bh);
#endif

	/* Turn on DTMF detection */
	if (vpmdtmfsupport)
		set_bit(VPM150M_DTMFDETECT, &vpm150m->control);

	set_bit(VPM150M_ACTIVE, &vpm150m->control);

	return 0;
}
#endif /* VPM150M_SUPPORT */

enum vpmadt032_init_result {
	VPMADT032_SUCCESS,
	VPMADT032_NOT_FOUND,
	VPMADT032_FAILED,
	VPMADT032_DISABLED,
};

static enum vpmadt032_init_result wctdm_vpm150m_init(struct wctdm *wc)
{
	unsigned short i;
	struct vpm150m *vpm150m;
	unsigned short reg;
	unsigned long flags;
	struct pci_dev* pdev = voicebus_get_pci_dev(wc->vb);
	enum vpmadt032_init_result res = VPMADT032_FAILED;

#ifdef VPM150M_SUPPORT
	struct wctdm_firmware fw;
	struct firmware embedded_firmware;
	const struct firmware *firmware = &embedded_firmware;
#if !defined(HOTPLUG_FIRMWARE)
	extern void _binary_zaptel_fw_vpmadt032_bin_size;
	extern u8 _binary_zaptel_fw_vpmadt032_bin_start[];
#else
	static const char vpmadt032_firmware[] = "zaptel-fw-vpmadt032.bin";
#endif
	gpakDownloadStatus_t downloadstatus;
	gpakPingDspStat_t pingstatus;
#endif

	if (!vpmsupport) {
		printk("VPM: Support Disabled\n");
		wc->vpm150m = NULL;
		return VPMADT032_DISABLED;
	}

	vpm150m = kmalloc(sizeof(struct vpm150m), GFP_KERNEL);

	if (!vpm150m) {
		printk("Unable to allocate VPM150M!\n");
		return VPMADT032_FAILED;
	}

	memset(vpm150m, 0, sizeof(struct vpm150m));

	/* Init our vpm150m struct */
	sema_init(&vpm150m->sem, 1);
	vpm150m->curpage = 0x80;

	for (i = 0; i < WC_MAX_IFACES; i++) {
		if (ifaces[i] == wc)
			vpm150m->dspid = i;
	}

	if (debug & DEBUG_ECHOCAN)
		printk("Setting VPMADT032 DSP ID to %d\n", vpm150m->dspid);

	spin_lock_irqsave(&wc->reglock, flags);
	wc->vpm150m = vpm150m;
	spin_unlock_irqrestore(&wc->reglock, flags);

	for (i = 0; i < 10; i++)
		schluffen(&wc->regq);

	if (debug & DEBUG_ECHOCAN)
		printk("VPMADT032 Testing page access: ");
	for (i = 0; i < 0xf; i++) {
		int x;
		for (x = 0; x < 3; x++) {
			wctdm_vpm150m_setpage(wc, i);
			reg = wctdm_vpm150m_getpage(wc);
			if (reg != i) {
				if (debug & DEBUG_ECHOCAN)
					printk("Failed: Sent %x != %x VPMADT032 Failed HI page test\n", i, reg);
				res = VPMADT032_NOT_FOUND;
				goto failed_exit;
			}
		}
	}
	if (debug & DEBUG_ECHOCAN)
		printk("Passed\n");

	/* Set us up to page 0 */
	wctdm_vpm150m_setpage(wc, 0);
	if (debug & DEBUG_ECHOCAN)
		printk("VPMADT032 now doing address test: ");
	for (i = 0; i < 16; i++) {
		int x;
		for (x = 0; x < 2; x++) {
			wctdm_vpm150m_setreg(wc, 1, 0x1000, &i);
			wctdm_vpm150m_getreg(wc, 1, 0x1000, &reg);
			if (reg != i) { 
				printk("VPMADT032 Failed address test\n");
				goto failed_exit;
			}

		}
	}
	if (debug & DEBUG_ECHOCAN)
		printk("Passed\n");

#ifndef VPM150M_SUPPORT
	printk("Found VPMADT032 module but it is not able to function in anything less than a version 2.6 kernel\n");
	printk("Please update your kernel to a 2.6 or later kernel to enable it\n");
	goto failed_exit;
#else

#if 0
	/* Load the firmware */
	set_bit(VPM150M_SPIRESET, &vpm150m->control);

	/* Wait for it to boot */
	msleep(7000);

	pingstatus = gpakPingDsp(vpm150m->dspid, &version);
	
	if (pingstatus || (version != 0x106)) {
#endif
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

		printk("VPMADT032 Loading firwmare... ");
		downloadstatus = gpakDownloadDsp(vpm150m->dspid, &fw);

		if (firmware != &embedded_firmware)
			release_firmware(firmware);

		if (downloadstatus != 0) {
			printk("Unable to download firmware to VPMADT032 with cause %d\n", downloadstatus);
			goto failed_exit;
		} else {
			printk("Success\n");
		}

		set_bit(VPM150M_SWRESET, &vpm150m->control);

		while (test_bit(VPM150M_SWRESET, &vpm150m->control))
			schluffen(&wc->regq);

#if 0
	}
#endif

	pingstatus = gpakPingDsp(vpm150m->dspid, &vpm150m->version);

	if (!pingstatus) {
		if (debug & DEBUG_ECHOCAN)
			printk("Version of DSP is %x\n", vpm150m->version);
	} else {
		printk("VPMADT032 Failed! Unable to ping the DSP (%d)!\n", pingstatus);
		goto failed_exit;
	}

	if (vpm150m_config_hw(wc)) {
		goto failed_exit;
	}

	return VPMADT032_SUCCESS;
#endif  /* VPM150M_SUPPORT */

failed_exit:
	spin_lock_irqsave(&wc->reglock, flags);
	wc->vpm150m = NULL;
	spin_unlock_irqrestore(&wc->reglock, flags);
	kfree(vpm150m);

	return res;
}

static void wctdm_vpm_set_dtmf_threshold(struct wctdm *wc, unsigned int threshold)
{
	unsigned int x;

	for (x = 0; x < 4; x++) {
		wctdm_vpm_out(wc, x, 0xC4, (threshold >> 8) & 0xFF);
		wctdm_vpm_out(wc, x, 0xC5, (threshold & 0xFF));
	}
	printk("VPM: DTMF threshold set to %d\n", threshold);
}

static void wctdm_vpm_init(struct wctdm *wc)
{
	unsigned char reg;
	unsigned int mask;
	unsigned int ver;
	unsigned char vpmver=0;
	unsigned int i, x, y;

	if (!vpmsupport) {
		printk("VPM: Support Disabled\n");
		wc->vpm = 0;
		return;
	}

	for (x=0;x<NUM_EC;x++) {
		ver = wctdm_vpm_in(wc, x, 0x1a0); /* revision */
		if (debug & DEBUG_ECHOCAN)
			printk("VPM100: Chip %d: ver %02x\n", x, ver);
		if (ver != 0x33) {
			printk("VPM100: %s\n", x ? "Inoperable" : "Not Present");
			wc->vpm = 0;
			return;
		}	

		if (!x) {
			vpmver = wctdm_vpm_in(wc, x, 0x1a6) & 0xf;
			printk("VPM Revision: %02x\n", vpmver);
		}


		/* Setup GPIO's */
		for (y=0;y<4;y++) {
			wctdm_vpm_out(wc, x, 0x1a8 + y, 0x00); /* GPIO out */
			if (y == 3)
				wctdm_vpm_out(wc, x, 0x1ac + y, 0x00); /* GPIO dir */
			else
				wctdm_vpm_out(wc, x, 0x1ac + y, 0xff); /* GPIO dir */
			wctdm_vpm_out(wc, x, 0x1b0 + y, 0x00); /* GPIO sel */
		}

		/* Setup TDM path - sets fsync and tdm_clk as inputs */
		reg = wctdm_vpm_in(wc, x, 0x1a3); /* misc_con */
		wctdm_vpm_out(wc, x, 0x1a3, reg & ~2);

		/* Setup Echo length (256 taps) */
		wctdm_vpm_out(wc, x, 0x022, 0);

		/* Setup timeslots */
		if (vpmver == 0x01) {
			wctdm_vpm_out(wc, x, 0x02f, 0x00); 
			wctdm_vpm_out(wc, x, 0x023, 0xff);
			mask = 0x11111111 << x;
		} else {
			wctdm_vpm_out(wc, x, 0x02f, 0x20  | (x << 3)); 
			wctdm_vpm_out(wc, x, 0x023, 0x3f);
			mask = 0x0000003f;
		}

		/* Setup the tdm channel masks for all chips*/
		for (i = 0; i < 4; i++)
			wctdm_vpm_out(wc, x, 0x33 - i, (mask >> (i << 3)) & 0xff);

		/* Setup convergence rate */
		reg = wctdm_vpm_in(wc,x,0x20);
		reg &= 0xE0;
		if (alawoverride) {
			if (!x)
				printk("VPM: A-law mode\n");
			reg |= 0x01;
		} else {
			if (!x)
				printk("VPM: U-law mode\n");
			reg &= ~0x01;
		}
		wctdm_vpm_out(wc,x,0x20,(reg | 0x20));

		/* Initialize echo cans */
		for (i = 0 ; i < MAX_TDM_CHAN; i++) {
			if (mask & (0x00000001 << i))
				wctdm_vpm_out(wc,x,i,0x00);
		}

		for (i=0;i<30;i++) 
			schluffen(&wc->regq);

		/* Put in bypass mode */
		for (i = 0 ; i < MAX_TDM_CHAN ; i++) {
			if (mask & (0x00000001 << i)) {
				wctdm_vpm_out(wc,x,i,0x01);
			}
		}

		/* Enable bypass */
		for (i = 0 ; i < MAX_TDM_CHAN ; i++) {
			if (mask & (0x00000001 << i))
				wctdm_vpm_out(wc,x,0x78 + i,0x01);
		}
      
		/* Enable DTMF detectors (always DTMF detect all spans) */
		for (i = 0; i < 6; i++) {
			if (vpmver == 0x01) 
				wctdm_vpm_out(wc, x, 0x98 + i, 0x40 | (i << 2) | x);
			else
				wctdm_vpm_out(wc, x, 0x98 + i, 0x40 | i);
		}

		for (i = 0xB8; i < 0xC0; i++)
			wctdm_vpm_out(wc, x, i, 0xFF);
		for (i = 0xC0; i < 0xC4; i++)
			wctdm_vpm_out(wc, x, i, 0xff);

	} 
	/* set DTMF detection threshold */
	wctdm_vpm_set_dtmf_threshold(wc, dtmfthreshold);
	
	if (vpmver == 0x01)
		wc->vpm = 2;
	else
		wc->vpm = 1;

	printk("Enabling VPM100 gain adjustments on any FXO ports found\n");
	for (i = 0; i < wc->type; i++) {
		if (wc->modtype[i] == MOD_TYPE_FXO) {
			/* Apply negative Tx gain of 4.5db to DAA */
			wctdm_setreg(wc, i, 38, 0x14);	/* 4db */
			wctdm_setreg(wc, i, 40, 0x15);	/* 0.5db */

			/* Apply negative Rx gain of 4.5db to DAA */
			wctdm_setreg(wc, i, 39, 0x14);	/* 4db */
			wctdm_setreg(wc, i, 41, 0x15);	/* 0.5db */
		}
	}

}

#endif

static int wctdm_locate_modules(struct wctdm *wc)
{
	int x;
	unsigned long flags;
	unsigned int startinglatency = voicebus_current_latency(wc->vb);
	wc->ctlreg = 0x00;
	
	/* Make sure all units go into daisy chain mode */
	spin_lock_irqsave(&wc->reglock, flags);
	wc->span.irqmisses = 0;
	for (x=0;x<wc->cards;x++) 
		wc->modtype[x] = MOD_TYPE_FXSINIT;
#ifdef VPM_SUPPORT
	wc->vpm = -1;
	for (x = wc->cards; x < wc->cards+NUM_EC; x++)
		wc->modtype[x] = MOD_TYPE_VPM;
#endif
	spin_unlock_irqrestore(&wc->reglock, flags);
	/* Wait just a bit */
	for (x=0;x<10;x++) 
		schluffen(&wc->regq);
	spin_lock_irqsave(&wc->reglock, flags);
	for (x=0;x<wc->cards;x++) 
		wc->modtype[x] = MOD_TYPE_FXS;
	spin_unlock_irqrestore(&wc->reglock, flags);

#if 0
	/* XXX */
	cmddesc = 0;
#endif	
	/* Now that all the cards have been reset, we can stop checking them all if there aren't as many */
	spin_lock_irqsave(&wc->reglock, flags);
	wc->cards = wc->type;
	spin_unlock_irqrestore(&wc->reglock, flags);

	/* Reset modules */
	for (x=0;x<wc->cards;x++) {
		int sane=0,ret=0,readi=0;
retry:
		if (voicebus_current_latency(wc->vb) > startinglatency) {
			return -EAGAIN;
		}
		/* Init with Auto Calibration */
		if (!(ret = wctdm_init_proslic(wc, x, 0, 0, sane))) {
			wc->cardflag |= (1 << x);
			if (debug & DEBUG_CARD) {
				readi = wctdm_getreg(wc,x,LOOP_I_LIMIT);
				printk("Proslic module %d loop current is %dmA\n",x,
					((readi*3)+20));
			}
			printk("Port %d: Installed -- AUTO FXS/DPO\n", x + 1);
		} else {
			if(ret!=-2) {
				sane=1;
				/* Init with Manual Calibration */
				if (!wctdm_init_proslic(wc, x, 0, 1, sane)) {
					wc->cardflag |= (1 << x);
                                if (debug & DEBUG_CARD) {
                                        readi = wctdm_getreg(wc,x,LOOP_I_LIMIT);
                                        printk("Proslic module %d loop current is %dmA\n",x,
                                 	       ((readi*3)+20));
                                }
					printk("Port %d: Installed -- MANUAL FXS\n",x + 1);
				} else {
					printk("Port %d: FAILED FXS (%s)\n", x + 1, fxshonormode ? fxo_modes[_opermode].name : "FCC");
					wc->chans[x].sigcap = ZT_SIG_BROKEN | __ZT_SIG_FXO;
				} 
			} else if (!(ret = wctdm_init_voicedaa(wc, x, 0, 0, sane))) {
				wc->cardflag |= (1 << x);
				printk("Port %d: Installed -- AUTO FXO (%s mode)\n",x + 1, fxo_modes[_opermode].name);
 			} else if (!wctdm_init_qrvdri(wc,x)) {
 				wc->cardflag |= 1 << x;
 				printk("Port %d: Installed -- QRV DRI card\n",x + 1);
			} else {
 				if ((wc->type != 24) && ((x & 0x3) == 1) && !wc->altcs[x]) {
 					spin_lock_irqsave(&wc->reglock, flags);
					wc->altcs[x] = 2;
					if (wc->type == 4) {
						wc->altcs[x+1] = 3;
						wc->altcs[x+2] = 3;
					}
 					wc->modtype[x] = MOD_TYPE_FXSINIT;
 					spin_unlock_irqrestore(&wc->reglock, flags);
				
 					schluffen(&wc->regq);
 					schluffen(&wc->regq);
 					spin_lock_irqsave(&wc->reglock, flags);
 					wc->modtype[x] = MOD_TYPE_FXS;
 					spin_unlock_irqrestore(&wc->reglock, flags);
 					if (debug & DEBUG_CARD)
 						printk("Trying port %d with alternate chip select\n", x + 1);
 					goto retry;
				} else {
 					printk("Port %d: Not installed\n", x + 1);
 					wc->modtype[x] = MOD_TYPE_NONE;
 					wc->cardflag |= (1 << x);
 				}
			}
		}
	}
#ifdef VPM_SUPPORT
	wctdm_vpm_init(wc);
	if (wc->vpm) {
		printk("VPM: Present and operational (Rev %c)\n", 'A' + wc->vpm - 1);
		wc->ctlreg |= 0x10;
	} else {
		enum vpmadt032_init_result res;
		spin_lock_irqsave(&wc->reglock, flags);
		for (x = NUM_CARDS; x < NUM_CARDS + NUM_EC; x++)
			wc->modtype[x] = MOD_TYPE_NONE;
		spin_unlock_irqrestore(&wc->reglock, flags);
		res = wctdm_vpm150m_init(wc);
		/* In case there was an error while we were loading the VPM module. */
		if (voicebus_current_latency(wc->vb) > startinglatency) {
			return -EAGAIN;
		}
		switch (res) {
		case VPMADT032_SUCCESS:
			printk("VPMADT032: Present and operational (Firmware version %x)\n", wc->vpm150m->version);
			wc->ctlreg |= 0x10;
			break;
		case VPMADT032_DISABLED:
		case VPMADT032_NOT_FOUND:
			/* nothing */
			break;
		default:
			return -EIO;
		}
	}
#endif
	/* In case there was an error while we were loading the VPM module. */
	if (voicebus_current_latency(wc->vb) > startinglatency) {
		return -EAGAIN;
	}
	return 0;
}

static struct pci_driver wctdm_driver;

static int __devinit wctdm_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct wctdm *wc;
	struct wctdm_desc *d = (struct wctdm_desc *)ent->driver_data;
	int i;
	int y;
	int ret;

	neonmwi_offlimit_cycles = neonmwi_offlimit /MS_PER_HOOKCHECK;

retry:
	wc = kmalloc(sizeof(struct wctdm), GFP_KERNEL);
	if (!wc) {
		/* \todo Print debug message. */
		return -ENOMEM;
	}
	memset(wc, 0, sizeof(*wc));
	spin_lock(&ifacelock);	
	/* \todo this is a candidate for removal... */
	for (i = 0; i < WC_MAX_IFACES; ++i) {
		if (!ifaces[i]) {
			ifaces[i] = wc;
			break;
		}
	}
	spin_unlock(&ifacelock);

	snprintf(wc->board_name, sizeof(wc->board_name)-1, "%s%d",
		wctdm_driver.name, i);
	ret = voicebus_init(pdev, SFRAME_SIZE, wc->board_name,
		handle_receive, handle_transmit, wc, &wc->vb);
	if (ret) {
		kfree(wc);
		return ret;
	}
	BUG_ON(!wc->vb);

	if (VOICEBUS_DEFAULT_LATENCY != latency) {
		voicebus_set_minlatency(wc->vb, latency);
	}

	spin_lock_init(&wc->reglock);
	wc->curcard = -1;
	wc->cards = NUM_CARDS;
	wc->type = d->ports;
	wc->pos = i;
	wc->variety = d->name;
	wc->txident = 1;
	for (y=0;y<NUM_CARDS;y++) {
		wc->flags[y] = d->flags;
		wc->dacssrc[y] = -1;
	}

	init_waitqueue_head(&wc->regq);

	if (wctdm_initialize(wc)) {
		voicebus_release(wc->vb);
		wc->vb = NULL;
		kfree(wc);
		return -EIO;
	}


	/* Keep track of which device we are */
	pci_set_drvdata(pdev, wc);

	/* Start the hardware processing. */
	if (voicebus_start(wc->vb)) {
		BUG_ON(1);
	}
	
	/* Now track down what modules are installed */
	ret = wctdm_locate_modules(wc);
	if (-EAGAIN == ret ) {
		/* The voicebus library increased the latency during
		 * initialization.  There is a chance that the hardware is in
		 * an inconsistent state, so lets increase the default latency
		 * and start the initialization over.
		 */
		printk(KERN_NOTICE "%s: Restarting board initialization " \
		 "after increasing latency.\n", wc->board_name);
		latency = voicebus_current_latency(wc->vb);
		wctdm_release(wc);
		goto retry;
	}
	
	/* Final initialization */
	wctdm_post_initialize(wc);
	
	/* We should be ready for zaptel to come in now. */
	if (zt_register(&wc->span, 0)) {
		printk("Unable to register span with zaptel\n");
		return -1;
	}

	wc->initialized = 1;

	printk("Found a Wildcard TDM: %s (%d modules)\n", wc->variety, wc->type);
	ret = 0;
	
	return ret;
}

static void wctdm_release(struct wctdm *wc)
{
	int i;

	if (wc->initialized) {
		zt_unregister(&wc->span);
	}

	voicebus_release(wc->vb);
	wc->vb = NULL;

	spin_lock(&ifacelock);
	for (i = 0; i < WC_MAX_IFACES; i++)
		if (ifaces[i] == wc)
			break;
	ifaces[i] = NULL;
	spin_unlock(&ifacelock);
	
	kfree(wc);
}

static void __devexit wctdm_remove_one(struct pci_dev *pdev)
{
	struct wctdm *wc = pci_get_drvdata(pdev);

#ifdef VPM150M_SUPPORT
	unsigned long flags;
	struct vpm150m *vpm150m = wc->vpm150m;
#endif

	if (wc) {

#ifdef VPM150M_SUPPORT
		if (vpm150m) {
			clear_bit(VPM150M_DTMFDETECT, &vpm150m->control);
			clear_bit(VPM150M_ACTIVE, &vpm150m->control);
			flush_workqueue(vpm150m->wq);
			destroy_workqueue(vpm150m->wq);
		}
#endif
		voicebus_stop(wc->vb);

#ifdef VPM150M_SUPPORT
		if (vpm150m) {
			spin_lock_irqsave(&wc->reglock, flags);
			wc->vpm150m = NULL;
			vpm150m->wc = NULL;
			spin_unlock_irqrestore(&wc->reglock, flags);
			kfree(wc->vpm150m);
		}
#endif
		/* Release span, possibly delayed */
		if (!wc->usecount) {
			wctdm_release(wc);
			printk("Freed a Wildcard\n");
		}
		else
			wc->dead = 1;
	}
}

static struct pci_device_id wctdm_pci_tbl[] = {
	{ 0xd161, 0x2400, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wctdm2400 },
	{ 0xd161, 0x0800, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wctdm800 },
	{ 0xd161, 0x8002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wcaex800 },
	{ 0xd161, 0x8003, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wcaex2400 },
	{ 0xd161, 0x8005, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wctdm410 },
	{ 0xd161, 0x8006, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wcaex410 },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, wctdm_pci_tbl);

static struct pci_driver wctdm_driver = {
	name: 	"wctdm24xxp",
	probe: 	wctdm_init_one,
#ifdef LINUX26
	remove:	__devexit_p(wctdm_remove_one),
#else
	remove:	wctdm_remove_one,
#endif
	suspend: NULL,
	resume:	NULL,
	id_table: wctdm_pci_tbl,
};

static int __init wctdm_init(void)
{
	int res;
	int x;

	for (x = 0; x < (sizeof(fxo_modes) / sizeof(fxo_modes[0])); x++) {
		if (!strcmp(fxo_modes[x].name, opermode))
			break;
	}
	if (x < sizeof(fxo_modes) / sizeof(fxo_modes[0])) {
		_opermode = x;
	} else {
		printk("Invalid/unknown operating mode '%s' specified.  Please choose one of:\n", opermode);
		for (x = 0; x < sizeof(fxo_modes) / sizeof(fxo_modes[0]); x++)
			printk("  %s\n", fxo_modes[x].name);
		printk("Note this option is CASE SENSITIVE!\n");
		return -ENODEV;
	}

	if (!strcmp(opermode, "AUSTRALIA")) {
		boostringer = 1;
		fxshonormode = 1;
	}

	/* for the voicedaa_check_hook defaults, if the user has not overridden
	   them by specifying them as module parameters, then get the values
	   from the selected operating mode
	*/
	if (battdebounce == 0) {
		battdebounce = fxo_modes[_opermode].battdebounce;
	}
	if (battalarm == 0) {
		battalarm = fxo_modes[_opermode].battalarm;
	}
	if (battthresh == 0) {
		battthresh = fxo_modes[_opermode].battthresh;
	}

	res = zap_pci_module(&wctdm_driver);
	if (res)
		return -ENODEV;
	return 0;
}

static void __exit wctdm_cleanup(void)
{
	pci_unregister_driver(&wctdm_driver);
}

#ifdef LINUX26
module_param(debug, int, 0600);
module_param(fxovoltage, int, 0600);
module_param(loopcurrent, int, 0600);
module_param(robust, int, 0600);
module_param(opermode, charp, 0600);
module_param(lowpower, int, 0600);
module_param(boostringer, int, 0600);
module_param(fastringer, int, 0600);
module_param(fxshonormode, int, 0600);
module_param(battdebounce, uint, 0600);
module_param(battalarm, uint, 0600);
module_param(battthresh, uint, 0600);
module_param(alawoverride, int, 0600);
module_param(nativebridge, int, 0600);
module_param(fxotxgain, int, 0600);
module_param(fxorxgain, int, 0600);
module_param(fxstxgain, int, 0600);
module_param(fxsrxgain, int, 0600);
module_param(ringdebounce, int, 0600);
module_param(fwringdetect, int, 0600);
module_param(latency, int, 0600);
module_param(neonmwi_monitor, int, 0600);
module_param(neonmwi_level, int, 0600);
module_param(neonmwi_envelope, int, 0600);
module_param(neonmwi_offlimit, int, 0600);
#ifdef VPM_SUPPORT
module_param(vpmsupport, int, 0600);
module_param(vpmdtmfsupport, int, 0600);
module_param(dtmfthreshold, int, 0600);
module_param(vpmnlptype, int, 0600);
module_param(vpmnlpthresh, int, 0600);
module_param(vpmnlpmaxsupp, int, 0600);
#endif
#else
MODULE_PARM(debug, "i");
MODULE_PARM(fxovoltage, "i");
MODULE_PARM(loopcurrent, "i");
MODULE_PARM(robust, "i");
MODULE_PARM(opermode, "s");
MODULE_PARM(lowpower, "i");
MODULE_PARM(boostringer, "i");
MODULE_PARM(fastringer, "i");
MODULE_PARM(fxshonormode, "i");
MODULE_PARM(battdebounce, "i");
MODULE_PARM(battalarm, "i");
MODULE_PARM(battthresh, "i");
MODULE_PARM(alawoverride, "i");
MODULE_PARM(nativebridge, "i");
MODULE_PARM(fxotxgain, "i");
MODULE_PARM(fxorxgain, "i");
MODULE_PARM(fxstxgain, "i");
MODULE_PARM(fxsrxgain, "i");
MODULE_PARM(ringdebounce, "i");
MODULE_PARM(fwringdetect, "i");
MODULE_PARM(neonmwi_monitor, "i");
MODULE_PARM(neonmwi_level, "i");
MODULE_PARM(neonmwi_envelope, "i");
MODULE_PARM(neonmwi_offlimit, "i");
#ifdef VPM_SUPPORT
MODULE_PARM(vpmsupport, "i");
MODULE_PARM(vpmdtmfsupport, "i");
MODULE_PARM(dtmfthreshold, "i");
MODULE_PARM(vpmnlptype, "i");
MODULE_PARM(vpmnlpthresh, "i");
MODULE_PARM(vpmnlpmaxsupp, "i");
#endif
#endif
MODULE_DESCRIPTION("Wildcard TDM2400P/TDM800P Zaptel Driver");
MODULE_AUTHOR("Mark Spencer <markster@digium.com>");
#if defined(MODULE_ALIAS)
MODULE_ALIAS("wctdm8xxp");
#endif
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif

module_init(wctdm_init);
module_exit(wctdm_cleanup);
